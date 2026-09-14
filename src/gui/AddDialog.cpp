#include "gui/AddDialog.h"

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AddDialog::AddDialog(QWidget* parent, int defaultSegments,
                     int defaultMaxSpeedKBs, const QString& defaultDir)
    : QDialog(parent), m_defaultDir(defaultDir) {
    setWindowTitle(QStringLiteral("Add download"));
    resize(520, 200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto* form = new QFormLayout();
    form->setSpacing(10);

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
    m_segmentsBox->setValue(defaultSegments);
    form->addRow(QStringLiteral("Connections:"), m_segmentsBox);

    m_speedBox = new QSpinBox(this);
    m_speedBox->setRange(0, 100000); // KB/s
    m_speedBox->setValue(defaultMaxSpeedKBs);
    m_speedBox->setSuffix(QStringLiteral(" KB/s"));
    m_speedBox->setSpecialValueText(QStringLiteral("Unlimited"));
    m_speedBox->setToolTip(
        QStringLiteral("Maximum download speed. Unlimited by default."));
    form->addRow(QStringLiteral("Max speed:"), m_speedBox);

    m_scheduleCheck = new QCheckBox(QStringLiteral("Start later"), this);
    m_scheduleEdit =
        new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), this);
    m_scheduleEdit->setCalendarPopup(true);
    m_scheduleEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_scheduleEdit->setEnabled(false);
    connect(m_scheduleCheck, &QCheckBox::toggled, m_scheduleEdit,
            &QWidget::setEnabled);
    form->addRow(m_scheduleCheck, m_scheduleEdit);

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

    m_urlEdit->setFocus();
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

double AddDialog::maxSpeedBps() const {
    return static_cast<double>(m_speedBox->value()) * 1024.0;
}

bool AddDialog::isScheduled() const {
    return m_scheduleCheck->isChecked();
}

qint64 AddDialog::scheduledAt() const {
    return m_scheduleEdit->dateTime().toMSecsSinceEpoch();
}

void AddDialog::onBrowse() {
    QString file = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save as"),
        m_defaultDir.isEmpty() ? QString() : m_defaultDir + QStringLiteral("/download.bin"));
    if (!file.isEmpty())
        m_pathEdit->setText(file);
}
