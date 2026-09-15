#include "gui/SettingsDialog.h"

#include "core/SettingsStore.h"
#include "gui/I18n.h"

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
    setWindowTitle(I18n::t("Settings"));
    resize(520, 360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto* form = new QFormLayout();
    form->setSpacing(10);

    auto* dirRow = new QWidget(this);
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    m_dirEdit = new QLineEdit(m_settings->defaultDirectory(), dirRow);
    auto* browseBtn = new QPushButton(I18n::t("Browse..."), dirRow);
    dirLayout->addWidget(m_dirEdit);
    dirLayout->addWidget(browseBtn);
    form->addRow(I18n::t("Default folder:"), dirRow);

    m_concurrentBox = new QSpinBox(this);
    m_concurrentBox->setRange(1, 5);
    m_concurrentBox->setValue(m_settings->maxConcurrent());
    form->addRow(I18n::t("Max simultaneous:"), m_concurrentBox);

    m_segmentsBox = new QSpinBox(this);
    m_segmentsBox->setRange(1, 16);
    m_segmentsBox->setValue(m_settings->defaultSegments());
    form->addRow(I18n::t("Default connections:"), m_segmentsBox);

    m_speedBox = new QSpinBox(this);
    m_speedBox->setRange(0, 100000);
    m_speedBox->setValue(m_settings->maxSpeedKBs());
    m_speedBox->setSuffix(QStringLiteral(" KB/s"));
    m_speedBox->setSpecialValueText(I18n::t("Unlimited"));
    form->addRow(I18n::t("Default max speed:"), m_speedBox);

    m_doneBox = new QComboBox(this);
    m_doneBox->addItems({I18n::t("Do nothing"), I18n::t("Sleep"),
                         I18n::t("Hibernate"), I18n::t("Shutdown")});
    m_doneBox->setCurrentIndex(m_settings->autoAction());
    form->addRow(I18n::t("When done:"), m_doneBox);

    m_watchClip = new QCheckBox(I18n::t("Watch clipboard for links"), this);
    m_watchClip->setChecked(m_settings->watchClipboard());
    m_watchClip->setToolTip(
        I18n::t("Auto-detect copied download URLs (also on the tray menu)."));
    form->addRow(I18n::t("Link catching:"), m_watchClip);

    m_langBox = new QComboBox(this);
    m_langBox->addItems({I18n::t("System (auto)"), QStringLiteral("English"),
                         QStringLiteral("العربية")});
    m_langBox->setCurrentIndex(m_settings->language());
    form->addRow(I18n::t("Language:"), m_langBox);

    m_autoStart = new QCheckBox(I18n::t("Start with Windows"), this);
    m_autoStart->setChecked(m_settings->autoStart());
    m_startMinimized = new QCheckBox(I18n::t("Start minimized to tray"), this);
    m_startMinimized->setChecked(m_settings->startMinimized());
    form->addRow(QString(), m_autoStart);
    form->addRow(QString(), m_startMinimized);

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
        this, I18n::t("Choose default folder"), m_dirEdit->text());
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
    m_settings->setLanguage(m_langBox->currentIndex());
    m_settings->setAutoStart(m_autoStart->isChecked());
    m_settings->setStartMinimized(m_startMinimized->isChecked());
    accept();
}