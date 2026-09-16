#pragma once

#include "models/DownloadItem.h"
#include "core/DownloadEngine.h" // complete type: ActiveRunner owns an engine
#include "core/PowerControl.h"

#include <QElapsedTimer>
#include <QObject>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QTimer;

// Coordinates several DownloadEngines: up to N run at once, the rest wait
// in line. Each download keeps its own resume state, so pausing, closing,
// or crashing never loses progress. Items can also be scheduled to start at
// a fixed time, and an optional power action fires once the whole queue is
// done (sleep / hibernate / shutdown).
class DownloadQueue : public QObject {
    Q_OBJECT
public:
    explicit DownloadQueue(QObject* parent = nullptr);

    // startAtMs = epoch milliseconds; 0 (default) starts right away.
    // maxSpeedBps = per-download speed cap; 0 = unlimited.
    // expectedSha256 = optional hex SHA-256 verified once the file is done
    // (empty skips verification).
    // startPaused = true restores an item in the Paused state instead of
    // letting the queue start it (used when reloading a saved session).
    int addDownload(const std::string& url, const std::string& outputPath,
                    int segments = 8, std::int64_t startAtMs = 0,
                    double maxSpeedBps = 0.0, bool startPaused = false,
                    const std::string& expectedSha256 = std::string());
    void pauseDownload(int id);
    void resumeDownload(int id);
    void removeDownload(int id);
    // Retry a Failed item: clears the error and re-queues it.
    void retryDownload(int id);
    void setMaxConcurrent(int n);
    int maxConcurrent() const { return m_maxConcurrent; }
    void setAutoAction(int action); // PowerControl::Action
    std::vector<DownloadItem> items() const { return m_items; }

signals:
    void itemAdded(int id);
    void itemChanged(int id);
    void itemRemoved(int id);
    void queueFinished(); // nothing running or waiting anymore
    void autoActionTriggered(int action); // helper for tests / UI logging

private slots:
    void onTick(); // 1 Hz pump: starts scheduled items whose time arrived

private:
    struct ActiveRunner {
        std::unique_ptr<DownloadEngine> engine;
        QElapsedTimer speedTimer;
        std::int64_t lastBytes = 0;
        qint64 lastEmitMs = 0;
    };

    void onItemProgress(int id, qint64 received, qint64 total);
    void onItemState(int id, const QString& state);
    void onItemFinished(int id, const QString& path);
    void onItemError(int id, const QString& message);
    void onItemCancelled(int id);

    void pump(); // start waiting items while slots are free
    void checkFinished();
    DownloadItem* find(int id);

    std::vector<DownloadItem> m_items;
    std::map<int, ActiveRunner> m_runners;
    int m_nextId = 1;
    int m_maxConcurrent = 3;
    bool m_finishNotified = false;
    QTimer* m_pumpTimer = nullptr;
    int m_autoAction = static_cast<int>(PowerControl::Action::None);
    bool m_autoActionDone = false;
};
