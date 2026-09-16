#include "gui/I18n.h"

#include <QApplication>
#include <QHash>
#include <QLocale>
#include <QStringList>
#include <QWidget>

bool I18n::s_arabic = false;

void I18n::setArabic(bool on) {
    s_arabic = on;
}

bool I18n::isArabic() {
    return s_arabic;
}

void I18n::applyFromSetting(int lang) {
    const bool arabic =
        lang == 2 || (lang == 0 && QLocale::system().language() == QLocale::Arabic);
    s_arabic = arabic;

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app)
        return;
    const Qt::LayoutDirection dir =
        arabic ? Qt::RightToLeft : Qt::LeftToRight;
    QApplication::setLayoutDirection(dir);
    // Re-layout every open window (setting only the application direction
    // would leave already-created top-level widgets untouched).
    const auto toplevels = app->topLevelWidgets();
    for (QWidget* w : toplevels) {
        if (w->layoutDirection() != dir)
            w->setLayoutDirection(dir);
    }
}

namespace {

const QHash<QString, QString>& strings() {
    static const QHash<QString, QString> map{
        // --- MainWindow top bar ---
        {QStringLiteral("Add"), QStringLiteral("إضافة")},
        {QStringLiteral("Pause"), QStringLiteral("إيقاف مؤقت")},
        {QStringLiteral("Resume"), QStringLiteral("استكمال")},
        {QStringLiteral("Remove"), QStringLiteral("حذف")},
        {QStringLiteral("Retry"), QStringLiteral("إعادة المحاولة")},
        {QStringLiteral("Settings"), QStringLiteral("الإعدادات")},
        {QStringLiteral("Clear completed"),
         QStringLiteral("مسح المكتملة")},
        {QStringLiteral("Pause the selected download (resumable)"),
         QStringLiteral("إيقاف التحميل المحدد مؤقتًا (قابل للاستكمال)")},
        {QStringLiteral("Resume the selected download"),
         QStringLiteral("استكمال التحميل المحدد")},
        {QStringLiteral("Remove the selected download from the queue"),
         QStringLiteral("حذف التحميل المحدد من القائمة")},
        {QStringLiteral("Retry the selected failed download"),
         QStringLiteral("إعادة محاولة التحميل المحدد الفاشل")},
        {QStringLiteral("Saved preferences: folder, connections, speed, power, clipboard"),
         QStringLiteral("التفضيلات المحفوظة: المجلد، الاتصالات، السرعة، الطاقة، الحافظة")},
        {QStringLiteral("Max simultaneous:"), QStringLiteral("أقصى عدد متوازٍ:")},
        {QStringLiteral("When done:"), QStringLiteral("عند الانتهاء:")},
        {QStringLiteral("Do nothing"), QStringLiteral("لا تفعل شيئًا")},
        {QStringLiteral("Sleep"), QStringLiteral("سكون")},
        {QStringLiteral("Hibernate"), QStringLiteral("إسبات")},
        {QStringLiteral("Shutdown"), QStringLiteral("إيقاف التشغيل")},
        {QStringLiteral("Clear completed"),
         QStringLiteral("مسح المكتملة")},

        // --- queue table ---
        {QStringLiteral("File"), QStringLiteral("الملف")},
        {QStringLiteral("Size"), QStringLiteral("الحجم")},
        {QStringLiteral("Progress"), QStringLiteral("التقدم")},
        {QStringLiteral("Speed"), QStringLiteral("السرعة")},
        {QStringLiteral("Status"), QStringLiteral("الحالة")},

        // --- tray ---
        {QStringLiteral("Phoenix Download Manager"),
         QStringLiteral("مدير تحميل فينكس")},
        {QStringLiteral("Show / Hide"), QStringLiteral("إظهار / إخفاء")},
        {QStringLiteral("Add Download..."), QStringLiteral("إضافة تحميل...")},
        {QStringLiteral("Settings..."), QStringLiteral("الإعدادات...")},
        {QStringLiteral("Pause All"), QStringLiteral("إيقاف الكل مؤقتًا")},
        {QStringLiteral("Resume All"), QStringLiteral("استكمال الكل")},
        {QStringLiteral("Watch clipboard for links"),
         QStringLiteral("مراقبة الحافظة للروابط")},
        {QStringLiteral("Quit"), QStringLiteral("خروج")},
        {QStringLiteral("Still running in the tray. Right-click the icon for options."),
         QStringLiteral("ما زالت تعمل في العلبة. انقر بزر الفأرة الأيمن على الأيقونة للخيارات.")},
        {QStringLiteral("Download added from clipboard:\n%1"),
         QStringLiteral("أُضيف التحميل من الحافظة:\n%1")},
        {QStringLiteral("Download complete:\n%1"),
         QStringLiteral("اكتمل التحميل:\n%1")},

        // --- status bar ---
        {QStringLiteral("Running: %1   Queued: %2   Completed: %3"),
         QStringLiteral("قيد التحميل: %1   في الانتظار: %2   مكتمل: %3")},
        {QStringLiteral("   Paused: %1"), QStringLiteral("   متوقف: %1")},
        {QStringLiteral("   Failed: %1"), QStringLiteral("   فشل: %1")},

        // --- AddDialog ---
        {QStringLiteral("Add download"), QStringLiteral("إضافة تحميل")},
        {QStringLiteral("URL:"), QStringLiteral("الرابط:")},
        {QStringLiteral("Save to:"), QStringLiteral("الحفظ إلى:")},
        {QStringLiteral("Browse..."), QStringLiteral("استعراض...")},
        {QStringLiteral("Connections:"), QStringLiteral("الاتصالات:")},
        {QStringLiteral("Max speed:"), QStringLiteral("السرعة القصوى:")},
        {QStringLiteral("Unlimited"), QStringLiteral("غير محدودة")},
        {QStringLiteral("Start later"), QStringLiteral("البدء لاحقًا")},
        {QStringLiteral("Maximum download speed. Unlimited by default."),
         QStringLiteral("السرعة القصوى للتحميل. غير محدودة افتراضيًا.")},
        {QStringLiteral("Save as"), QStringLiteral("الحفظ باسم")},

        // --- checksum verification ---
        {QStringLiteral("SHA-256:"),
         QStringLiteral("SHA-256:")},
        {QStringLiteral("SHA-256 checksum to verify against once the download finishes. "
                        "Optional: leave blank to skip verification."),
         QStringLiteral("بصمة SHA-256 للتحقق من الملف عند اكتمال التحميل. "
                        "اختياري: اتركه فارغًا لتخطي التحقق.")},

        // --- main status bar (link catcher) ---
        {QStringLiteral("Link catcher ready: http://127.0.0.1:%1/add?token=%2 (and phoenix:// links)"),
         QStringLiteral("ملتقِط الروابط جاهز: http://127.0.0.1:%1/add?token=%2 (وروابط phoenix://)")},

        // --- SettingsDialog ---
        {QStringLiteral("Default folder:"), QStringLiteral("المجلد الافتراضي:")},
        {QStringLiteral("Max simultaneous:"), QStringLiteral("أقصى عدد متوازٍ:")},
        {QStringLiteral("Default connections:"),
         QStringLiteral("الاتصالات الافتراضية:")},
        {QStringLiteral("Default max speed:"),
         QStringLiteral("السرعة القصوى الافتراضية:")},
        {QStringLiteral("When done:"), QStringLiteral("عند الانتهاء:")},
        {QStringLiteral("Link catching:"), QStringLiteral("التقاط الروابط:")},
        {QStringLiteral("Watch clipboard for links"),
         QStringLiteral("مراقبة الحافظة للروابط")},
        {QStringLiteral("Auto-detect copied download URLs (also on the tray menu)."),
         QStringLiteral("كشف روابط التحميل المنسوخة تلقائيًا (متاح أيضًا من قائمة العلبة).")},
        {QStringLiteral("Start with Windows"), QStringLiteral("البدء مع ويندوز")},
        {QStringLiteral("Start minimized to tray"),
         QStringLiteral("البدء مصغرًا في العلبة")},
        {QStringLiteral("Language:"), QStringLiteral("اللغة:")},
        {QStringLiteral("System (auto)"), QStringLiteral("تلقائي (حسب النظام)")},
        {QStringLiteral("Choose default folder"),
         QStringLiteral("اختر المجلد الافتراضي")},
    };
    return map;
}

} // namespace

