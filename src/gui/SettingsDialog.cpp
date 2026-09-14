#include "gui/SettingsDialog.h"

#include "core/SettingsStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsStore* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(480, 280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto* form = new QFormLayout();
    form->setSpacing(10);

    auto* dirRow = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    m_dirEdit = new QLineEdit(m_settings->defaultDirectory(), dirRow);
    auto* browseBtn = new QPushButton(QStringLiteral("Browse..."), dirRow);
    dirLayout->addWidget(m_dirEdit);
    dirLayout->addWidget(browseBtn);
    form->addRow(QStringLiteral("Default folder:"), dirRow);

    m_concurrentBox = new QSpinBox(this);
    m_concurrentBox->setRange(1, 5);
    m_concurrentBox->setValue(m_settings->maxConcurrent());
    form->addRow(QStringLiteral("Max simultaneous:"), m_concurrentBox);

    m_segmentsBox = new QSpinBox(this);
    m_segmentsBox->setRange(1, 16);
    m_segmentsBox->setValue(m_settings->defaultSegments());
    form->addRow(QStringLiteral("Default connections:"), m_segmentsBox);

    m_speedBox = new QSpinBox(this);
    m_speedBox->setRange(0, 100000);
    m_speedBox->setValue(m_settings->maxSpeedKBs());
    m_speedBox->setSuffix(QStringLiteral(" KB/s"));
    m_speedBox->setSpecialValueText(QStringLiteral("Unlimited"));
    form->addRow(QStringLiteral("Default max speed:"), m_speedBox);

    m_doneBox = new QComboBox(this);
    m_doneBox->addItems({QStringLiteral("Do nothing"), QStringLiteral("Sleep"),
                         QStringLiteral("Hibernate"), QStringLiteral("Shutdown")});
    m_doneBox->setCurrentIndex(m_settings->autoAction());
    form->addRow(QStringLiteral("When done:"), m_doneBox);

    m_watchClip = new QCheckBox(
        QStringLiteral("Watch clipboard for links"), this);
    m_watchClip->setChecked(m_settings->watchClipboard());
    m_watchClip->setToolTip(
        QStringLiteral("Auto-detect copied download URLs (also on the tray menu)."));
    form->addRow(QStringLiteral("Link catching:"), m_watchClip);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowse);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::onBrowse() {
    QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose default folder"), m_dirEdit->text());
    if (!dir.isEmpty())
        m_dirEdit->setText(dir);
}

void SettingsDialog::onAccept() {
    m_settings->setDefaultDirectory(m_dirEdit->text().trimmed());
    m_settings->setMaxConcurrent(m_concurrentBox->value());
    m_settings->setDefaultSegments(m_segmentsBox->value());
    m_settings->setMaxSpeedKBs(m_speedBox->value());
    m_settings->setAutoAction(m_doneBox->currentIndex());
    m_settings->setWatchClipboard(m_watchClip->isChecked());
    accept();
}