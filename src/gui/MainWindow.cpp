#include "gui/I18n.h"
#include "gui/MainWindow.h"

#include "core/ClipboardWatcher.h"
#include "core/DownloadQueue.h"
#include "core/SessionStore.h"
#include "core/SettingsStore.h"
#include "core/UrlMatcher.h"
#include "gui/AddDialog.h"
#include "gui/SettingsDialog.h"
#include "models/DownloadItem.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cstring>
#include <vector>

namespace {
constexpr int kColFile = 0;
constexpr int kColSize = 1;
constexpr int kColProgress = 2;
constexpr int kColSpeed = 3;
constexpr int kColStatus = 4;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Phoenix 0.9"));
    resize(820, 460);

    // Phoenix flame icon (multi-size so 16px taskbar and 256px details both crisp).
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/phoenix-%1.png").arg(s));
    setWindowIcon(icon);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* topRow = new QHBoxLayout();
    m_addBtn = new QPushButton(I18n::t("Add"), central);
    m_addBtn->setProperty("accent", true);
    m_pauseBtn = new QPushButton(I18n::t("Pause"), central);
    m_resumeBtn = new QPushButton(I18n::t("Resume"), central);
    m_removeBtn = new QPushButton(I18n::t("Remove"), central);
    m_settingsBtn = new QPushButton(I18n::t("Settings"), central);
    m_clearBtn = new QPushButton(I18n::t("Clear completed"), central);
    m_pauseBtn->setToolTip(I18n::t("Pause the selected download (resumable)"));
    m_resumeBtn->setToolTip(I18n::t("Resume the selected download"));
    m_removeBtn->setToolTip(I18n::t("Remove the selected download from the queue"));
    m_settingsBtn->setToolTip(I18n::t("Saved preferences: folder, connections, speed, power, clipboard"));
    topRow->addWidget(m_addBtn);
    topRow->addWidget(m_pauseBtn);
    topRow->addWidget(m_resumeBtn);
    topRow->addWidget(m_removeBtn);
    topRow->addWidget(m_settingsBtn);
    topRow->addWidget(m_clearBtn);
    topRow->addStretch(1);
    m_maxLbl = new QLabel(I18n::t("Max simultaneous:"), central);
    topRow->addWidget(m_maxLbl);
    m_maxBox = new QSpinBox(central);
    m_maxBox->setRange(1, 5);
    m_maxBox->setValue(3);
    topRow->addWidget(m_maxBox);
    topRow->addSpacing(12);
    m_doneLbl = new QLabel(I18n::t("When done:"), central);
    topRow->addWidget(m_doneLbl);
    m_doneBox = new QComboBox(central);
    m_doneBox->addItems({I18n::t("Do nothing"), I18n::t("Sleep"),
                         I18n::t("Hibernate"), I18n::t("Shutdown")});
    topRow->addWidget(m_doneBox);
    layout->addLayout(topRow);

    m_table = new QTableWidget(0, 5, central);
    m_table->setHorizontalHeaderLabels(
        {I18n::t("File"), I18n::t("Size"), I18n::t("Progress"),
         I18n::t("Speed"), I18n::t("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(kColFile, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QTableWidget::SelectRows);
    m_table->setEditTriggers(QTableWidget::NoEditTriggers);
    layout->addWidget(m_table);

    setCentralWidget(central);

    m_queue = new DownloadQueue(this);
    m_settings = new SettingsStore(QString(), this);
    connect(m_addBtn, &QPushButton::clicked, this, &MainWindow::onAdd);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_resumeBtn, &QPushButton::clicked, this, &MainWindow::onResume);
    connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::onRemove);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::clearCompleted);
    connect(m_maxBox, &QSpinBox::valueChanged, m_queue,
            &DownloadQueue::setMaxConcurrent);
    connect(m_doneBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_queue, &DownloadQueue::setAutoAction);
    connect(m_queue, &DownloadQueue::itemAdded, this, &MainWindow::onItemAdded);
    connect(m_queue, &DownloadQueue::itemChanged, this, &MainWindow::onItemChanged);
    connect(m_queue, &DownloadQueue::itemRemoved, this, &MainWindow::onItemRemoved);
    connect(m_queue, &DownloadQueue::itemAdded, this, &MainWindow::refreshStatus);
    connect(m_queue, &DownloadQueue::itemChanged, this, &MainWindow::refreshStatus);
    connect(m_queue, &DownloadQueue::itemRemoved, this, &MainWindow::refreshStatus);
    connect(m_queue, &DownloadQueue::queueFinished, statusBar(),
            [this] { refreshStatus(); });

