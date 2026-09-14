#pragma once

#include <QObject>
#include <QString>

class QTimer;

// Polls the clipboard (1 Hz) for download-looking URLs. When a new one appears
// (and differs from the last seen text), urlDetected is emitted. Lightweight
// and independent of any specific web browser.
class ClipboardWatcher : public QObject {
    Q_OBJECT
public:
    explicit ClipboardWatcher(QObject* parent = nullptr);

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }
    QString lastSeen() const { return m_lastText; }

signals:
    void urlDetected(const QString& url);

private:
    void onTick();

    QTimer* m_timer = nullptr;
    bool m_enabled = false;
    QString m_lastText;
};