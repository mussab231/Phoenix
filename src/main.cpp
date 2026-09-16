#include "core/DownloadQueue.h"
#include "core/Checksum.h"
#include "core/SelfTest.h"
#include "core/HttpClient.h"
#include "core/HttpListener.h"
#include "core/NativeHost.h"
#include "core/PowerControl.h"
#include "core/ProtocolRegistrar.h"
#include "core/RateLimiter.h"
#include "core/SettingsStore.h"
#include "core/SingleInstance.h"
#include "core/UrlCodec.h"
#include "core/UrlMatcher.h"
#include "gui/I18n.h"
#include "gui/MainWindow.h"
#include "models/DownloadItem.h"

#include <winsock2.h>
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPalette>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef PHOENIX_VERSION
#define PHOENIX_VERSION "0.9.0-dev"
#endif

namespace {

// Dark Fusion theme + Phoenix style sheet. GUI only.
void applyTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x17, 0x17, 0x1f));
    pal.setColor(QPalette::WindowText, QColor(0xe9, 0xe9, 0xf2));
    pal.setColor(QPalette::Base, QColor(0x19, 0x19, 0x22));
    pal.setColor(QPalette::AlternateBase, QColor(0x1d, 0x1d, 0x29));
    pal.setColor(QPalette::Text, QColor(0xe9, 0xe9, 0xf2));
    pal.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x3d));
    pal.setColor(QPalette::ButtonText, QColor(0xe9, 0xe9, 0xf2));
    pal.setColor(QPalette::Highlight, QColor(0xff, 0x7a, 0x1a));
    pal.setColor(QPalette::HighlightedText, QColor(0x1a, 0x12, 0x0a));
    pal.setColor(QPalette::ToolTipBase, QColor(0x26, 0x26, 0x38));
    pal.setColor(QPalette::ToolTipText, QColor(0xe9, 0xe9, 0xf2));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6a, 0x6a, 0x80));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6a, 0x6a, 0x80));
    app.setPalette(pal);

    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
}

} // namespace