    // Double-click a row to reveal its folder in Explorer.
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                QTableWidgetItem* item = m_table->item(row, kColFile);
                if (!item)
                    return;
                const int id = item->data(Qt::UserRole).toInt();
                for (const auto& it : m_queue->items()) {
                    if (it.id == id) {
                        const QFileInfo fi(QString::fromStdString(it.outputPath));
                        if (it.state == DownloadState::Completed && fi.exists())
                            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
                        else
                            QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
                        break;
                    }
                }
            });

    // Clipboard link catching (off until the user enables it from the tray).
    m_clipWatcher = new ClipboardWatcher(this);
    connect(m_clipWatcher, &ClipboardWatcher::urlDetected, this,
            [this](const QString& url) {
                acceptIncomingUrl(url);
                if (m_tray)
                    m_tray->showMessage(
                        QStringLiteral("Phoenix"),
                        I18n::t("Download added from clipboard:\n%1").arg(url),
                        QSystemTrayIcon::Information, 4000);
            });

    setupTray();
    applySettingsToUi();
    restoreSession();
    if (!m_settings->mainGeometry().isEmpty())
        restoreGeometry(m_settings->mainGeometry());
    refreshStatus();
}

void MainWindow::applySettingsToUi() {
    m_maxBox->setValue(m_settings->maxConcurrent());
    m_doneBox->setCurrentIndex(m_settings->autoAction());
    if (m_watchClipAct)
        m_watchClipAct->setChecked(m_settings->watchClipboard());
    if (m_clipWatcher)
        m_clipWatcher->setEnabled(m_settings->watchClipboard());
}

