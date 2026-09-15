#pragma once

#include <QJsonObject>
#include <QObject>

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

class DownloadQueue;

// Native Messaging Host: hands download links from a browser extension to
// Phoenix (replaces clipboard scraping for the browser case).
//
// The browser launches "Phoenix.exe --native-messaging" and talks to it over
// stdin/stdout using the standard native-messaging framing: a 4-byte
// little-endian unsigned length followed by that many bytes of UTF-8 JSON
// (one object per frame).
//
// Frames (browser -> host):
//   {"type":"add","url":"https://...","fileName":"optional.pdf"}
//   {"type":"status"}
//   {"type":"ping"}
//
// Replies (host -> browser):
//   {"ok":true,"id":N} | {"ok":false,"error":"..."}   (add)
//   {"ok":true,"items":[{id,file,receivedBytes,totalBytes,speedBps,status}]}
//   {"ok":true,"pong":true}                            (status, ping)
//
// A reader thread decodes stdin and emits requestReceived on the owning
// thread (queued connection, so the receiver must run an event loop). Reply
// bytes go to stdout from whichever thread calls reply(); a mutex serializes
// writers. stdin (read) and stdout (write) are independent handles, so the
// reader thread and the responder never contend for the same handle.
class NativeHost : public QObject {
    Q_OBJECT
public:
    explicit NativeHost(QObject* parent = nullptr);
    ~NativeHost() override;

    // Spawns the stdin reader thread. Call after connecting signals.
    void start();

    // Writes one framed JSON reply to stdout. Thread-safe.
    bool reply(const QJsonObject& obj);

    // -- standalone frame protocol (also used by the self-test) --
    static bool writeFrame(std::FILE* out, const std::string& json);
    static bool readFrame(std::FILE* in, std::string* json);

    // Request dispatcher shared by the live host and the self-test. queue may
    // be null (only "ping"/unknown work then). defaultDir is where "add"
    // places the file when no explicit path is given. Returns a JSON object.
    static QJsonObject handleRequest(const QJsonObject& request,
                                     DownloadQueue* queue,
                                     const QString& defaultDir);

signals:
    void requestReceived(const QJsonObject& request);
    void connectionClosed(); // stdin hit EOF or an unrecoverable decode error

private:
    void run();

    std::thread* m_thread = nullptr;
    std::mutex m_writeMutex;
};