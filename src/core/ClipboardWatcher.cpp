#include "core/ClipboardWatcher.h"

#include "core/UrlMatcher.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>

ClipboardWatcher::ClipboardWatcher(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ClipboardWatcher::onTick);
}

void ClipboardWatcher::setEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (m_enabled) {
        auto* cb = QGuiApplication::clipboard();
        if (cb)
            m_lastText = cb->text(); // don't react to text copied "before"
        m_timer->start();
    } else {
        m_timer->stop();
    }
}

void ClipboardWatcher::onTick() {
    auto* cb = QGuiApplication::clipboard();
    if (!cb)
        return;
    const QString text = cb->text();
    if (text.isEmpty() || text == m_lastText || text.size() > 4096)
        return;
    m_lastText = text;
    const std::string url = UrlMatcher::extractFirstUrl(text.toStdString());
    if (!url.empty())
        emit urlDetected(QString::fromStdString(url));
}