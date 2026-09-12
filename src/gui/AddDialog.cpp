#include "gui/AddDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AddDialog::AddDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Add download"));
    resize(480, 160);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    form->addRow(QStringLiteral("URL:"), m_urlEdit);

    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_pathEdit = new QLineEdit(pathRow);
    auto* browseBtn = new QPushButton(QStringLiteral("Browse..."), pathRow);
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(browseBtn);
    form->addRow(QStringLiteral("Save to:"), pathRow);

    m_segmentsBox = new QSpinBox(this);
    m_segmentsBox->setRange(1, 16);
    m_segmentsBox->setValue(8);
    form->addRow(QStringLiteral("Connections:"), m_segmentsBox);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, &AddDialog::onBrowse);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_urlEdit->text().trimmed().isEmpty() &&
            !m_pathEdit->text().trimmed().isEmpty())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString AddDialog::url() const {
    return m_urlEdit->text().trimmed();
}

QString AddDialog::outputPath() const {
    return m_pathEdit->text().trimmed();
}

int AddDialog::segments() const {
    return m_segmentsBox->value();
}

void AddDialog::onBrowse() {
    QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save as"));
    if (!file.isEmpty())
        m_pathEdit->setText(file);
}