void MainWindow::retranslateUi() {
    m_addBtn->setText(I18n::t("Add"));
    m_pauseBtn->setText(I18n::t("Pause"));
    m_resumeBtn->setText(I18n::t("Resume"));
    m_removeBtn->setText(I18n::t("Remove"));
    m_settingsBtn->setText(I18n::t("Settings"));
    m_clearBtn->setText(I18n::t("Clear completed"));
    m_pauseBtn->setToolTip(I18n::t("Pause the selected download (resumable)"));
    m_resumeBtn->setToolTip(I18n::t("Resume the selected download"));
    m_removeBtn->setToolTip(I18n::t("Remove the selected download from the queue"));
    m_settingsBtn->setToolTip(I18n::t("Saved preferences: folder, connections, speed, power, clipboard"));

    m_maxLbl->setText(I18n::t("Max simultaneous:"));
    m_doneLbl->setText(I18n::t("When done:"));

    // Rebuild the combo without firing the queue slot mid-swap.
    const int idx = m_doneBox->currentIndex();
    m_doneBox->blockSignals(true);
    m_doneBox->clear();
    m_doneBox->addItems({I18n::t("Do nothing"), I18n::t("Sleep"),
                         I18n::t("Hibernate"), I18n::t("Shutdown")});
    m_doneBox->setCurrentIndex(qBound(0, idx, m_doneBox->count() - 1));
    m_doneBox->blockSignals(false);

    m_table->setHorizontalHeaderLabels(
        {I18n::t("File"), I18n::t("Size"), I18n::t("Progress"),
         I18n::t("Speed"), I18n::t("Status")});

    if (m_tray) {
        m_tray->setToolTip(I18n::t("Phoenix Download Manager"));
        m_showAct->setText(I18n::t("Show / Hide"));
        m_addAct->setText(I18n::t("Add Download..."));
        m_settingsAct->setText(I18n::t("Settings..."));
        m_clearAct->setText(I18n::t("Clear completed"));
        m_pauseAllAct->setText(I18n::t("Pause All"));
        m_resumeAllAct->setText(I18n::t("Resume All"));
        m_watchClipAct->setText(I18n::t("Watch clipboard for links"));
        m_quitAct->setText(I18n::t("Quit"));
    }

    for (const auto& it : m_queue->items())
        updateRow(it.id);
    refreshStatus();
}

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QIcon icon = windowIcon();
    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(I18n::t("Phoenix Download Manager"));

    m_trayMenu = new QMenu(this);
    m_showAct = m_trayMenu->addAction(I18n::t("Show / Hide"));
    connect(m_showAct, &QAction::triggered, this, &MainWindow::showWindow);
    m_trayMenu->addSeparator();

    m_addAct = m_trayMenu->addAction(I18n::t("Add Download..."));
    connect(m_addAct, &QAction::triggered, this, &MainWindow::onAdd);

    m_settingsAct = m_trayMenu->addAction(I18n::t("Settings..."));
    connect(m_settingsAct, &QAction::triggered, this, &MainWindow::onSettings);

    m_clearAct = m_trayMenu->addAction(I18n::t("Clear completed"));
    connect(m_clearAct, &QAction::triggered, this, &MainWindow::clearCompleted);

    m_pauseAllAct = m_trayMenu->addAction(I18n::t("Pause All"));
    connect(m_pauseAllAct, &QAction::triggered, this, [this] {
        for (auto it : m_queue->items())
            m_queue->pauseDownload(it.id);
    });

    m_resumeAllAct = m_trayMenu->addAction(I18n::t("Resume All"));
    connect(m_resumeAllAct, &QAction::triggered, this, [this] {
        for (auto it : m_queue->items())
            if (it.state == DownloadState::Paused)
                m_queue->resumeDownload(it.id);
    });

    m_watchClipAct = m_trayMenu->addAction(I18n::t("Watch clipboard for links"));
    m_watchClipAct->setCheckable(true);
    m_watchClipAct->setChecked(m_settings->watchClipboard());
    connect(m_watchClipAct, &QAction::toggled, this, [this](bool on) {
        if (m_settings)
            m_settings->setWatchClipboard(on);
        if (m_clipWatcher)
            m_clipWatcher->setEnabled(on);
    });

    m_trayMenu->addSeparator();
    m_quitAct = m_trayMenu->addAction(I18n::t("Quit"));
    connect(m_quitAct, &QAction::triggered, this, [this] {
        QSystemTrayIcon* t = m_tray;
        m_tray = nullptr; // allow closeEvent to actually quit
        m_firstHide = false;
        if (t)
            t->hide();
        close();
    });

    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    showWindow();
            });
    m_tray->show();
}

void MainWindow::showWindow() {
    show();
    showNormal();
    raise();
    activateWindow();
}