QString I18n::t(const char* en) {
    return t(QString::fromUtf8(en));
}

QString I18n::t(const QString& en) {
    if (!s_arabic)
        return en;
    const auto it = strings().constFind(en);
    return it != strings().constEnd() ? it.value() : en;
}

QString I18n::status(const QString& en) {
    if (!s_arabic)
        return en;

    if (en == QStringLiteral("Queued"))
        return QStringLiteral("في الانتظار");
    if (en == QStringLiteral("Paused"))
        return QStringLiteral("متوقف");
    if (en == QStringLiteral("Pausing..."))
        return QStringLiteral("جارٍ الإيقاف...");
    if (en == QStringLiteral("Connecting..."))
        return QStringLiteral("جارٍ الاتصال...");
    if (en == QStringLiteral("Downloading..."))
        return QStringLiteral("جارٍ التحميل...");
    if (en == QStringLiteral("Completed"))
        return QStringLiteral("مكتمل");
    if (en == QStringLiteral("Failed"))
        return QStringLiteral("فشل");
    if (en == QStringLiteral("Paused - resume anytime"))
        return QStringLiteral("متوقف - استكمله متى شئت");
    if (en == QStringLiteral("Paused - press Download to resume"))
        return QStringLiteral("متوقف - اضغط استكمال للمتابعة");

    if (en.startsWith(QStringLiteral("Downloading (")) &&
        en.endsWith(QStringLiteral(" connections)..."))) {
        // "Downloading (8 connections)..." -> "جارٍ التحميل (8 اتصالات)..."
        const QString n =
            en.mid(QStringLiteral("Downloading (").length(),
                   en.length() - QStringLiteral("Downloading (").length() -
                       QStringLiteral(" connections)...").length());
        return QStringLiteral("جارٍ التحميل (%1 اتصالات)...").arg(n);
    }
    if (en.startsWith(QStringLiteral("Resuming from ")) &&
        en.endsWith(QStringLiteral(")..."))) {
        // "Resuming from 55%... (8 connections)..." -> Arabic equivalent.
        const int at = en.indexOf(QStringLiteral("%... ("));
        if (at > 0) {
            const QString pct =
                en.mid(QStringLiteral("Resuming from ").length(),
                       at - QStringLiteral("Resuming from ").length());
            const QString tail = en.mid(at);
            const int close = tail.indexOf(QStringLiteral(")"));
            const QString n =
                close > 0 ? tail.mid(0, close + 1) : tail;
            return QStringLiteral("جارٍ الاستكمال من %1%%2...").arg(pct).arg(n);
        }
    }
    if (en.startsWith(QStringLiteral("Scheduled ")))
        return QStringLiteral("مجدول ") +
               en.mid(QStringLiteral("Scheduled ").length());
    if (en.startsWith(QStringLiteral("Failed: ")))
        return QStringLiteral("فشل: ") +
               en.mid(QStringLiteral("Failed: ").length());

    if (en == QStringLiteral("Checksum OK"))
        return QStringLiteral("التكامل سليم");
    if (en == QStringLiteral("Checksum FAILED"))
        return QStringLiteral("التكامل فاسد");
    if (en == QStringLiteral("Completed (checksum unreadable)"))
        return QStringLiteral("مكتمل (تعذّر التحقق)");

    return en;
}