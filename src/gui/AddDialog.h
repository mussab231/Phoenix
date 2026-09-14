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

private slots:
    void onBrowse();

private:
    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QSpinBox* m_segmentsBox = nullptr;
    QSpinBox* m_speedBox = nullptr;
    QCheckBox* m_scheduleCheck = nullptr;
    QDateTimeEdit* m_scheduleEdit = nullptr;
    QString m_defaultDir;
};
