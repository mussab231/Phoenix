#pragma once

#include "models/DownloadTask.h"

#include <QObject>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class RateLimiter;

// Runs one download on a worker thread, reports back via Qt signals.
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    explicit DownloadEngine(QObject* parent = nullptr);
    ~DownloadEngine() override;

    void start(const std::string& url, const std::string& outputPath,
               int segments = 8);
    void setSpeedLimit(double bytesPerSecond); // 0 = unlimited
    void cancel();
    void wait(); // blocks until the worker thread ends (also done by dtor)

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
    std::unique_ptr<RateLimiter> m_limiter; // set on the GUI thread before start()
};
