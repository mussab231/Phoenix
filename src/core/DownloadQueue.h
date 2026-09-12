#pragma once

#include "models/DownloadItem.h"
#include "core/DownloadEngine.h" // complete type: ActiveRunner owns an engine

#include <QElapsedTimer>
#include <QObject>

#include <map>
#include <memory>
#include <string>
#include <vector>

// Coordinates several DownloadEngines: up to N run at once, the rest wait
// in line. Each download keeps its own resume state, so pausing, closing,
// or crashing never loses progress.
class DownloadQueue : public QObject {
    Q_OBJECT
public:
    explicit DownloadQueue(QObject* parent = nullptr);

    int addDownload(const std::string& url, const std::string& outputPath,
                    int segments = 8);
    void pauseDownload(int id);
    void resumeDownload(int id);
    void removeDownload(int id);
    void setMaxConcurrent(int n);
    int maxConcurrent() const { return m_maxConcurrent; }
    std::vector<DownloadItem> items() const { return m_items; }

signals:
    void itemAdded(int id);
    void itemChanged(int id);
    void itemRemoved(int id);
    void queueFinished(); // nothing running or waiting anymore

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
};
