#include "core/DownloadQueue.h"

#include "core/DownloadEngine.h"

#include <QDateTime>

DownloadQueue::DownloadQueue(QObject* parent) : QObject(parent) {}

int DownloadQueue::addDownload(const std::string& url, const std::string& outputPath,
                               int segments) {
    DownloadItem item;
    item.id = m_nextId++;
    item.url = url;
    item.outputPath = outputPath;
    item.segments = segments < 1 ? 1 : (segments > 16 ? 16 : segments);
    item.state = DownloadState::Idle;
    item.statusText = "Queued";
    m_items.push_back(item);
    m_finishNotified = false;
    emit itemAdded(item.id);
    pump();
    return item.id;
}

void DownloadQueue::pauseDownload(int id) {
    DownloadItem* it = find(id);
    if (!it)
        return;
    auto rit = m_runners.find(id);
    if (rit != m_runners.end()) {
        it->statusText = "Pausing...";
        emit itemChanged(id);
        rit->second.engine->cancel(); // onItemCancelled finalizes async
    } else if (it->state == DownloadState::Idle) {
        it->state = DownloadState::Paused;
        it->statusText = "Paused";
        emit itemChanged(id);
        checkFinished();
    }
}

void DownloadQueue::resumeDownload(int id) {
    DownloadItem* it = find(id);
    if (!it || it->state != DownloadState::Paused)
        return;
    it->state = DownloadState::Idle;
    it->statusText = "Queued";
    it->errorMessage.clear();
    m_finishNotified = false;
    emit itemChanged(id);
    pump();
}

void DownloadQueue::removeDownload(int id) {
    auto rit = m_runners.find(id);
    if (rit != m_runners.end()) {
        rit->second.engine->cancel();
        rit->second.engine->wait(); // join; queued slot (if any) is guarded
        m_runners.erase(id);
    }
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        if (it->id == id) {
            m_items.erase(it);
            break;
        }
    }
    emit itemRemoved(id);
    pump();
    checkFinished();
}

void DownloadQueue::setMaxConcurrent(int n) {
    if (n < 1)
        n = 1;
    if (n > 5)
        n = 5;
    m_maxConcurrent = n;
    pump();
}

void DownloadQueue::onItemProgress(int id, qint64 received, qint64 total) {
    DownloadItem* it = find(id);
    if (!it)
        return;
    it->receivedBytes = received;
    it->totalBytes = total;
    auto rit = m_runners.find(id);
    if (rit == m_runners.end())
        return;
    qint64 ms = rit->second.speedTimer.elapsed();
    if (ms >= 500) {
        double instant = (received - rit->second.lastBytes) * 1000.0 / ms;
        it->speedBps = (it->speedBps <= 0.0) ? instant : it->speedBps * 0.4 + instant * 0.6;
        rit->second.lastBytes = received;
        rit->second.speedTimer.restart();
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - rit->second.lastEmitMs >= 150) {
        rit->second.lastEmitMs = now;
        emit itemChanged(id);
    }
}

void DownloadQueue::onItemState(int id, const QString& state) {
    DownloadItem* it = find(id);
    if (!it)
        return;
    it->statusText = state.toStdString();
    emit itemChanged(id);
}

void DownloadQueue::onItemFinished(int id, const QString&) {
    m_runners.erase(id);
    DownloadItem* it = find(id);
    if (!it)
        return;
    it->state = DownloadState::Completed;
    it->speedBps = 0.0;
    if (it->totalBytes > 0)
        it->receivedBytes = it->totalBytes;
    it->statusText = "Completed";
    emit itemChanged(id);
    pump();
}

void DownloadQueue::onItemError(int id, const QString& message) {
    m_runners.erase(id);
    DownloadItem* it = find(id);
    if (!it)
        return;
    it->state = DownloadState::Failed;
    it->speedBps = 0.0;
    it->errorMessage = message.toStdString();
    it->statusText = "Failed: " + it->errorMessage;
    emit itemChanged(id);
    pump();
}

void DownloadQueue::onItemCancelled(int id) {
    m_runners.erase(id);
    DownloadItem* it = find(id);
    if (!it)
        return;
    if (it->state == DownloadState::Running) {
        it->state = DownloadState::Paused;
        it->speedBps = 0.0;
        it->statusText = "Paused - resume anytime";
    }
    emit itemChanged(id);
    pump(); // a free slot may unblock a waiting item
}

void DownloadQueue::pump() {
    while (static_cast<int>(m_runners.size()) < m_maxConcurrent) {
        DownloadItem* next = nullptr;
        for (auto& item : m_items) {
            if (item.state == DownloadState::Idle && !m_runners.count(item.id)) {
                next = &item;
                break;
            }
        }
        if (!next)
            break;
        int id = next->id;
        auto engine = std::make_unique<DownloadEngine>(this);
        connect(engine.get(), &DownloadEngine::progressChanged, this,
                [this, id](qint64 r, qint64 t) { onItemProgress(id, r, t); });
        connect(engine.get(), &DownloadEngine::stateChanged, this,
                [this, id](const QString& s) { onItemState(id, s); });
        connect(engine.get(), &DownloadEngine::finished, this,
                [this, id](const QString& p) { onItemFinished(id, p); });
        connect(engine.get(), &DownloadEngine::errorOccurred, this,
                [this, id](const QString& m) { onItemError(id, m); });
        connect(engine.get(), &DownloadEngine::cancelled, this,
                [this, id]() { onItemCancelled(id); });
        next->state = DownloadState::Running;
        ActiveRunner runner;
        runner.engine = std::move(engine);
        runner.speedTimer.start();
        m_runners.emplace(id, std::move(runner));
        emit itemChanged(id);
        m_runners[id].engine->start(next->url, next->outputPath, next->segments);
    }
    checkFinished();
}

void DownloadQueue::checkFinished() {
    if (m_finishNotified)
        return;
    for (const auto& item : m_items) {
        if (item.state == DownloadState::Running || item.state == DownloadState::Idle)
            return;
    }
    m_finishNotified = true;
    emit queueFinished();
}

DownloadItem* DownloadQueue::find(int id) {
    for (auto& item : m_items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}
