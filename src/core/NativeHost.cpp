#include "core/NativeHost.h"

#include "core/DownloadQueue.h"
#include "models/DownloadItem.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

#include <cstdint>
#include <cstring>

namespace {

bool readExact(std::FILE* in, void* buffer, size_t n) {
    auto* p = static_cast<char*>(buffer);
    size_t got = 0;
    while (got < n) {
        const size_t r = std::fread(p + got, 1, n - got, in);
        if (r == 0)
            return false; // EOF or error before the full frame arrived
        got += r;
    }
    return true;
}

std::uint32_t leToU32(const unsigned char* b) {
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

void u32ToLe(std::uint32_t v, unsigned char* b) {
    b[0] = static_cast<unsigned char>(v & 0xff);
    b[1] = static_cast<unsigned char>((v >> 8) & 0xff);
    b[2] = static_cast<unsigned char>((v >> 16) & 0xff);
    b[3] = static_cast<unsigned char>((v >> 24) & 0xff);
}

// Matches MainWindow::sanitizeFileName: strip control chars and the Windows
// path-forbidden set, never return an empty name.
QString sanitizeFileName(const QString& fileName) {
    QString cleaned;
    cleaned.reserve(fileName.size());
    for (QChar ch : fileName) {
        const uchar c = ch.unicode();
        if (c < 0x20 || std::strchr("<>:\"/\\|?*", static_cast<char>(c & 0x7f)))
            continue;
        cleaned += ch;
    }
    return cleaned.isEmpty() ? QStringLiteral("download.bin") : cleaned;
}

QString targetPathFor(const QUrl& url, const QString& fileName,
                      const QString& defaultDir) {
    QString safe = fileName;
    if (safe.isEmpty()) {
        const QString path = url.path();
        const int slash = path.lastIndexOf('/');
        safe = slash >= 0 ? path.mid(slash + 1) : path;
    }
    safe = sanitizeFileName(safe);
    QString dir = defaultDir;
    if (dir.isEmpty())
        dir = QStringLiteral(".");
    return dir + QStringLiteral("/") + safe;
}

QString downloadStateText(DownloadState state) {
    switch (state) {
    case DownloadState::Idle:
        return QStringLiteral("Queued");
    case DownloadState::Running:
        return QStringLiteral("Running");
    case DownloadState::Paused:
        return QStringLiteral("Paused");
    case DownloadState::Completed:
        return QStringLiteral("Completed");
    case DownloadState::Failed:
        return QStringLiteral("Failed");
    case DownloadState::Cancelled:
        return QStringLiteral("Cancelled");
    }
    return QStringLiteral("Unknown");
}

} // namespace

NativeHost::NativeHost(QObject* parent) : QObject(parent) {}

NativeHost::~NativeHost() {
    if (m_thread) {
        // The reader thread may still be blocked on stdin; the owning headless
        // process is about to exit, so detach instead of blocking forever.
        m_thread->detach();
        delete m_thread;
        m_thread = nullptr;
    }
}

bool NativeHost::writeFrame(std::FILE* out, const std::string& json) {
    if (!out || json.size() > UINT32_MAX)
        return false;
    const std::uint32_t n = static_cast<std::uint32_t>(json.size());
    unsigned char prefix[4];
    u32ToLe(n, prefix);
    if (std::fwrite(prefix, 1, sizeof(prefix), out) != sizeof(prefix))
        return false;
    if (std::fwrite(json.data(), 1, json.size(), out) != json.size())
        return false;
    std::fflush(out);
    return true;
}

bool NativeHost::readFrame(std::FILE* in, std::string* json) {
    if (!in)
        return false;
    unsigned char prefix[4]{};
    if (!readExact(in, prefix, sizeof(prefix)))
        return false; // clean EOF -> connection closed, not a protocol error
    const std::uint32_t n = leToU32(prefix);
    // Native-messaging frames are small JSON objects; cap the payload so a
    // corrupt length cannot make us allocate nonsense.
    if (n == 0 || n > (1u << 26))
        return false;
    json->resize(n);
    if (!readExact(in, &(*json)[0], n))
        return false;
    return true;
}

void NativeHost::start() {
    if (m_thread)
        return;
    m_thread = new std::thread(&NativeHost::run, this);
}

void NativeHost::run() {
    std::string json;
    while (readFrame(stdin, &json)) {
        QJsonParseError err;
        const QJsonDocument doc =
            QJsonDocument::fromJson(QByteArray::fromStdString(json), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue; // ignore a malformed frame, keep the channel alive
        emit requestReceived(doc.object());
    }
    emit connectionClosed();
}

bool NativeHost::reply(const QJsonObject& obj) {
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    std::lock_guard<std::mutex> lock(m_writeMutex);
    return writeFrame(stdout, std::string(data.constData(), data.size()));
}

QJsonObject NativeHost::handleRequest(const QJsonObject& request,
                                      DownloadQueue* queue,
                                      const QString& defaultDir) {
    const QString type = request.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("ping"))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("pong"), true}};

    if (type == QStringLiteral("add")) {
        const QString url = request.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("empty url")}};
        if (!queue)
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("no active download queue")}};
        const QString fileName =
            request.value(QStringLiteral("fileName")).toString();
        const QString target =
            targetPathFor(QUrl(url), fileName, defaultDir);
        const int id = queue->addDownload(url.toStdString(), target.toStdString());
        return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id}};
    }

    if (type == QStringLiteral("status")) {
        if (!queue)
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("no active download queue")}};
        QJsonArray items;
        for (const DownloadItem& it : queue->items()) {
            items.append(QJsonObject{
                {QStringLiteral("id"), it.id},
                {QStringLiteral("file"), QFileInfo(
                     QString::fromStdString(it.outputPath)).fileName()},
                {QStringLiteral("url"), QString::fromStdString(it.url)},
                {QStringLiteral("receivedBytes"), it.receivedBytes},
                {QStringLiteral("totalBytes"), it.totalBytes},
                {QStringLiteral("speedBps"), it.speedBps},
                {QStringLiteral("state"), downloadStateText(it.state)},
                {QStringLiteral("status"), QString::fromStdString(it.statusText)},
            });
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("items"), items}};
    }

    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("unknown message")}};
}