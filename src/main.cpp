#include "core/DownloadQueue.h"
#include "core/HttpClient.h"
#include "core/PowerControl.h"
#include "gui/MainWindow.h"
#include "models/DownloadItem.h"

#include <windows.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

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
    }
    QApplication app(argc, argv);
    applyTheme(app);

    QIcon windowIcon;
    for (int s : {16, 24, 32, 48, 64, 128, 256})
        windowIcon.addFile(QStringLiteral(":/icons/phoenix-%1.png").arg(s));
    app.setWindowIcon(windowIcon);

    MainWindow w;
    w.show();
    return app.exec();
}