bool MainWindow::startsHidden() const {
    return m_settings && m_settings->startMinimized() && m_tray &&
           QSystemTrayIcon::isSystemTrayAvailable();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing hides to the tray instead of quitting (unless the user asked to
    // quit from the tray menu, which clears m_tray first).
    if (m_tray && QSystemTrayIcon::isSystemTrayAvailable()) {
        if (m_firstHide) {
            m_firstHide = false;
            m_tray->showMessage(
                QStringLiteral("Phoenix"),
                I18n::t("Still running in the tray. Right-click the "
                        "icon for options."),
                QSystemTrayIcon::Information, 4000);
        }
        hide();
        event->ignore();
        return;
    }
    if (m_settings)
        m_settings->setMainGeometry(saveGeometry());
    saveSession();
    QMainWindow::closeEvent(event);
}

QString MainWindow::sanitizeFileName(const QString& fileName) {
    QString cleaned;
    cleaned.reserve(fileName.size());
    for (QChar ch : fileName) {
        uchar c = ch.unicode();
        if (c < 0x20 || strchr("<>:\"/\\|?*", static_cast<char>(c & 0x7f)))
            continue;
        cleaned += ch;
    }
    return cleaned.isEmpty() ? QStringLiteral("download.bin") : cleaned;
}

void MainWindow::acceptIncomingUrl(const QString& url, const QString& fileName) {
    if (url.isEmpty())
        return;
    QString safe = fileName;
    if (safe.isEmpty()) {
        // derive the filename from the URL basename, stripping the query
        QUrl u(url);
        QString path = u.path();
        int slash = path.lastIndexOf('/');
        if (slash >= 0)
            safe = path.mid(slash + 1);
        else
            safe = path;
        if (safe.isEmpty())
            safe = QStringLiteral("download.bin");
    }
    QString dir = m_settings ? m_settings->defaultDirectory() : QString();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/Downloads");
    const QString target = dir + QStringLiteral("/") + sanitizeFileName(safe);
    m_queue->addDownload(url.toStdString(), target.toStdString());
    showWindow();
}

void MainWindow::onAdd() {
    AddDialog dlg(this, m_settings->defaultSegments(), m_settings->maxSpeedKBs(),
                  m_settings->defaultDirectory());
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_queue->addDownload(dlg.url().toStdString(), dlg.outputPath().toStdString(),
                         dlg.segments(), dlg.isScheduled() ? dlg.scheduledAt() : 0,
                         dlg.maxSpeedBps());
}

void MainWindow::onSettings() {
    SettingsDialog dlg(m_settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // Language changes take effect immediately (no restart).
    I18n::applyFromSetting(m_settings->language());
    retranslateUi();
    applySettingsToUi();
}

void MainWindow::onPause() {
    int id = selectedId();
    if (id > 0)
        m_queue->pauseDownload(id);
}

void MainWindow::onResume() {
    int id = selectedId();
    if (id > 0)
        m_queue->resumeDownload(id);
}

void MainWindow::onRemove() {
    int id = selectedId();
    if (id > 0)
        m_queue->removeDownload(id);
}

void MainWindow::clearCompleted() {
    std::vector<int> done;
    for (const auto& it : m_queue->items())
        if (it.state == DownloadState::Completed)
            done.push_back(it.id);
    for (int id : done)
        m_queue->removeDownload(id);
}

void MainWindow::onItemAdded(int id) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* name = new QTableWidgetItem();
    name->setData(Qt::UserRole, id);
    m_table->setItem(row, kColFile, name);
    m_table->setItem(row, kColSize, new QTableWidgetItem());
    m_table->setCellWidget(row, kColProgress, new QProgressBar(m_table));
    m_table->setItem(row, kColSpeed, new QTableWidgetItem());
    m_table->setItem(row, kColStatus, new QTableWidgetItem());
    m_table->selectRow(row);
    updateRow(id);
}

void MainWindow::onItemChanged(int id) {
    updateRow(id);
    for (const auto& it : m_queue->items()) {
        if (it.id == id && it.state == DownloadState::Completed) {
            if (m_tray)
                m_tray->showMessage(
                    QStringLiteral("Phoenix"),
                    I18n::t("Download complete:\n%1")
                        .arg(baseName(QString::fromStdString(it.outputPath))),
                    QSystemTrayIcon::Information, 4000);
            break;
        }
    }
}

void MainWindow::onItemRemoved(int id) {
    int row = rowForId(id);
    if (row >= 0)
        m_table->removeRow(row);
}

int MainWindow::rowForId(int id) const {
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* item = m_table->item(r, kColFile);
        if (item && item->data(Qt::UserRole).toInt() == id)
            return r;
    }
    return -1;
}

