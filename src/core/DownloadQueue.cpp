#include "core/DownloadQueue.h"

#include "core/Checksum.h"
#include "core/DownloadEngine.h"
#include "core/Uuid.h"

#include <QDateTime>
#include <QTimer>

DownloadQueue::DownloadQueue(QObject* parent) : QObject(parent) {
    m_pumpTimer = new QTimer(this);
    m_pumpTimer->setInterval(1000);
    connect(m_pumpTimer, &QTimer::timeout, this, &DownloadQueue::onTick);
    m_pumpTimer->start();
}

int DownloadQueue::addDownload(const std::string& url, const std::string& outputPath,
                               int segments, std::int64_t startAtMs,
                               double maxSpeedBps, bool startPaused,
                               const std::string& expectedSha256) {
    DownloadItem item;
    item.id = m_nextId++;
    item.uuid = Uuid::create();
    item.url = url;
    item.outputPath = outputPath;
    item.segments = segments < 1 ? 1 : (segments > 16 ? 16 : segments);
    item.maxSpeedBps = maxSpeedBps < 0.0 ? 0.0 : maxSpeedBps;
    item.expectedSha256 = expectedSha256;
    if (startAtMs > 0)
        item.scheduledAt = startAtMs;
    item.state = startPaused ? DownloadState::Paused : DownloadState::Idle;
    item.statusText = startPaused ? "Paused" : "Queued";
    m_items.push_back(item);
    m_finishNotified = false;
    m_autoActionDone = false;
    emit itemAdded(item.id);
    if (!startPaused)
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

void DownloadQueue::retryDownload(int id) {
    // A Failed download re-enters the queue the same way a paused one resumes:
    // the resume machinery reuses whatever bytes are still valid on disk and
    // re-fetches only the rest.
    DownloadItem* it = find(id);
    if (!it || it->state != DownloadState::Failed)
        return;
    it->state = DownloadState::Paused;
    it->errorMessage.clear();
    resumeDownload(id);
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

void DownloadQueue::setAutoAction(int action) {
    if (action < static_cast<int>(PowerControl::Action::None))
        action = static_cast<int>(PowerControl::Action::None);
    if (action > static_cast<int>(PowerControl::Action::Shutdown))
        action = static_cast<int>(PowerControl::Action::Shutdown);
    m_autoAction = action;
}

void DownloadQueue::onTick() {
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    for (auto& item : m_items) {
        if (item.state == DownloadState::Idle && item.scheduledAt > 0 &&
            !m_runners.count(item.id) && item.scheduledAt > now) {
            std::string text = "Scheduled " +
                QDateTime::fromMSecsSinceEpoch(item.scheduledAt)
                    .toString(QStringLiteral("HH:mm"))
                    .toStdString();
            if (item.statusText != text) {
                item.statusText = text;
                emit itemChanged(item.id);
            }
        }
    }
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

    // When the user supplied an expected hash, verify the finished file now.
    // A mismatch does not delete anything (the user may want to inspect it),
    // it just flags the row so a corrupt download is never mistaken for a
    // good one.
    if (!it->expectedSha256.empty()) {
        const std::string got = Checksum::sha256File(it->outputPath);
        if (got.empty()) {
            it->statusText = "Completed (checksum unreadable)";
            emit itemChanged(id);
        } else if (!Checksum::hexEquals(got, it->expectedSha256)) {
            it->statusText = "Checksum FAILED";
            it->errorMessage = "SHA-256 mismatch: expected " + it->expectedSha256 +
                               ", got " + got;
            emit itemChanged(id);
        } else {
            it->statusText = "Checksum OK";
            emit itemChanged(id);
        }
    }

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
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    while (static_cast<int>(m_runners.size()) < m_maxConcurrent) {
        DownloadItem* next = nullptr;
        for (auto& item : m_items) {
            if (item.state == DownloadState::Idle && !m_runners.count(item.id)) {
                if (item.scheduledAt > 0 && item.scheduledAt > now)
                    continue; // whose time has not come yet
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
        m_runners[id].engine->setSpeedLimit(next->maxSpeedBps);
        m_runners[id].engine->start(next->url, next->outputPath, next->segments);
    }
    checkFinished();
}

void DownloadQueue::checkFinished() {
    if (m_finishNotified)
        return;
    for (const auto& item : m_items) {
        // Paused items are user-initiated stops: the queue is not done yet.
        if (item.state == DownloadState::Running || item.state == DownloadState::Idle ||
            item.state == DownloadState::Paused)
            return;
    }
    m_finishNotified = true;
    emit queueFinished();
    if (m_autoAction != static_cast<int>(PowerControl::Action::None) &&
        !m_autoActionDone) {
        m_autoActionDone = true;
        emit autoActionTriggered(m_autoAction);
        PowerControl::perform(static_cast<PowerControl::Action>(m_autoAction));
    }
}

DownloadItem* DownloadQueue::find(int id) {
    for (auto& item : m_items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}
