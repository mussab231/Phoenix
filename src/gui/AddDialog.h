#pragma once

#include <QDateTime>
#include <QDialog>

class QCheckBox;
class QDateTimeEdit;
class QLineEdit;
class QSpinBox;

// New-download dialog. Initial defaults (connections, max speed in KB/s, start
// folder for the file browser) come from the app settings, passed in.
class AddDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddDialog(QWidget* parent = nullptr,
                       int defaultSegments = 8,
                       int defaultMaxSpeedKBs = 0,
                       const QString& defaultDir = QString());

    QString url() const;
    QString outputPath() const;
    int segments() const;
    double maxSpeedBps() const;
    bool isScheduled() const;
    qint64 scheduledAt() const;
    // Hex SHA-256 the finished file must match; empty when left blank.
    QString expectedSha256() const;

private slots:
    void onBrowse();
    // Re-derives the output name when the URL changes and the current name is
    // still the placeholder (nothing for the user to overwrite).
    void onUrlChanged();

private:
    void probeFilename();
    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_hashEdit = nullptr;
    QSpinBox* m_segmentsBox = nullptr;
    QSpinBox* m_speedBox = nullptr;
    QCheckBox* m_scheduleCheck = nullptr;
    QDateTimeEdit* m_scheduleEdit = nullptr;
    QString m_defaultDir;
    // True once the user typed the output path by hand: probeFilename() must
    // not overwrite a deliberate choice.
    bool m_userEditedPath = false;
    // True when the current name came from a probe: a later URL change may
    // still refine it.
    bool m_nameFromProbe = false;
};
