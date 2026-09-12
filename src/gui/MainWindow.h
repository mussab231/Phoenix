#pragma once

#include <QMainWindow>

class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class DownloadQueue;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onAdd();
    void onPause();
    void onResume();
    void onRemove();
    void onItemAdded(int id);
    void onItemChanged(int id);
    void onItemRemoved(int id);

private:
    int rowForId(int id) const;
    int selectedId() const;
    void updateRow(int id);
    static QString formatSize(qint64 bytes);
    static QString formatSpeed(double bytesPerSec);
    static QString baseName(const QString& path);

    QTableWidget* m_table = nullptr;
    QSpinBox* m_maxBox = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_resumeBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;

    DownloadQueue* m_queue = nullptr;
};
