#include "core/SettingsStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

SettingsStore::SettingsStore(const QString& iniPath, QObject* parent)
    : QObject(parent) {
    if (iniPath.isEmpty())
        m_s = std::make_unique<QSettings>();
    else
        m_s = std::make_unique<QSettings>(iniPath, QSettings::IniFormat);
}

SettingsStore::~SettingsStore() = default;

QString SettingsStore::defaultDirectory() const {
    QString dir = m_s->value(QStringLiteral("downloads/defaultDir")).toString();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    return dir;
}

void SettingsStore::setDefaultDirectory(const QString& dir) {
    m_s->setValue(QStringLiteral("downloads/defaultDir"), dir);
}

int SettingsStore::maxConcurrent() const {
    return qBound(1, m_s->value(QStringLiteral("queue/maxConcurrent"), 3).toInt(), 5);
}

void SettingsStore::setMaxConcurrent(int n) {
    m_s->setValue(QStringLiteral("queue/maxConcurrent"), qBound(1, n, 5));
}

int SettingsStore::defaultSegments() const {
    return qBound(1, m_s->value(QStringLiteral("queue/defaultSegments"), 8).toInt(), 16);
}

void SettingsStore::setDefaultSegments(int n) {
    m_s->setValue(QStringLiteral("queue/defaultSegments"), qBound(1, n, 16));
}

int SettingsStore::maxSpeedKBs() const {
    return qBound(0, m_s->value(QStringLiteral("queue/maxSpeedKBs"), 0).toInt(), 100000);
}

void SettingsStore::setMaxSpeedKBs(int kb) {
    m_s->setValue(QStringLiteral("queue/maxSpeedKBs"), qBound(0, kb, 100000));
}

int SettingsStore::autoAction() const {
    return qBound(0, m_s->value(QStringLiteral("queue/autoAction"), 0).toInt(), 3);
}

void SettingsStore::setAutoAction(int action) {
    m_s->setValue(QStringLiteral("queue/autoAction"), qBound(0, action, 3));
}

bool SettingsStore::watchClipboard() const {
    return m_s->value(QStringLiteral("clipboard/watch"), false).toBool();
}

void SettingsStore::setWatchClipboard(bool on) {
    m_s->setValue(QStringLiteral("clipboard/watch"), on);
}

QByteArray SettingsStore::mainGeometry() const {
    return m_s->value(QStringLiteral("window/geometry")).toByteArray();
}

void SettingsStore::setMainGeometry(const QByteArray& geometry) {
    m_s->setValue(QStringLiteral("window/geometry"), geometry);
}

int SettingsStore::language() const {
    return qBound(0, m_s->value(QStringLiteral("ui/language"), 0).toInt(), 2);
}

void SettingsStore::setLanguage(int lang) {
    m_s->setValue(QStringLiteral("ui/language"), qBound(0, lang, 2));
}

bool SettingsStore::autoStart() const {
    return m_s->value(QStringLiteral("ui/autoStart"), false).toBool();
}

void SettingsStore::setAutoStart(bool on) {
    m_s->setValue(QStringLiteral("ui/autoStart"), on);
    // Mirrors the choice into the Windows "Run" key so it survives a reboot.
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\"
                                 "Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (on)
        run.setValue(QStringLiteral("Phoenix"), QStringLiteral("\"%1\"").arg(QCoreApplication::applicationFilePath()));
    else
        run.remove(QStringLiteral("Phoenix"));
}

bool SettingsStore::startMinimized() const {
    return m_s->value(QStringLiteral("ui/startMinimized"), false).toBool();
}

void SettingsStore::setStartMinimized(bool on) {
    m_s->setValue(QStringLiteral("ui/startMinimized"), on);
}