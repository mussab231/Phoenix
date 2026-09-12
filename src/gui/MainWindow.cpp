#include "gui/MainWindow.h"

#include "core/DownloadEngine.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Phoenix 0.3"));
    resize(560, 250);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* urlRow = new QHBoxLayout();
    urlRow->addWidget(new QLabel(QStringLiteral("URL:"), central));
    m_urlEdit = new QLineEdit(central);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    urlRow->addWidget(m_urlEdit);
    layout->addLayout(urlRow);

    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(QStringLiteral("Save to:"), central));
    m_pathEdit = new QLineEdit(central);
    pathRow->addWidget(m_pathEdit);
    m_browseBtn = new QPushButton(QStringLiteral("Browse..."), central);
    pathRow->addWidget(m_browseBtn);
    layout->addLayout(pathRow);

    auto* segRow = new QHBoxLayout();
    segRow->addWidget(new QLabel(QStringLiteral("Connections:"), central));
    m_segmentsBox = new QSpinBox(central);
    m_segmentsBox->setRange(1, 16);
    m_segmentsBox->setValue(8);
    m_segmentsBox->setToolTip(QStringLiteral("Parallel connections (1 = classic single download)"));
    segRow->addWidget(m_segmentsBox);
    segRow->addStretch(1);
    layout->addLayout(segRow);

    auto* btnRow = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("Download"), central);
    m_cancelBtn = new QPushButton(QStringLiteral("Pause"), central);
    m_cancelBtn->setEnabled(false);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_cancelBtn);
    layout->addLayout(btnRow);

    m_bar = new QProgressBar(central);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    layout->addWidget(m_bar);

    m_statusLabel = new QLabel(QStringLiteral("Idle"), central);
    m_speedLabel = new QLabel(QString(), central);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_speedLabel);

    setCentralWidget(central);

    m_engine = new DownloadEngine(this);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(m_browseBtn, &QPushButton::clicked, this, &MainWindow::onBrowse);
    connect(m_engine, &DownloadEngine::progressChanged, this, &MainWindow::onProgress);
    connect(m_engine, &DownloadEngine::finished, this, &MainWindow::onFinished);
    connect(m_engine, &DownloadEngine::errorOccurred, this, &MainWindow::onError);
    connect(m_engine, &DownloadEngine::cancelled, this, &MainWindow::onCancelled);
    connect(m_engine, &DownloadEngine::stateChanged, m_statusLabel,
            &QLabel::setText);
}

void MainWindow::onStart() {
    QString url = m_urlEdit->text().trimmed();
    QString path = m_pathEdit->text().trimmed();
    if (url.isEmpty() || path.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Enter a URL and an output file first."));
        return;
    }
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    m_speedLabel->clear();
    m_lastBytes = 0;
    m_speedTimer.start();
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_engine->start(url.toStdString(), path.toStdString(), m_segmentsBox->value());
}

void MainWindow::onCancel() {
    m_cancelBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Pausing..."));
    m_engine->cancel();
}

void MainWindow::onBrowse() {
    QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save as"));
    if (!file.isEmpty())
        m_pathEdit->setText(file);
}

void MainWindow::onProgress(qint64 received, qint64 total) {
    if (total > 0)
        m_bar->setValue(static_cast<int>(received * 100 / total));
    else
        m_bar->setRange(0, 0); // unknown size: busy indicator

    qint64 elapsed = m_speedTimer.restart();
    if (elapsed > 0) {
        double speed = (received - m_lastBytes) * 1000.0 / elapsed;
        m_speedLabel->setText(formatSize(received) +
                              (total > 0 ? QStringLiteral(" / ") + formatSize(total)
                                         : QString()) +
                              QStringLiteral("  -  ") + formatSpeed(speed));
    }
    m_lastBytes = received;
}

void MainWindow::onFinished(const QString& path) {
    m_bar->setRange(0, 100);
    m_bar->setValue(100);
    m_statusLabel->setText(QStringLiteral("Saved to: ") + path);
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

void MainWindow::onError(const QString& message) {
    m_bar->setRange(0, 100);
    m_statusLabel->setText(QStringLiteral("Error: ") + message);
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

void MainWindow::onCancelled() {
    m_bar->setRange(0, 100);
    m_statusLabel->setText(QStringLiteral("Paused - press Download to resume"));
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
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
