#include "gui/MainWindow.h"

#include "core/DownloadQueue.h"
#include "gui/AddDialog.h"
#include "models/DownloadItem.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int kColFile = 0;
constexpr int kColSize = 1;
constexpr int kColProgress = 2;
constexpr int kColSpeed = 3;
constexpr int kColStatus = 4;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Phoenix 0.4"));
    resize(760, 420);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* topRow = new QHBoxLayout();
    m_addBtn = new QPushButton(QStringLiteral("Add..."), central);
    m_pauseBtn = new QPushButton(QStringLiteral("Pause"), central);
    m_resumeBtn = new QPushButton(QStringLiteral("Resume"), central);
    m_removeBtn = new QPushButton(QStringLiteral("Remove"), central);
    topRow->addWidget(m_addBtn);
    topRow->addWidget(m_pauseBtn);
    topRow->addWidget(m_resumeBtn);
    topRow->addWidget(m_removeBtn);
    topRow->addStretch(1);
    topRow->addWidget(new QLabel(QStringLiteral("Max simultaneous:"), central));
    m_maxBox = new QSpinBox(central);
    m_maxBox->setRange(1, 5);
    m_maxBox->setValue(3);
    topRow->addWidget(m_maxBox);
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
    connect(m_addBtn, &QPushButton::clicked, this, &MainWindow::onAdd);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_resumeBtn, &QPushButton::clicked, this, &MainWindow::onResume);
    connect(m_removeBtn, &QPushButton::clicked, this, &MainWindow::onRemove);
    connect(m_maxBox, &QSpinBox::valueChanged, m_queue,
            &DownloadQueue::setMaxConcurrent);
    connect(m_queue, &DownloadQueue::itemAdded, this, &MainWindow::onItemAdded);
    connect(m_queue, &DownloadQueue::itemChanged, this, &MainWindow::onItemChanged);
    connect(m_queue, &DownloadQueue::itemRemoved, this, &MainWindow::onItemRemoved);
}

void MainWindow::onAdd() {
    AddDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_queue->addDownload(dlg.url().toStdString(), dlg.outputPath().toStdString(),
                         dlg.segments());
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
