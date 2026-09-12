#pragma once

#include "models/DownloadTask.h"

#include <QObject>

#include <atomic>
#include <string>
#include <thread>

// Runs one download on a worker thread, reports back via Qt signals.
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    explicit DownloadEngine(QObject* parent = nullptr);
    ~DownloadEngine() override;

    void start(const std::string& url, const std::string& outputPath,
               int segments = 8);
    void cancel();

signals:
    void progressChanged(qint64 received, qint64 total);
    void stateChanged(const QString& state);
    void finished(const QString& filePath);
    void errorOccurred(const QString& message);
    void cancelled();

private:
    void run(DownloadTask task);

    std::thread m_worker;
    std::atomic<bool> m_stop{false};
    int m_segments = 8;
};
