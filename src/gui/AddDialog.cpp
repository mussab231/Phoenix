#include "gui/AddDialog.h"

#include "core/HttpClient.h"
#include "core/UrlMatcher.h"
#include "gui/I18n.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <thread>

namespace {

// Windows forbids these characters in file names; a URL path segment can
// contain several of them (query strings especially). Left alone they make
// the open() call fail outright or, worse, get silently mangled.
QString sanitizeFileName(QString name) {
    name.remove(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")));
    // Windows silently drops trailing dots and spaces, which would make the
    // on-disk name differ from the one we show and resume-state lookups key on.
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    // Reserved DOS device names (CON, PRN, AUX, NUL, COM1..9, LPT1..9) are
    // usable in paths but never as a real file; opening them hits a device.
    const QString stem = name.section(QLatin1Char('.'), 0, 0).toUpper();
    const QSet<QString> reserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9")};
    if (reserved.contains(stem))
        name = QStringLiteral("download") + name.mid(stem.size());
    if (name.isEmpty())
        name = QStringLiteral("download.bin");
    return name;
}

} // namespace

AddDialog::AddDialog(QWidget* parent, int defaultSegments,
                     int defaultMaxSpeedKBs, const QString& defaultDir)
    : QDialog(parent), m_defaultDir(defaultDir) {
    setWindowTitle(I18n::t("Add download"));
    resize(520, 200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    auto* form = new QFormLayout();
    form->setSpacing(10);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    form->addRow(I18n::t("URL:"), m_urlEdit);

    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_pathEdit = new QLineEdit(pathRow);
    auto* browseBtn = new QPushButton(I18n::t("Browse..."), pathRow);
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(browseBtn);
    form->addRow(I18n::t("Save to:"), pathRow);

    m_segmentsBox = new QSpinBox(this);
    m_segmentsBox->setRange(1, 16);
    m_segmentsBox->setValue(defaultSegments);
    form->addRow(I18n::t("Connections:"), m_segmentsBox);

    m_speedBox = new QSpinBox(this);
    m_speedBox->setRange(0, 100000); // KB/s
    m_speedBox->setValue(defaultMaxSpeedKBs);
    m_speedBox->setSuffix(QStringLiteral(" KB/s"));
    m_speedBox->setSpecialValueText(I18n::t("Unlimited"));
    m_speedBox->setToolTip(
        I18n::t("Maximum download speed. Unlimited by default."));
    form->addRow(I18n::t("Max speed:"), m_speedBox);

    m_scheduleCheck = new QCheckBox(I18n::t("Start later"), this);
    m_scheduleEdit =
        new QDateTimeEdit(QDateTime::currentDateTime().addSecs(3600), this);
    m_scheduleEdit->setCalendarPopup(true);
    m_scheduleEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_scheduleEdit->setEnabled(false);
    connect(m_scheduleCheck, &QCheckBox::toggled, m_scheduleEdit,
            &QWidget::setEnabled);
    form->addRow(m_scheduleCheck, m_scheduleEdit);

    // Optional integrity check: paste the publisher's SHA-256 and the finished
    // file is verified automatically. Blank (default) skips verification.
    m_hashEdit = new QLineEdit(this);
    m_hashEdit->setPlaceholderText(
        QStringLiteral("a1b2c3...  (64 hex chars, optional)"));
    m_hashEdit->setToolTip(I18n::t(
        "SHA-256 checksum to verify against once the download finishes. "
        "Optional: leave blank to skip verification."));
    form->addRow(I18n::t("SHA-256:"), m_hashEdit);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, &AddDialog::onBrowse);
    // Re-derive the output name whenever the URL is finished, unless the user
    // has already typed one by hand.
    connect(m_urlEdit, &QLineEdit::editingFinished, this, &AddDialog::onUrlChanged);
    connect(m_pathEdit, &QLineEdit::textEdited, this, [this] { m_userEditedPath = true; });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_urlEdit->text().trimmed().isEmpty() &&
            !m_pathEdit->text().trimmed().isEmpty())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Convenience: a copy of a download URL typically sits in the clipboard,
    // so pre-fill the URL and a first-guess target name from it. The name is
    // refined shortly after by probeFilename() (Content-Disposition / type).
    const QString clipText = QGuiApplication::clipboard()->text();
    const std::string first = UrlMatcher::extractFirstUrl(clipText.toStdString());
    if (!first.empty()) {
        m_urlEdit->setText(QString::fromStdString(first));
        QUrl u(QString::fromStdString(first));
        QString name = sanitizeFileName(u.fileName());
        if (name.isEmpty())
            name = QStringLiteral("download.bin");
        if (!m_defaultDir.isEmpty())
            m_pathEdit->setText(QDir(m_defaultDir).filePath(name));
        probeFilename();
    }

    m_urlEdit->setFocus();
}

void AddDialog::onUrlChanged() {
    probeFilename();
}

void AddDialog::probeFilename() {
    const std::string url = m_urlEdit->text().trimmed().toStdString();
    if (url.empty())
        return;
    // Never clobber a name the user picked by hand.
    const QString current = m_pathEdit->text().trimmed();
    if (!current.isEmpty() && m_userEditedPath)
        return;

    // The refinement only matters when the URL's own path gives no usable
    // name (e.g. https://cdn.example.com/d?id=1234); a typed URL is left as is.
    const QUrl q(QString::fromStdString(url));
    if (!q.fileName().isEmpty() && QFileInfo(q.fileName()).suffix().isEmpty() == false)
        return;

    // The probe blocks on the network, so it runs off the UI thread; the name
    // is applied back on the dialog thread when it lands.
    QPointer<AddDialog> guard(this);
    const QString dir = m_defaultDir;
    std::thread([guard, url, dir]() {
        std::string guessed;
        try {
            guessed = HttpClient::guessFilename(url);
        } catch (...) {
            return;
        }
        if (guessed.empty())
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, name = QString::fromStdString(guessed), dir]() {
                if (!guard)
                    return;
                const QString path =
                    dir.isEmpty() ? name : QDir(dir).filePath(name);
                guard->m_pathEdit->setText(path);
                guard->m_nameFromProbe = true;
            },
            Qt::QueuedConnection);
    }).detach();
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

QString AddDialog::expectedSha256() const {
    // Tolerate pasted hashes with spaces/colons ("ab cd ef" or checksum-file
    // style); only hex digits are kept.
    QString out;
    for (const QChar ch : m_hashEdit->text().trimmed()) {
        const ushort c = ch.unicode();
        const bool hex =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (hex)
            out.append(ch);
    }
    return out.length() == 64 ? out.toLower() : QString();
}

void AddDialog::onBrowse() {
    QString file = QFileDialog::getSaveFileName(
        this, I18n::t("Save as"),
        m_defaultDir.isEmpty() ? QString() : m_defaultDir + QStringLiteral("/download.bin"));
    if (!file.isEmpty())
        m_pathEdit->setText(file);
}
