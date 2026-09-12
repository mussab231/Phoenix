#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class DownloadEngine;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onStart();
    void onCancel();
    void onBrowse();
    void onProgress(qint64 received, qint64 total);
    void onFinished(const QString& path);
    void onError(const QString& message);
    void onCancelled();

private:
    static QString formatSize(qint64 bytes);
    static QString formatSpeed(double bytesPerSec);

    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QSpinBox* m_segmentsBox = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QProgressBar* m_bar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_speedLabel = nullptr;

    DownloadEngine* m_engine = nullptr;
    QElapsedTimer m_speedTimer;
    qint64 m_lastBytes = 0;
};
