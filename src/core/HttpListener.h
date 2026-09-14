#pragma once

#include <QObject>
#include <QString>

#include <thread>

// Minimal HTTP/1.1 listener on 127.0.0.1 (loopback only). Purpose: a
// lightweight, browser-agnostic way to hand links to Phoenix without the
// phoenix:// protocol handler being registered:
//
//   GET  http://127.0.0.1:<port>/add?token=<one-time>&url=<percent-encoded>&name=<optional>
//        -> queues the link; body replies "OK"  (401 without the right token)
//   GET  http://127.0.0.1:<port>/status?token=<one-time>
//        -> {"ok":true,"app":"Phoenix","port":<port>,"token":"..."}
//
// A random per-launch token is generated when the listener is constructed and
// must be supplied on every request, so no other local process (or a web page
// reaching loopback) can queue links behind the app's back. The token is
// available through token() to the app's own integrations (UI hint, native
// messaging host, tests).
//
// urlReceived is emitted on the owning (Qt main) thread via a queued signal.
// Only the loopback address is bound; nothing is exposed to the network.
class HttpListener : public QObject {
    Q_OBJECT
public:
    explicit HttpListener(QObject* parent = nullptr);
    ~HttpListener() override;

    // Tries <preferredPort> first, then walks upward. Returns the bound port,
    // or 0 if no port could be bound.
    quint16 start(quint16 preferredPort = 0);
    void stop();
    quint16 port() const { return m_port; }
    QString token() const { return m_token; }

signals:
    void urlReceived(const QString& url, const QString& fileName);

private:
    void run();

    std::thread* m_thread = nullptr;
    void* m_listenSock = nullptr; // SOCKET (kept opaque to avoid winsock include)
    quint16 m_port = 0;
    QString m_token;
};