int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::printf("Phoenix %s\n", PHOENIX_VERSION);
            return 0;
        }
        if (arg == "--self-test")
            return SelfTest::runSelfTest();
        if (arg == "--self-test-mt")
            return SelfTest::runMultiSegmentTest();
        if (arg == "--self-test-resume")
            return SelfTest::runResumeTest();
        if (arg == "--self-test-queue")
            return SelfTest::runQueueTest();
        if (arg == "--self-test-schedule")
            return SelfTest::runScheduleTest();
        if (arg == "--self-test-sleep")
            return SelfTest::runSleepTest();
        if (arg == "--self-test-limit")
            return SelfTest::runSpeedLimitTest();
        if (arg == "--self-test-retry")
            return SelfTest::runRetryTest();
        if (arg == "--self-test-listen")
            return SelfTest::runListenTest();
        if (arg == "--self-test-proto")
            return SelfTest::runProtoTest();
        if (arg == "--self-test-urlmatch")
            return SelfTest::runUrlMatchTest();
        if (arg == "--self-test-settings")
            return SelfTest::runSettingsTest();
        if (arg == "--self-test-native")
            return SelfTest::runNativeHostTest();
        if (arg == "--self-test-checksum")
            return SelfTest::runChecksumTest();
        if (arg == "--native-messaging")
            return runNativeHost(argc, argv);
        // Chrome/Edge spawn the native host with the calling extension's
        // origin ("chrome-extension://<id>/") as the first argument and no
        // flag of its own.
        if (arg.rfind("chrome-extension://", 0) == 0)
            return runNativeHost(argc, argv);
        if (arg == "--checksum" && i + 1 < argc) {
            // Phoenix --checksum <file>          prints lowercase hex SHA-256
            // Phoenix --checksum <file> <hash>   verifies; exit 0 = match
            const std::string path = argv[++i];
            const std::string hash = (i + 1 < argc) ? argv[++i] : std::string();
            const std::string got = Checksum::sha256File(path);
            if (got.empty()) {
                std::fprintf(stderr, "CHECKSUM FAILED: cannot read %s\n",
                             path.c_str());
                return 1;
            }
            if (hash.empty()) {
                std::printf("%s  %s\n", got.c_str(), path.c_str());
                return 0;
            }
            if (Checksum::hexEquals(got, hash)) {
                std::printf("CHECKSUM OK\n");
                return 0;
            }
            std::fprintf(stderr, "CHECKSUM MISMATCH\n  expected %s\n  got      %s\n",
                         hash.c_str(), got.c_str());
            return 1;
        }
        if (arg == "--register")
            return ProtocolRegistrar::registerHandler() ? 0 : 1;
        if (arg == "--unregister")
            return ProtocolRegistrar::unregisterHandler() ? 0 : 1;
    }

    // OS/browser launch with a phoenix:// link: decode it. If another Phoenix
    // is running we hand the link over via the single-instance pipe and exit.
    QString pendingLink;
    if (argc > 1) {
        std::string url, name;
        if (UrlCodec::parseProtocolLink(argv[1], url, name))
            pendingLink = QString::fromStdString(
                UrlCodec::buildProtocolLink(url, name));
    }

    QApplication app(argc, argv);
    applyTheme(app);
    // Persistent settings live in HKCU\Software\Phoenix\Phoenix (QSettings).
    app.setOrganizationName(QStringLiteral("Phoenix"));
    app.setOrganizationDomain(QStringLiteral("phoenix.local"));
    app.setApplicationName(QStringLiteral("Phoenix"));

    // UI language: explicit override from Settings, else follow the system
    // locale. Arabic flips the whole UI to right-to-left. Must run before any
    // widget is constructed.
    SettingsStore uiPrefs;
    I18n::applyFromSetting(uiPrefs.language());

    QIcon windowIcon;
    for (int s : {16, 24, 32, 48, 64, 128, 256})
        windowIcon.addFile(QStringLiteral(":/icons/phoenix-%1.png").arg(s));
    app.setWindowIcon(windowIcon);

    SingleInstance single;
    if (!pendingLink.isEmpty() && single.tryActivate(pendingLink))
        return 0; // the running instance took the link
    if (!single.becomeOwner())
        std::fprintf(stderr, "WARNING: single-instance pipe busy\n");

    MainWindow w;
    QObject::connect(&single, &SingleInstance::commandReceived, [&w](const QString& cmd) {
        if (cmd == QStringLiteral("::show")) {
            w.showWindow();
            return;
        }
        std::string url, name;
        if (UrlCodec::parseProtocolLink(cmd.toStdString(), url, name))
            w.acceptIncomingUrl(QString::fromStdString(url),
                                QString::fromStdString(name));
        else
            w.acceptIncomingUrl(cmd);
    });
    if (!pendingLink.isEmpty()) {
        const QString link = pendingLink;
        QTimer::singleShot(0, &w, [&w, link] {
            std::string url, name;
            if (UrlCodec::parseProtocolLink(link.toStdString(), url, name))
                w.acceptIncomingUrl(QString::fromStdString(url),
                                    QString::fromStdString(name));
        });
    }

    // Register the phoenix:// scheme silently on first run (HKCU, no admin).
    if (!ProtocolRegistrar::isRegistered())
        ProtocolRegistrar::registerHandler();

    // Loopback HTTP listener for link-catching without the protocol handler.
    HttpListener listener;
    QObject::connect(&listener, &HttpListener::urlReceived, &w,
                     &MainWindow::acceptIncomingUrl);
    quint16 port = listener.start(51047);
    if (port)
        w.statusBar()->showMessage(
            I18n::t("Link catcher ready: "
                    "http://127.0.0.1:%1/add?token=%2 (and phoenix:// links)")
                .arg(port)
                .arg(listener.token()));

    if (!w.startsHidden())
        w.show();
    return app.exec();
}
