#include "gui/MainWindow.h"

#include "core/ClipboardWatcher.h"
#include "core/DownloadQueue.h"
#include "core/SettingsStore.h"
#include "core/UrlMatcher.h"
#include "gui/AddDialog.h"
#include "gui/SettingsDialog.h"
#include "models/DownloadItem.h"

#include <QCloseEvent>
#include <QComboBox>
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
    m_addBtn = new QPushButton(QStringLiteral("Add"), central);
    m_addBtn->setProperty("accent", true);
    m_pauseBtn = new QPushButton(QStringLiteral("Pause"), central);
    m_resumeBtn = new QPushButton(QStringLiteral("Resume"), central);
    m_removeBtn = new QPushButton(QStringLiteral("Remove"), central);
    auto* settingsBtn = new QPushButton(QStringLiteral("Settings"), central);
    m_pauseBtn->setToolTip(QStringLiteral("Pause the selected download (resumable)"));
    m_resumeBtn->setToolTip(QStringLiteral("Resume the selected download"));
    m_removeBtn->setToolTip(QStringLiteral("Remove the selected download from the queue"));
    settingsBtn->setToolTip(QStringLiteral("Saved preferences: folder, connections, speed, power, clipboard"));
    topRow->addWidget(m_addBtn);
    topRow->addWidget(m_pauseBtn);
    topRow->addWidget(m_resumeBtn);
    topRow->addWidget(m_removeBtn);
    topRow->addWidget(settingsBtn);
    topRow->addStretch(1);
    topRow->addWidget(new QLabel(QStringLiteral("Max simultaneous:"), central));
    m_maxBox = new QSpinBox(central);
    m_maxBox->setRange(1, 5);
    m_maxBox->setValue(3);
    topRow->addWidget(m_maxBox);
    topRow->addSpacing(12);
    topRow->addWidget(new QLabel(QStringLiteral("When done:"), central));
    m_doneBox = new QComboBox(central);
    m_doneBox->addItems({QStringLiteral("Do nothing"), QStringLiteral("Sleep"),
                         QStringLiteral("Hibernate"), QStringLiteral("Shutdown")});
    topRow->addWidget(m_doneBox);
    layout->addLayout(topRow);

    m_table = new QTableWidget(0, 5, central);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("File"), QStringLiteral("Size"), QStringLiteral("Progress"),
         QStringLiteral("Speed"), QStringLiteral("Status")});
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
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);
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

    // Clipboard link catching (off until the user enables it from the tray).
    m_clipWatcher = new ClipboardWatcher(this);
    connect(m_clipWatcher, &ClipboardWatcher::urlDetected, this,
            [this](const QString& url) {
                acceptIncomingUrl(url);
                if (m_tray)
                    m_tray->showMessage(
                        QStringLiteral("Phoenix"),
                        QStringLiteral("Download added from clipboard:\n%1").arg(url),
                        QSystemTrayIcon::Information, 4000);
            });

    setupTray();
    applySettingsToUi();
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

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    QIcon icon = windowIcon();
    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip(QStringLiteral("Phoenix Download Manager"));

    m_trayMenu = new QMenu(this);
    QAction* showAct = m_trayMenu->addAction(QStringLiteral("Show / Hide"));
    connect(showAct, &QAction::triggered, this, &MainWindow::showWindow);
    m_trayMenu->addSeparator();

    QAction* addAct = m_trayMenu->addAction(QStringLiteral("Add Download..."));
    connect(addAct, &QAction::triggered, this, &MainWindow::onAdd);

    QAction* settingsAct = m_trayMenu->addAction(QStringLiteral("Settings..."));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::onSettings);

    QAction* pauseAll = m_trayMenu->addAction(QStringLiteral("Pause All"));
    connect(pauseAll, &QAction::triggered, this, [this] {
        for (auto it : m_queue->items())
            m_queue->pauseDownload(it.id);
    });

    QAction* resumeAll = m_trayMenu->addAction(QStringLiteral("Resume All"));
    connect(resumeAll, &QAction::triggered, this, [this] {
        for (auto it : m_queue->items())
            if (it.state == DownloadState::Paused)
                m_queue->resumeDownload(it.id);
    });

    QAction* watchClip = m_trayMenu->addAction(QStringLiteral("Watch clipboard for links"));
    watchClip->setCheckable(true);
    watchClip->setChecked(m_settings->watchClipboard());
    m_watchClipAct = watchClip;
    connect(watchClip, &QAction::toggled, this, [this](bool on) {
        if (m_settings)
            m_settings->setWatchClipboard(on);
        if (m_clipWatcher)
            m_clipWatcher->setEnabled(on);
    });

    m_trayMenu->addSeparator();
    QAction* quitAct = m_trayMenu->addAction(QStringLiteral("Quit"));
    connect(quitAct, &QAction::triggered, this, [this] {
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

void MainWindow::closeEvent(QCloseEvent* event) {
    // Closing hides to the tray instead of quitting (unless the user asked to
    // quit from the tray menu, which clears m_tray first).
    if (m_tray && QSystemTrayIcon::isSystemTrayAvailable()) {
        if (m_firstHide) {
            m_firstHide = false;
            m_tray->showMessage(
                QStringLiteral("Phoenix"),
                QStringLiteral("Still running in the tray. Right-click the "
                               "icon for options."),
                QSystemTrayIcon::Information, 4000);
        }
        hide();
        event->ignore();
        return;
    }
    if (m_settings)
        m_settings->setMainGeometry(saveGeometry());
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
    m_table->item(row, kColSize)
        ->setText(data.totalBytes > 0 ? formatSize(data.totalBytes) : QStringLiteral("?"));

    auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, kColProgress));
    if (bar) {
        if (data.totalBytes > 0) {
            bar->setRange(0, 100);
            bar->setValue(static_cast<int>(data.receivedBytes * 100 / data.totalBytes));
        } else {
            bar->setRange(0, 0); // unknown size: busy indicator
        }
    }

    m_table->item(row, kColSpeed)
        ->setText(data.speedBps > 0 ? formatSpeed(data.speedBps) : QString());
    m_table->item(row, kColStatus)
        ->setText(QString::fromStdString(data.statusText));
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
    QString msg = QStringLiteral("Running: %1   Queued: %2   Completed: %3")
                      .arg(running).arg(queued).arg(done);
    if (paused)
        msg += QStringLiteral("   Paused: %1").arg(paused);
    if (failed)
        msg += QStringLiteral("   Failed: %1").arg(failed);
    statusBar()->showMessage(msg);
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
