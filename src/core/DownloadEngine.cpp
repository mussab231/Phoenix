#include "core/DownloadEngine.h"

#include "core/HttpClient.h"
#include "core/ResumeStore.h"

#include <QString>

DownloadEngine::DownloadEngine(QObject* parent) : QObject(parent) {}

DownloadEngine::~DownloadEngine() {
    m_stop = true;
    wait();
}

void DownloadEngine::start(const std::string& url, const std::string& outputPath,
                           int segments) {
    wait(); // previous run already finished (signals delivered)
    m_stop = false;
    if (segments < 1)
        segments = 1;
    if (segments > 16)
        segments = 16;
    m_segments = segments;
    DownloadTask task;
    task.url = url;
    task.outputPath = outputPath;
    task.state = DownloadState::Running;
    m_worker = std::thread(&DownloadEngine::run, this, std::move(task));
}

void DownloadEngine::cancel() {
    m_stop = true;
}

void DownloadEngine::wait() {
    if (m_worker.joinable())
        m_worker.join();
}

void DownloadEngine::run(DownloadTask task) {
    emit stateChanged(QStringLiteral("Connecting..."));
    try {
        task.totalBytes = HttpClient::getFileSize(task.url);
        emit progressChanged(0, static_cast<qint64>(task.totalBytes));

        auto onProgress = [this](std::int64_t received, std::int64_t total) {
            emit progressChanged(static_cast<qint64>(received),
                                 static_cast<qint64>(total));
        };

        if (m_segments > 1) {
            // Peek at saved state to report "Resuming" honestly.
            QString phase = QStringLiteral("Downloading (%1 connections)...").arg(m_segments);
            if (auto saved = ResumeStore::load(ResumeStore::statePathFor(task.outputPath))) {
                if (saved->url == task.url && saved->total == task.totalBytes &&
                    static_cast<int>(saved->segments.size()) == m_segments) {
                    std::int64_t done = 0;
                    for (const auto& s : saved->segments)
                        done += s.done;
                    if (done > 0 && task.totalBytes > 0)
                        phase = QStringLiteral("Resuming from %1%... (%2 connections)...")
                                    .arg(done * 100 / task.totalBytes)
                                    .arg(m_segments);
                }
            }
            emit stateChanged(phase);
            HttpClient::downloadSegmented(task.url, task.outputPath, onProgress,
                                          &m_stop, m_segments);
        } else {
            emit stateChanged(QStringLiteral("Downloading..."));
            HttpClient::download(task.url, task.outputPath, onProgress, &m_stop);
        }

        task.state = DownloadState::Completed;
        emit stateChanged(QStringLiteral("Completed"));
        emit finished(QString::fromStdString(task.outputPath));
    } catch (const std::exception& e) {
        std::string what = e.what();
        if (m_stop || what == "cancelled") {
            task.state = DownloadState::Cancelled;
            emit stateChanged(QStringLiteral("Paused - press Download to resume"));
            emit cancelled();
        } else {
            task.state = DownloadState::Failed;
            task.errorMessage = what;
            emit stateChanged(QStringLiteral("Failed"));
            emit errorOccurred(QString::fromStdString(what));
        }
    }
}
