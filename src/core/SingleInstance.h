#pragma once

#include <QLocalServer>
#include <QObject>

#include <QString>

// Single-instance coordinator. The first Phoenix instance owns a named
// QLocalServer pipe ("Phoenix.Main"). Later instances (opened by the OS for a
// phoenix:// link, or by the temporary port indicator) call tryActivate(url):
// if another instance is running it forwards the URL through the pipe and this
// process can exit; otherwise this instance becomes the owner.
//
// The owner listens for commands and re-emits commandReceived(url) on its
// thread (queued), ready to connect into MainWindow.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    static bool isOwnerRunning();      // another instance already owns the pipe?
    bool tryActivate(const QString& url); // false if we are the owner ourselves
    bool becomeOwner();                // take over the pipe; false if busy

signals:
    void commandReceived(const QString& url);

private:
    void onNewConnection();

    QLocalServer m_server;
    QString m_pending;
};