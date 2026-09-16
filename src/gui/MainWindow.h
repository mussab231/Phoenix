#pragma once

#include <QMainWindow>

#include <string>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTableWidget;
class QMenu;
class QAction;
class QTimer;
class ClipboardWatcher;
class DownloadQueue;
class SettingsStore;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    DownloadQueue* queue() const { return m_queue; }

    // True when the window should boot hidden into the tray instead of
    // showing (start-minimized preference). main() checks this before show().
    bool startsHidden() const;

    // Adds a url handed to us from outside (phoenix:// link, HTTP listener,
    // clipboard). fileName (optional) is used as the target filename.
    void acceptIncomingUrl(const QString& url, const QString& fileName = {});

public slots:
    void showWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onAdd();
    void onPause();
    void onResume();
    void onRemove();
    void onRetry();
    void onSettings();
    void onItemAdded(int id);
    void onItemChanged(int id);
    void onItemRemoved(int id);

private:
    int rowForId(int id) const;
    int selectedId() const;
    void updateRow(int id);
    void refreshStatus();
    void restoreSession();
    void saveSession();
    void setupTray();
    void applySettingsToUi();
    void clearCompleted();
    void retranslateUi();
    static QString formatSize(qint64 bytes);
    static QString formatSpeed(double bytesPerSec);
    static QString baseName(const QString& path);
    static QString sanitizeFileName(const QString& fileName);

    QTableWidget* m_table = nullptr;
    QSpinBox* m_maxBox = nullptr;
    QComboBox* m_doneBox = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_resumeBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_retryBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_maxLbl = nullptr;
    QLabel* m_doneLbl = nullptr;

    DownloadQueue* m_queue = nullptr;
    SettingsStore* m_settings = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QAction* m_watchClipAct = nullptr;
    QAction* m_showAct = nullptr;
    QAction* m_addAct = nullptr;
    QAction* m_settingsAct = nullptr;
    QAction* m_clearAct = nullptr;
    QAction* m_pauseAllAct = nullptr;
    QAction* m_resumeAllAct = nullptr;
    QAction* m_quitAct = nullptr;
    ClipboardWatcher* m_clipWatcher = nullptr;
    bool m_firstHide = true;
};
