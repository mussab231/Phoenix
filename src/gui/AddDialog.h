#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;

class AddDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddDialog(QWidget* parent = nullptr);

    QString url() const;
    QString outputPath() const;
    int segments() const;

private slots:
    void onBrowse();

private:
    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QSpinBox* m_segmentsBox = nullptr;
};