int MainWindow::selectedId() const {
    auto sel = m_table->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return -1;
    QTableWidgetItem* item = m_table->item(sel.first().row(), kColFile);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

void MainWindow::updateRow(int id) {
    int row = rowForId(id);
    if (row < 0)
        return;
    DownloadItem data;
    bool found = false;
    for (const auto& it : m_queue->items()) {
        if (it.id == id) {
            data = it;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    m_table->item(row, kColFile)
        ->setText(baseName(QString::fromStdString(data.outputPath)));
    if (data.totalBytes > 0) {
        const QString total = formatSize(data.totalBytes);
        if (data.state == DownloadState::Running ||
            data.state == DownloadState::Idle)
            m_table->item(row, kColSize)
                ->setText(formatSize(data.receivedBytes) + QStringLiteral(" / ") + total);
        else
            m_table->item(row, kColSize)->setText(total);
    } else {
        m_table->item(row, kColSize)->setText(QStringLiteral("?"));
    }

    auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, kColProgress));
    if (bar) {
        if (data.totalBytes > 0) {
            bar->setRange(0, 100);
            bar->setValue(static_cast<int>(data.receivedBytes * 100 / data.totalBytes));
            bar->setFormat(QStringLiteral("%p%"));
        } else {
            bar->setRange(0, 0); // unknown size: busy indicator
        }
    }

    m_table->item(row, kColSpeed)
        ->setText(data.speedBps > 0 ? formatSpeed(data.speedBps) : QString());
    m_table->item(row, kColStatus)
        ->setText(I18n::status(QString::fromStdString(data.statusText)));
}

void MainWindow::refreshStatus() {
    int running = 0, queued = 0, paused = 0, done = 0, failed = 0;
    for (const auto& it : m_queue->items()) {
        switch (it.state) {
        case DownloadState::Running: ++running; break;
        case DownloadState::Idle: ++queued; break;
        case DownloadState::Paused: ++paused; break;
        case DownloadState::Completed: ++done; break;
        case DownloadState::Failed: ++failed; break;
        case DownloadState::Cancelled: break;
        }
    }
    QString msg = I18n::t("Running: %1   Queued: %2   Completed: %3")
                      .arg(running).arg(queued).arg(done);
    if (paused)
        msg += I18n::t("   Paused: %1").arg(paused);
    if (failed)
        msg += I18n::t("   Failed: %1").arg(failed);
    statusBar()->showMessage(msg);
}

void MainWindow::restoreSession() {
    std::string path = SessionStore::defaultPath();
    if (path.empty() || !SessionStore::exists(path))
        return;
    for (const auto& it : SessionStore::load(path)) {
        if (it.outputPath.empty())
            continue;
        m_queue->addDownload(it.url, it.outputPath, it.segments,
                             it.scheduledAt > 0 ? it.scheduledAt : 0,
                             it.maxSpeedBps,
                             it.state == DownloadState::Paused);
    }
    SessionStore::remove(path);
}

void MainWindow::saveSession() {
    std::string path = SessionStore::defaultPath();
    if (path.empty())
        return;
    if (m_queue->items().empty()) {
        SessionStore::remove(path);
        return;
    }
    SessionStore::save(path, m_queue->items());
}

QString MainWindow::formatSize(qint64 bytes) {
    constexpr qint64 k = 1024;
    if (bytes < k)
        return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < k * k)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    if (bytes < k * k * k)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
    return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) +
           QStringLiteral(" GB");
}

QString MainWindow::formatSpeed(double bytesPerSec) {
    return formatSize(static_cast<qint64>(bytesPerSec)) + QStringLiteral("/s");
}

QString MainWindow::baseName(const QString& path) {
    return QFileInfo(path).fileName();
}
