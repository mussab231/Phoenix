#include "core/HttpClient.h"
#include "gui/MainWindow.h"

#include <windows.h>

#include <QApplication>

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
        const std::string url = "https://cachefly.cachefly.net/10mb.test";
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
    const std::string url = "https://cachefly.cachefly.net/10mb.test";
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--self-test")
            return runSelfTest();
        if (arg == "--self-test-mt")
            return runMultiSegmentTest();
        if (arg == "--self-test-resume")
            return runResumeTest();
    }
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
