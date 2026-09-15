#pragma once

#include <QString>

// Two-language UI helper (English default / Arabic with right-to-left
// layout). The app is small, so instead of wiring the full Qt/QTranslator
// pipeline (lupdate/lrelease/CMake genex) we map the handful of user-visible
// strings in one place. main() calls setArabic() early (before any widget is
// constructed); I18n::t() translates static UI strings and I18n::status()
// maps the engine's English status lines at the display boundary.
class I18n {
public:
    static void setArabic(bool on);
    static bool isArabic();

    // Resolves a stored preference (0 = system, 1 = English, 2 = Arabic),
    // enables the matching language and switches the layout direction of all
    // open windows immediately (no restart needed).
    static void applyFromSetting(int lang);

    // Static UI strings (button labels, headers, tooltips, ...).
    static QString t(const char* en);
    static QString t(const QString& en);

    // Dynamic status rows coming from the core ("Queued", "Downloading...",
    // "Failed: <msg>", "Scheduled 14:30", ...). Known patterns are mapped,
    // anything unrecognized is passed through untouched.
    static QString status(const QString& en);

private:
    static bool s_arabic;
};