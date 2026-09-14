#include "core/SingleInstance.h"

#include <QLocalSocket>

namespace {
const QString kPipeName = QStringLiteral("Phoenix.Main");
}

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {
    connect(&m_server, &QLocalServer::newConnection, this,
            &SingleInstance::onNewConnection);
}

SingleInstance::~SingleInstance() {
    if (m_server.isListening())
        m_server.close();
}

bool SingleInstance::isOwnerRunning() {
    QLocalSocket probe;
    probe.connectToServer(kPipeName, QIODevice::ReadWrite);
    bool ok = probe.waitForConnected(300);
    if (ok)
        probe.disconnectFromServer();
    return ok;
}

bool SingleInstance::tryActivate(const QString& url) {
    if (isOwnerRunning()) {
        QLocalSocket sock;
        sock.connectToServer(kPipeName, QIODevice::WriteOnly);
        if (sock.waitForConnected(300)) {
            sock.write(url.toUtf8());
            sock.flush();
            sock.waitForBytesWritten(300);
            sock.disconnectFromServer();
            return true;
        }
    }
    return false;
}

bool SingleInstance::becomeOwner() {
    QLocalServer::removeServer(kPipeName); // stale pipe after a crash
    if (!m_server.listen(kPipeName))
        return false;
    return true;
}

void SingleInstance::onNewConnection() {
    while (QLocalSocket* sock = m_server.nextPendingConnection()) {
        connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
            const QByteArray data = sock->readAll();
            sock->disconnectFromServer();
            sock->deleteLater();
            if (!data.isEmpty())
                emit commandReceived(QString::fromUtf8(data));
        });
    }
}