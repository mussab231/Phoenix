#pragma once

#include <QObject>
#include <QString>

#include <thread>

// Minimal HTTP/1.1 listener on 127.0.0.1 (loopback only). Purpose: a
// lightweight, browser-agnostic way to hand links to Phoenix without the
// phoenix:// protocol handler being registered:
//
//   GET  http://127.0.0.1:<port>/add?url=<percent-encoded>&name=<optional>
//        -> queues the link; body replies "OK"
//   GET  http://127.0.0.1:<port>/status
//        -> {"ok":true,"app":"Phoenix","port":<port>}
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

signals:
    void urlReceived(const QString& url, const QString& fileName);

private:
    void run();

    std::thread* m_thread = nullptr;
    void* m_listenSock = nullptr; // SOCKET (kept opaque to avoid winsock include)
    quint16 m_port = 0;
};