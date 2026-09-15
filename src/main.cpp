#include "core/DownloadQueue.h"
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

namespace {

std::string tempFile(const char* name) {
    char tmp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + name;
}

std::int64_t diskSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f.is_open() ? static_cast<std::int64_t>(f.tellg()) : -1;
}

bool filesIdentical(const std::string& a, const std::string& b) {
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa.is_open() || !fb.is_open())
        return false;
    std::vector<char> ba(1024 * 1024), bb(1024 * 1024);
    for (;;) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb)
            return false;
        if (na == 0)
            return true; // both at EOF together
        if (std::memcmp(ba.data(), bb.data(), static_cast<size_t>(na)) != 0)
            return false;
    }
}

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

// Headless smoke test: HEAD + small GET without showing any window.
int runSelfTest() {
    try {
        const std::string url = "https://example.com/";
        std::int64_t size = HttpClient::getFileSize(url);
        std::printf("HEAD size: %lld\n", static_cast<long long>(size));

        std::string out = tempFile("phoenix_selftest.html");
        HttpClient::download(url, out, [](std::int64_t received, std::int64_t total) {
            std::printf("\rGET %lld / %lld", static_cast<long long>(received),
                        static_cast<long long>(total));
        });
        std::printf("\nSELF-TEST OK -> %s\n", out.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::printf("SELF-TEST FAILED: %s\n", e.what());
        return 1;
    }
}

// Multi-segment test: 10 MB over 8 connections, then byte-for-byte compare
// against a classic single-connection reference download.
int runMultiSegmentTest() {
    try {
        const std::string url = "https://proof.ovh.net/files/10Mb.dat";
        const std::int64_t expected = 10LL * 1024 * 1024;

        std::string out = tempFile("phoenix_selftest_mt.bin");
        std::string ref = tempFile("phoenix_reference.bin");
        std::printf("Range support: %s\n",
                    HttpClient::supportsRanges(url) ? "YES" : "NO");

        DWORD t0 = GetTickCount();
        HttpClient::downloadSegmented(
            url, out,
            [](std::int64_t received, std::int64_t total) {
                std::printf("\rMT %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            },
            nullptr, 8);
        double mtSecs = (GetTickCount() - t0) / 1000.0;

        std::int64_t got = diskSize(out);
        std::printf("\nSegmented size: %lld (expected %lld)\n",
                    static_cast<long long>(got), static_cast<long long>(expected));
        if (got != expected) {
            std::printf("MT-TEST FAILED: size mismatch\n");
            return 1;
        }

        std::printf("Downloading single-connection reference...\n");
        t0 = GetTickCount();
        HttpClient::download(
            url, ref,
            [](std::int64_t received, std::int64_t total) {
                std::printf("\rREF %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            });
        double refSecs = (GetTickCount() - t0) / 1000.0;
        std::printf("\n");

        bool same = filesIdentical(out, ref);
        std::remove(ref.c_str());
        std::printf("Content identical: %s\n", same ? "YES" : "NO");
        if (!same) {
            std::printf("MT-TEST FAILED: content mismatch (segment assembly bug)\n");
            return 1;
        }
        std::printf("MT-TEST OK -> %s (multi %.1f MB/s vs single %.1f MB/s)\n",
                    out.c_str(), (got / 1024.0 / 1024.0) / (mtSecs > 0 ? mtSecs : 1),
                    (got / 1024.0 / 1024.0) / (refSecs > 0 ? refSecs : 1));
        return 0;
    } catch (const std::exception& e) {
        std::printf("MT-TEST FAILED: %s\n", e.what());
        return 1;
    }
}

// Resume test: force-cancel at ~2 MB, then resume the same call to completion.
int runResumeTest() {
    const std::string url = "https://proof.ovh.net/files/10Mb.dat";
    const std::int64_t expected = 10LL * 1024 * 1024;
    const std::string out = tempFile("phoenix_resume.bin");
    const std::string state = out + ".phoenix-state";
    std::remove(out.c_str());
    std::remove(state.c_str());

    // Pass 1: download, then pull the plug at ~2 MB (simulates pause/crash).
    std::atomic<bool> stop{false};
    try {
        HttpClient::downloadSegmented(
            url, out,
            [&](std::int64_t received, std::int64_t total) {
                std::printf("\rPASS1 %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
                if (received >= 2LL * 1024 * 1024)
                    stop = true;
            },
            &stop, 8);
        std::printf("\nRESUME-TEST FAILED: pass 1 should have been cancelled\n");
        return 1;
    } catch (const std::exception& e) {
        if (std::string(e.what()) != "cancelled") {
            std::printf("\nRESUME-TEST FAILED (pass 1): %s\n", e.what());
            return 1;
        }
        std::printf("\nPass 1 stopped as planned.\n");
    }

    std::int64_t partial = diskSize(out);
    bool hasState = diskSize(state) > 0;
    std::printf("Partial file: %lld bytes (preallocated %lld), state file: %s\n",
                static_cast<long long>(partial), static_cast<long long>(expected),
                hasState ? "YES" : "NO");
    if (partial != expected || !hasState) {
        std::printf("RESUME-TEST FAILED: preallocated file or state missing\n");
        return 1;
    }

    // Pass 2: same call again - must continue, not restart.
    std::int64_t pass2Start = -1;
    try {
        DWORD t0 = GetTickCount();
        HttpClient::downloadSegmented(
            url, out,
            [&](std::int64_t received, std::int64_t total) {
                if (pass2Start < 0)
                    pass2Start = received; // first callback = resumed offset
                std::printf("\rPASS2 %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            },
            nullptr, 8);
        double secs = (GetTickCount() - t0) / 1000.0;
        std::printf("\nPass 2 done in %.1fs (resumed from %lld)\n", secs,
                    static_cast<long long>(pass2Start));
    } catch (const std::exception& e) {
        std::printf("\nRESUME-TEST FAILED (pass 2): %s\n", e.what());
        return 1;
    }

    if (pass2Start <= 0) {
        std::printf("RESUME-TEST FAILED: pass 2 restarted from zero instead of resuming\n");
        return 1;
    }

    std::int64_t finalSize = diskSize(out);
    bool stateGone = diskSize(state) < 0;
    std::printf("Final size: %lld (expected %lld), state cleaned: %s\n",
                static_cast<long long>(finalSize), static_cast<long long>(expected),
                stateGone ? "YES" : "NO");
    if (finalSize != expected || !stateGone) {
        std::printf("RESUME-TEST FAILED: bad final state\n");
        return 1;
    }
    std::printf("RESUME-TEST OK -> %s\n", out.c_str());
    return 0;
}

// Queue test: two downloads at once through DownloadQueue, verified on disk.
int runQueueTest() {
    int argc = 1;
    char prog[] = "phoenix";
    char* argv[] = {prog};
    QCoreApplication app(argc, argv);

    const std::string smallUrl = "https://example.com/";
    const std::string bigUrl = "https://proof.ovh.net/files/10Mb.dat";
    const std::int64_t bigExpected = 10LL * 1024 * 1024;
    const std::string smallOut = tempFile("phoenix_q1.html");
    const std::string bigOut = tempFile("phoenix_q2.bin");
    std::remove(smallOut.c_str());
    std::remove(bigOut.c_str());
    std::remove((bigOut + ".phoenix-state").c_str());

    DownloadQueue queue;
    queue.setMaxConcurrent(2);
    bool timedOut = false;
    QObject::connect(&queue, &DownloadQueue::queueFinished, &app,
                     &QCoreApplication::quit);
    QObject::connect(&queue, &DownloadQueue::itemChanged, [&](int id) {
        for (const auto& it : queue.items()) {
            if (it.id == id) {
                std::printf("\rQUEUE #%d %lld/%lld %s   ", id,
                            static_cast<long long>(it.receivedBytes),
                            static_cast<long long>(it.totalBytes), it.statusText.c_str());
                break;
            }
        }
    });
    QTimer::singleShot(180000, &app, [&] {
        timedOut = true;
        QCoreApplication::quit();
    });

    int id1 = queue.addDownload(smallUrl, smallOut, 1);
    int id2 = queue.addDownload(bigUrl, bigOut, 8);
    std::printf("Queued #%d (small) and #%d (10 MB), max 2 at once.\n", id1, id2);
    app.exec();
    std::printf("\n");

    if (timedOut) {
        std::printf("QUEUE-TEST FAILED: timed out\n");
        return 1;
    }
    std::int64_t smallSize = diskSize(smallOut);
    std::int64_t bigSize = diskSize(bigOut);
    std::printf("Small: %lld bytes, big: %lld (expected %lld)\n",
                static_cast<long long>(smallSize), static_cast<long long>(bigSize),
                static_cast<long long>(bigExpected));
    if (smallSize <= 0 || bigSize != bigExpected) {
        std::printf("QUEUE-TEST FAILED: bad output sizes\n");
        return 1;
    }
    bool bothDone = true;
    for (const auto& it : queue.items()) {
        if (it.state != DownloadState::Completed)
            bothDone = false;
    }
    if (!bothDone) {
        std::printf("QUEUE-TEST FAILED: not all items Completed\n");
        return 1;
    }
    std::printf("QUEUE-TEST OK\n");
    return 0;
}

// Scheduler test: a download is told to start +2.5s; it must sit as
// "Scheduled", not move a byte early, and only go Running after its time.
int runScheduleTest() {
    int argc = 1;
    char prog[] = "phoenix";
    char* argv[] = {prog};
    QCoreApplication app(argc, argv);

    DownloadQueue queue;
    queue.setMaxConcurrent(1);

    const std::string url = "https://example.com/";
    const std::string out = tempFile("phoenix_sched.html");
    std::remove(out.c_str());
    std::remove((out + ".phoenix-state").c_str());

    const std::int64_t windowMs = 2500;
    const std::int64_t scheduledAt = QDateTime::currentMSecsSinceEpoch() + windowMs;

    bool timedOut = false;
    bool sawScheduledStatus = false;
    std::int64_t startedMs = -1;
    int id = -1;

    QObject::connect(&queue, &DownloadQueue::itemAdded, [&](int nid) {
        id = nid;
        std::printf("SCHEDULE-TEST: item %d queued for start in +%.1fs\n", id,
                    windowMs / 1000.0);
    });
    QObject::connect(&queue, &DownloadQueue::itemChanged, [&](int cid) {
        if (cid != id)
            return;
        for (const auto& it : queue.items()) {
            if (it.id != id)
                continue;
            if (it.state == DownloadState::Idle &&
                it.statusText.find("Scheduled") != std::string::npos)
                sawScheduledStatus = true;
            if (it.state == DownloadState::Running && startedMs < 0)
                startedMs = QDateTime::currentMSecsSinceEpoch();
            std::printf("\rSCHED %lld/%lld %s   ",
                        static_cast<long long>(it.receivedBytes),
                        static_cast<long long>(it.totalBytes), it.statusText.c_str());
            break;
        }
    });
    QObject::connect(&queue, &DownloadQueue::queueFinished, &app,
                     &QCoreApplication::quit);
    QTimer::singleShot(30000, &app, [&] {
        timedOut = true;
        QCoreApplication::quit();
    });

    id = queue.addDownload(url, out, 1, scheduledAt);
    app.exec();
    std::printf("\n");

    if (timedOut) {
        std::printf("SCHEDULE-TEST FAILED: timed out\n");
        return 1;
    }
    bool completed = false;
    for (const auto& it : queue.items())
        if (it.id == id && it.state == DownloadState::Completed)
            completed = true;
    std::int64_t size = diskSize(out);
    double startedAfter = startedMs > 0
                              ? (startedMs - (scheduledAt - windowMs)) / 1000.0
                              : -1.0;
    std::printf("Scheduled status shown: %s, started at +%.2fs (window +%.1fs), "
                "size: %lld bytes\n",
                sawScheduledStatus ? "YES" : "NO", startedAfter,
                windowMs / 1000.0, static_cast<long long>(size));
    if (!sawScheduledStatus || !completed || size <= 0 ||
        (startedMs > 0 && startedMs < scheduledAt)) {
        std::printf("SCHEDULE-TEST FAILED: item ran before its time or never ran\n");
        return 1;
    }
    std::printf("SCHEDULE-TEST OK -> %s\n", out.c_str());
    return 0;
}

// Auto-action test: queue finishes -> "sleep" auto action fires. Power control
// is in test mode, so nothing actually suspends the machine.
int runSleepTest() {
    int argc = 1;
    char prog[] = "phoenix";
    char* argv[] = {prog};
    QCoreApplication app(argc, argv);

    DownloadQueue queue;
    queue.setMaxConcurrent(1);
    PowerControl::g_testMode = true;
    queue.setAutoAction(static_cast<int>(PowerControl::Action::Sleep));

    const std::string out = tempFile("phoenix_sleep.html");
    std::remove(out.c_str());

    bool timedOut = false;
    bool triggerOk = false;
    QObject::connect(&queue, &DownloadQueue::autoActionTriggered, [&](int action) {
        triggerOk = (action == static_cast<int>(PowerControl::Action::Sleep));
        std::printf("SLEEP-TEST: auto action fired (action=%d)\n", action);
        QCoreApplication::quit();
    });
    QTimer::singleShot(30000, &app, [&] {
        timedOut = true;
        QCoreApplication::quit();
    });

    queue.addDownload("https://example.com/", out, 1);
    app.exec();

    std::int64_t size = diskSize(out);
    std::printf("Sleep file size: %lld, trigger ok: %s\n",
                static_cast<long long>(size), triggerOk ? "YES" : "NO");
    PowerControl::g_testMode = false;
    if (timedOut || !triggerOk || size <= 0) {
        std::printf("SLEEP-TEST FAILED\n");
        return 1;
    }
    std::printf("SLEEP-TEST OK\n");
    return 0;
}

// Speed limiter test: 10 MB (≈6.7 s at 1.5 MiB/s) must actually take that
// long and never average faster than the cap.
int runSpeedLimitTest() {
    const std::string url = "https://proof.ovh.net/files/10Mb.dat";
    const std::int64_t expected = 10LL * 1024 * 1024;
    const double limitBps = 1572864.0; // 1.5 MiB/s
    const std::string out = tempFile("phoenix_limit.bin");
    std::remove(out.c_str());
    std::remove((out + ".phoenix-state").c_str());

    RateLimiter limiter(limitBps);
    DWORD t0 = GetTickCount();
    try {
        HttpClient::downloadSegmented(
            url, out,
            [](std::int64_t received, std::int64_t total) {
                std::printf("\rLIMIT %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            },
            nullptr, 8, &limiter, 1, 0);
    } catch (const std::exception& e) {
        std::printf("\nLIMIT-TEST FAILED: %s\n", e.what());
        return 1;
    }
    double secs = (GetTickCount() - t0) / 1000.0;
    std::int64_t got = diskSize(out);
    double avg = (got > 0 && secs > 0) ? got / secs : 0.0;
    double strictSecs = got / limitBps;
    std::printf("\nSize: %lld (expected %lld), %.2fs (strict %.2fs), "
                "avg %.2f MiB/s (cap %.2f MiB/s)\n",
                static_cast<long long>(got), static_cast<long long>(expected), secs,
                strictSecs, avg / 1024.0 / 1024.0, limitBps / 1024.0 / 1024.0);
    if (got != expected) {
        std::printf("LIMIT-TEST FAILED: size mismatch\n");
        return 1;
    }
    if (avg > limitBps * 1.2) { // never wildly over the cap when averaging
        std::printf("LIMIT-TEST FAILED: too fast, limiter not applied\n");
        return 1;
    }
    if (secs < strictSecs * 0.6) {
        std::printf("LIMIT-TEST FAILED: finished far too quickly\n");
        return 1;
    }
    std::printf("LIMIT-TEST OK -> %s\n", out.c_str());
    return 0;
}

// Retry test: simulate transient failures and check both recovery layers.
// Part 1 (single connection) exercises the whole-call retry in
// downloadSegmented; part 2 (8 segments) exercises the per-segment retry.
int runRetryTest() {
    const std::string url = "https://proof.ovh.net/files/10Mb.dat";
    const std::int64_t expected = 10LL * 1024 * 1024;

    // Part 1: single-connection fallback -> whole-call retry layer.
    const std::string out1 = tempFile("phoenix_retry1.bin");
    std::remove(out1.c_str());
    std::remove((out1 + ".phoenix-state").c_str());
    HttpClient::g_testFakeFailures = 1;
    DWORD t0 = GetTickCount();
    try {
        HttpClient::downloadSegmented(
            url, out1,
            [](std::int64_t received, std::int64_t total) {
                std::printf("\rRETRY1 %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            },
            nullptr, 1, nullptr, 3, 200);
    } catch (const std::exception& e) {
        HttpClient::g_testFakeFailures = 0;
        std::printf("\nRETRY-TEST FAILED (part 1, whole-call): %s\n", e.what());
        return 1;
    }
    double secs1 = (GetTickCount() - t0) / 1000.0;
    std::int64_t got1 = diskSize(out1);
    std::printf("\nPart 1: %lld bytes in %.2fs (whole-call retry OK)\n",
                static_cast<long long>(got1), secs1);

    // Part 2: 8 segments -> per-segment retry layer.
    const std::string out2 = tempFile("phoenix_retry2.bin");
    std::remove(out2.c_str());
    std::remove((out2 + ".phoenix-state").c_str());
    HttpClient::g_testFakeFailures = 1;
    t0 = GetTickCount();
    try {
        HttpClient::downloadSegmented(
            url, out2,
            [](std::int64_t received, std::int64_t total) {
                std::printf("\rRETRY2 %lld / %lld", static_cast<long long>(received),
                            static_cast<long long>(total));
            },
            nullptr, 8, nullptr, 3, 200);
    } catch (const std::exception& e) {
        HttpClient::g_testFakeFailures = 0;
        std::printf("\nRETRY-TEST FAILED (part 2, segment): %s\n", e.what());
        return 1;
    }
    HttpClient::g_testFakeFailures = 0;
    double secs2 = (GetTickCount() - t0) / 1000.0;
    std::int64_t got2 = diskSize(out2);
    std::printf("\nPart 2: %lld bytes in %.2fs (segment retry OK)\n",
                static_cast<long long>(got2), secs2);

    if (got1 != expected || got2 != expected) {
        std::printf("RETRY-TEST FAILED: size mismatch after recovery\n");
        return 1;
    }
    std::printf("RETRY-TEST OK\n");
    return 0;
}

// Localhost HTTP listener test: hit /add with an encoded URL, expect the
// urlReceived signal to fire with the decoded value, and /status to answer.
int runListenTest() {
    int argc = 1;
    char prog[] = "phoenix";
    char* argv[] = {prog};
    QCoreApplication app(argc, argv);

    bool gotSignal = false;
    bool timedOut = false;
    QString gotUrl, gotName, statusBody;
    HttpListener listener;
    QObject::connect(&listener, &HttpListener::urlReceived, &app,
                     [&](const QString& u, const QString& n) {
                         gotUrl = u;
                         gotName = n;
                         gotSignal = true;
                         QCoreApplication::quit();
                     });
    QTimer::singleShot(15000, &app, [&] {
        timedOut = true;
        QCoreApplication::quit();
    });

    quint16 port = listener.start(51047);
    if (port == 0) {
        std::printf("LISTEN-TEST FAILED: could not bind a port\n");
        return 1;
    }
    const std::string token = listener.token().toStdString();

    const std::string target = "https://example.com/path/file name.zip";
    QTimer::singleShot(0, &app, [&] {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                const std::string rel = "/add?token=" + token + "&url=" +
                                        UrlCodec::encode(target);
                const std::string req =
                    "GET " + rel + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
                send(s, req.data(), static_cast<int>(req.size()), 0);
                char buf[512]{};
                int n = recv(s, buf, sizeof(buf) - 1, 0);
                if (n > 0)
                    gotUrl = gotUrl; // response body parsed below
            }
            closesocket(s);
        }
        WSACleanup();
    });

    // A request WITHOUT the token must be refused with 401 (closed door).
    QTimer::singleShot(0, &app, [&] {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
                const std::string req = "GET /add?url=https%3A%2F%2Fblocked HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
                send(s, req.data(), static_cast<int>(req.size()), 0);
                char buf[512]{};
                int m = recv(s, buf, sizeof(buf) - 1, 0);
                if (m > 0)
                    statusBody = QString::fromLatin1(buf, m);
            }
            closesocket(s);
        }
        WSACleanup();
    });

    app.exec(); // exits on urlReceived or timeout
    std::printf("Listener on 127.0.0.1:%u, got url signal: %s\n",
                static_cast<unsigned>(port), gotSignal ? "YES" : "NO");
    if (timedOut || !gotSignal) {
        std::printf("LISTEN-TEST FAILED: no urlReceived\n");
        return 1;
    }
    std::printf("Decoded: [%s] name=[%s]\n", gotUrl.toUtf8().constData(),
                gotName.toUtf8().constData());
    if (gotUrl.toStdString() != "https://example.com/path/file name.zip") {
        std::printf("LISTEN-TEST FAILED: wrong decoded url\n");
        return 1;
    }
    if (statusBody.contains(QStringLiteral("200"))) {
        std::printf("LISTEN-TEST FAILED: no-token request was accepted\n");
        return 1;
    }
    std::printf("No-token request correctly refused (401).\n");

    // /status should return a 200 JSON blob (listener still running).
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool statusOk = false;
    if (s != INVALID_SOCKET) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
            const std::string req =
                "GET /status?token=" + token + " HTTP/1.1\r\nHost: x\r\n\r\n";
            send(s, req.data(), static_cast<int>(req.size()), 0);
            char buf[512]{};
            int n = recv(s, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                statusBody = QString::fromLatin1(buf, n);
                statusOk = statusBody.contains(QStringLiteral("200")) &&
                           statusBody.contains(QStringLiteral("\"port\"")) &&
                           statusBody.contains(QString::fromStdString(token));
            }
        }
        closesocket(s);
    }
    WSACleanup();
    listener.stop();
    if (!statusOk) {
        std::printf("LISTEN-TEST FAILED: /status did not answer 200 JSON\n");
        return 1;
    }
    std::printf("LISTEN-TEST OK\n");
    return 0;
}

// phoenix:// codec round trip + negative case.
int runProtoTest() {
    const std::string url = "https://example.com/a b.zip";
    const std::string name = "a b.zip";
    std::string link = UrlCodec::buildProtocolLink(url, name);
    std::printf("Protocol link: %s\n", link.c_str());
    std::string outUrl, outName;
    if (!UrlCodec::parseProtocolLink(link, outUrl, outName)) {
        std::printf("PROTO-TEST FAILED: could not parse own link\n");
        return 1;
    }
    if (outUrl != url || outName != name) {
        std::printf("PROTO-TEST FAILED: ['%s' vs '%s'] ['%s' vs '%s']\n",
                    outUrl.c_str(), url.c_str(), outName.c_str(), name.c_str());
        return 1;
    }
    std::string non = "https://ordinary.link/x.zip";
    std::string nu, nn;
    if (UrlCodec::parseProtocolLink(non, nu, nn)) {
        std::printf("PROTO-TEST FAILED: accepted a non-phoenix arg\n");
        return 1;
    }
    std::printf("PROTO-TEST OK\n");
    return 0;
}

// Pure URL-classification checks (used by the clipboard watcher).
int runUrlMatchTest() {
    if (!UrlMatcher::isDownloadUrl("https://example.com/f.zip"))
        return 1;
    if (!UrlMatcher::isDownloadUrl("http://example.com/file"))
        return 1;
    if (UrlMatcher::isDownloadUrl("ftp://example.com/f"))
        return 1;
    if (UrlMatcher::isDownloadUrl("https"))
        return 1;
    std::string got = UrlMatcher::extractFirstUrl(
        "see https://cdn.io/path/v1.2.3/App%20setup.exe and more here");
    if (got != "https://cdn.io/path/v1.2.3/App%20setup.exe") {
        std::printf("URLMATCH-TEST FAILED: extracted '%s'\n", got.c_str());
        return 1;
    }
    std::printf("URLMATCH-TEST OK\n");
    return 0;
}

// Round-trip of the persisted settings: defaults, write, reload from a fresh
// SettingsStore reading the same .ini file.
int runSettingsTest() {
    const QString ini = QString::fromStdString(tempFile("phoenix_settings_test.ini"));
    QFile::remove(ini);

    {
        SettingsStore s(ini);
        if (s.maxConcurrent() != 3 || s.defaultSegments() != 8 ||
            s.maxSpeedKBs() != 0 || s.autoAction() != 0 || s.watchClipboard()) {
            std::printf("SETTINGS-TEST FAILED: wrong defaults\n");
            return 1;
        }
        s.setDefaultDirectory(QStringLiteral("C:/Data/Downloads"));
        s.setMaxConcurrent(5);
        s.setDefaultSegments(2);
        s.setMaxSpeedKBs(512);
        s.setAutoAction(2);
        s.setWatchClipboard(true);
        s.setMainGeometry(QByteArray::fromHex("0100000001000000")); // 1,1
    }
    {
        SettingsStore s(ini);
        if (s.defaultDirectory() != QStringLiteral("C:/Data/Downloads") ||
            s.maxConcurrent() != 5 || s.defaultSegments() != 2 ||
            s.maxSpeedKBs() != 512 || s.autoAction() != 2 ||
            !s.watchClipboard()) {
            std::printf("SETTINGS-TEST FAILED: values did not persist\n");
            return 1;
        }
        if (s.mainGeometry().isEmpty()) {
            std::printf("SETTINGS-TEST FAILED: window geometry did not persist\n");
            return 1;
        }
        // Values are clamped back to widget ranges even if the file was edited.
        s.setMaxConcurrent(99);
        if (s.maxConcurrent() != 5) {
            std::printf("SETTINGS-TEST FAILED: maxConcurrent not clamped\n");
            return 1;
        }
    }
    std::printf("SETTINGS-TEST OK -> %s\n", ini.toUtf8().constData());
    QFile::remove(ini);
    return 0;
}

// Headless Native Messaging Host entry point. The browser launches
// "Phoenix.exe --native-messaging <origin>" and speaks length-prefixed JSON
// frames over stdin/stdout. If a Phoenix window is already running, "add"
// requests are handed to it over the single-instance pipe (so the download
// shows up in the visible queue); otherwise this process queues them itself.
int runNativeHost(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Phoenix"));
    app.setOrganizationDomain(QStringLiteral("phoenix.local"));
    app.setApplicationName(QStringLiteral("Phoenix"));

    DownloadQueue queue;
    SettingsStore settings;
    queue.setMaxConcurrent(settings.maxConcurrent());

    NativeHost host;
    SingleInstance single; // only used for handoff (tryActivate)
    bool handedOff = false;
    QObject::connect(&host, &NativeHost::requestReceived, &app,
                     [&](const QJsonObject& request) {
        const bool isAdd =
            request.value(QStringLiteral("type")).toString() == QStringLiteral("add");
        if (isAdd && SingleInstance::isOwnerRunning()) {
            const QString url =
                request.value(QStringLiteral("url")).toString();
            const QString name =
                request.value(QStringLiteral("fileName")).toString();
            const QUrl parsed(url);
            const QString nameOrDefault =
                name.isEmpty() ? parsed.fileName() : name;
            const std::string link = UrlCodec::buildProtocolLink(
                url.toStdString(), nameOrDefault.toStdString());
            if (single.tryActivate(QString::fromStdString(link))) {
                handedOff = true;
                host.reply({{QStringLiteral("ok"), true},
                            {QStringLiteral("method"), QStringLiteral("handoff")}});
                return;
            }
        }
        const QString dir = settings.defaultDirectory().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            : settings.defaultDirectory();
        host.reply(NativeHost::handleRequest(request, &queue, dir));
    });
    QObject::connect(&host, &NativeHost::connectionClosed, &app,
                     &QCoreApplication::quit);

    host.start();
    const int code = app.exec();
    std::fprintf(stderr, "native-host exited (handedOff=%s)\n",
                 handedOff ? "yes" : "no");
    return code;
}

// Native Messaging Host test: (1) the binary frame protocol round-trips on a
// real pipe and rejects truncation, (2) the JSON request dispatcher answers
// ping / add / status / unknown correctly.
int runNativeHostTest() {
    int argc = 1;
    char prog[] = "phoenix";
    char* argv[] = {prog};
    QCoreApplication app(argc, argv);

    // --- frame protocol over anonymous pipes ---
    HANDLE readH = INVALID_HANDLE_VALUE, writeH = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&readH, &writeH, nullptr, 0)) {
        std::printf("NATIVE-TEST FAILED: CreatePipe\n");
        return 1;
    }
    auto osf = [](HANDLE h) {
        return _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_BINARY);
    };
    std::FILE* rf = _fdopen(osf(readH), "rb");
    std::FILE* wf = _fdopen(osf(writeH), "wb");
    if (!rf || !wf) {
        if (rf)
            std::fclose(rf);
        if (wf)
            std::fclose(wf);
        std::printf("NATIVE-TEST FAILED: _fdopen\n");
        return 1;
    }

    auto writeLe = [](std::FILE* f, std::uint32_t v) {
        unsigned char b[4] = {static_cast<unsigned char>(v & 0xff),
                              static_cast<unsigned char>((v >> 8) & 0xff),
                              static_cast<unsigned char>((v >> 16) & 0xff),
                              static_cast<unsigned char>((v >> 24) & 0xff)};
        std::fwrite(b, 1, 4, f);
    };

    const std::string ping = "{\"type\":\"ping\"}";
    if (!NativeHost::writeFrame(wf, ping)) {
        std::printf("NATIVE-TEST FAILED: writeFrame\n");
        return 1;
    }
    std::string got;
    if (!NativeHost::readFrame(rf, &got) || got != ping) {
        std::printf("NATIVE-TEST FAILED: frame round trip\n");
        return 1;
    }

    // Truncated frame: length prefix promises more bytes than we send, then
    // the write side closes mid-frame -> readFrame must report failure.
    const std::string body = "{\"type\":\"add\"";
    writeLe(wf, static_cast<std::uint32_t>(body.size()) + 64);
    std::fwrite(body.data(), 1, body.size(), wf);
    std::fflush(wf);
    std::fclose(wf); // EOF with bytes still promised
    got.clear();
    if (NativeHost::readFrame(rf, &got)) {
        std::printf("NATIVE-TEST FAILED: truncated frame accepted\n");
        return 1;
    }
    std::fclose(rf);
    std::printf("Frame round trip + truncation rejection: OK\n");

    // --- JSON request dispatcher ---
    DownloadQueue queue;
    const std::string dir = tempFile("phoenix_native_dir");
    QDir().mkpath(QString::fromStdString(dir));
    const QString qDir = QString::fromStdString(dir);

    auto req = [](const char* json) {
        return QJsonDocument::fromJson(QByteArray(json)).object();
    };

    const QJsonObject pong = NativeHost::handleRequest(
        req(R"({"type":"ping"})"), &queue, qDir);
    if (!pong.value(QStringLiteral("ok")).toBool() ||
        !pong.value(QStringLiteral("pong")).toBool()) {
        std::printf("NATIVE-TEST FAILED: ping\n");
        return 1;
    }

    const QJsonObject bad = NativeHost::handleRequest(
        req(R"({"type":"add","url":""})"), &queue, qDir);
    if (bad.value(QStringLiteral("ok")).toBool()) {
        std::printf("NATIVE-TEST FAILED: empty url accepted\n");
        return 1;
    }

    const QString url = QStringLiteral("https://example.com/a b.zip");
    QString addJson = QStringLiteral(R"({"type":"add","url":"%1"})").arg(url);
    const QJsonObject added = NativeHost::handleRequest(
        req(addJson.toUtf8().constData()), &queue, qDir);
    const int addedId = added.value(QStringLiteral("id")).toInt();
    if (!added.value(QStringLiteral("ok")).toBool() || addedId <= 0) {
        std::printf("NATIVE-TEST FAILED: add\n");
        return 1;
    }

    const QJsonObject status = NativeHost::handleRequest(
        req(R"({"type":"status"})"), &queue, qDir);
    const QJsonArray items =
        status.value(QStringLiteral("items")).toArray();
    if (!status.value(QStringLiteral("ok")).toBool() || items.isEmpty() ||
        items.first().toObject().value(QStringLiteral("id")).toInt() != addedId) {
        std::printf("NATIVE-TEST FAILED: status missing the added item\n");
        return 1;
    }

    const QJsonObject unknown = NativeHost::handleRequest(
        req(R"({"type":"explode"})"), &queue, qDir);
    if (unknown.value(QStringLiteral("ok")).toBool()) {
        std::printf("NATIVE-TEST FAILED: unknown type accepted\n");
        return 1;
    }

    std::printf("NATIVE-TEST OK\n");
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-test")
            return runSelfTest();
        if (arg == "--self-test-mt")
            return runMultiSegmentTest();
        if (arg == "--self-test-resume")
            return runResumeTest();
        if (arg == "--self-test-queue")
            return runQueueTest();
        if (arg == "--self-test-schedule")
            return runScheduleTest();
        if (arg == "--self-test-sleep")
            return runSleepTest();
        if (arg == "--self-test-limit")
            return runSpeedLimitTest();
        if (arg == "--self-test-retry")
            return runRetryTest();
        if (arg == "--self-test-listen")
            return runListenTest();
        if (arg == "--self-test-proto")
            return runProtoTest();
        if (arg == "--self-test-urlmatch")
            return runUrlMatchTest();
        if (arg == "--self-test-settings")
            return runSettingsTest();
        if (arg == "--self-test-native")
            return runNativeHostTest();
        if (arg == "--native-messaging")
            return runNativeHost(argc, argv);
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
            QStringLiteral("Link catcher ready: "
                           "http://127.0.0.1:%1/add?token=%2 (and phoenix:// links)")
                .arg(port)
                .arg(listener.token()));

    w.show();
    return app.exec();
}
