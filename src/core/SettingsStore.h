#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QSettings;

// Persisted application preferences (QSettings: registry HKCU on Windows by
// default, or an explicit .ini file when constructed with a path — used by
// the headless self-test so it never touches the real registry).
//
// Values are clamped to the same ranges the widgets allow, so stored garbage
// can never produce an unusable queue.
class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(const QString& iniPath = QString(),
                           QObject* parent = nullptr);
    ~SettingsStore() override;

    QString defaultDirectory() const;
    void setDefaultDirectory(const QString& dir);

    int maxConcurrent() const;   // 1..5
    void setMaxConcurrent(int n);

    int defaultSegments() const; // 1..16
    void setDefaultSegments(int n);

    int maxSpeedKBs() const;     // 0 = unlimited
    void setMaxSpeedKBs(int kb);

    int autoAction() const;      // PowerControl::Action 0..3
    void setAutoAction(int action);

    bool watchClipboard() const;
    void setWatchClipboard(bool on);

    QByteArray mainGeometry() const;
    void setMainGeometry(const QByteArray& geometry);

    // Language: 0 = system (auto), 1 = English, 2 = Arabic.
    int language() const;
    void setLanguage(int lang);

    bool autoStart() const;     // launch Phoenix with Windows (HKCU\...\Run)
    void setAutoStart(bool on);

    bool startMinimized() const; // start hidden to tray
    void setStartMinimized(bool on);

private:
    std::unique_ptr<QSettings> m_s;
};