#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class SettingsStore;

// Preferences editor. Reads the current values from SettingsStore, and writes
// them back on Accept. The caller (MainWindow) applies the live effects after
// the dialog closes.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsStore* settings, QWidget* parent = nullptr);

private slots:
    void onBrowse();
    void onAccept();

private:
    SettingsStore* m_settings = nullptr;
    QLineEdit* m_dirEdit = nullptr;
    QSpinBox* m_concurrentBox = nullptr;
    QSpinBox* m_segmentsBox = nullptr;
    QSpinBox* m_speedBox = nullptr;
    QComboBox* m_doneBox = nullptr;
    QCheckBox* m_watchClip = nullptr;
};