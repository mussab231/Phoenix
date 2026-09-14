# Phoenix 🐦‍🔥 — Download Manager

[![CI](https://github.com/mussab231/Phoenix/actions/workflows/ci.yml/badge.svg)](https://github.com/mussab231/Phoenix/actions/workflows/ci.yml)

A fast **multi-segment download manager for Windows**, built with **C++17**, **WinHTTP** and **Qt6**.
Goal: match — then surpass — Internet Download Manager.

> 🇸🇾 **بالعربي:** برنامج تحميل سريع لويندوز، بيقسّم الملف لعدة مقاطع وبيحمّلها بالتوازي،
> وبيدعم الاستكمال بعد الإيقاف أو انقطاع النت، مع قائمة تحميلات متعددة. مكتوب بلغة C++ ومبني على WinHTTP وQt6.
> من الإصدار 0.8 يدعم التقاط الروابط تلقائياً عبر `phoenix://` ومحلي `http://127.0.0.1:51047` ومراقبة الحافظة.

---

## ✨ Features (v0.8)

| Feature | Status |
|---------|--------|
| Parallel segmented downloads (1–16 connections) | ✅ |
| Automatic resume after pause / crash / network loss (`.phoenix-state`) | ✅ |
| Download queue: several files at once, configurable concurrency (1–5) | ✅ |
| Per-download pause / resume / remove, live speed per item | ✅ |
| Scheduler: start a download at a chosen date/time (queued as "Scheduled …") | ✅ |
| When done: sleep / hibernate / shutdown the PC automatically | ✅ |
| Modern dark UI (Qt Style Sheets, Phoenix-orange accents, live status bar) | ✅ |
| Phoenix flame app icon: window + taskbar + embedded in `.exe` | ✅ |
| Smart fallback to single connection when server ignores `Range` | ✅ |
| Byte-for-byte verified downloads (segmented == single-connection) | ✅ |
| **Speed limiter** (per download, "Max speed" in Add dialog, shared across segments) | ✅ |
| **Smart retry**: transient errors are retried with exponential backoff (per-segment + whole-call), persistent errors (404/disk/cancel) never retried | ✅ |
| **System tray + background mode**: close hides to tray, right-click menu, clipboard watcher | ✅ |
| **Link catching via `phoenix://` protocol**: registered silently in HKCU on first run | ✅ |
| **Localhost HTTP listener** (`http://127.0.0.1:51047/add?url=...`): add links without the protocol handler | ✅ |
| **Clipboard watcher** (opt-in from tray menu): detects copied download URLs and offers to add them | ✅ |
| Single-instance: a running Phoenix receives `phoenix://` links from new OS-launched instances | ✅ |
| Headless self-tests (`--self-test`, `-mt`, `-resume`, `-queue`, `-schedule`, `-sleep`, `-limit`, `-retry`, `-listen`, `-proto`, `-urlmatch`) | ✅ |

## 🛠️ Requirements

- Windows 10/11 64-bit
- [MSYS2](https://www.msys2.org/) with MinGW-w64 toolchain:
  `mingw-w64-x86_64-gcc`, `cmake`, `ninja`, `make`, `gdb`
- Qt6: `mingw-w64-x86_64-qt6-base`, `mingw-w64-x86_64-qt6-tools`, `mingw-w64-x86_64-qt-creator`

## 🔨 Build

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=C:/msys64/mingw64"
cmake --build build
```

Or open the folder in VS Code (`F5` → *Run Phoenix (GUI)*) — tasks are preconfigured in `.vscode/`.

> **CI:** every push to `main` builds Phoenix in a clean MSYS2/MinGW+Qt6 Windows
> environment and runs the whole headless self-test suite (`.github/workflows/ci.yml`).

## ▶️ Run

```powershell
.\build\Phoenix.exe                    # GUI (queue table + scheduler + power actions)
.\build\Phoenix.exe --self-test        # quick HEAD + GET smoke test
.\build\Phoenix.exe --self-test-mt     # 10 MB over 8 connections + content check
.\build\Phoenix.exe --self-test-resume # forced cancel at ~2 MB, then resume
.\build\Phoenix.exe --self-test-queue  # two downloads at once through the queue
.\build\Phoenix.exe --self-test-schedule # starts a download at +2.5s (verifies it waits)
.\build\Phoenix.exe --self-test-sleep  # queue finishes -> "sleep" action fires (no-op in test)
.\build\Phoenix.exe --self-test-limit   # 10 MB capped at 1.5 MiB/s (verifies the limiter throttles)
.\build\Phoenix.exe --self-test-retry   # injected transient errors -> recovery (whole-call + per-segment)
.\build\Phoenix.exe --self-test-listen  # local HTTP listener (/add + /status round-trip)
.\build\Phoenix.exe --self-test-proto   # phoenix:// encode/decode round trip
.\build\Phoenix.exe --self-test-urlmatch # URL classification for clipboard catching
.\build\Phoenix.exe --register         # register phoenix:// handler in HKCU (also done on first GUI launch)
.\build\Phoenix.exe --unregister       # remove the protocol registration
```

## 🗂️ Project layout

```
Phoenix/
├── CMakeLists.txt
├── Phoenix.qrc               # icon PNGs + theme.qss bundled into the binary
├── assets/
│   ├── phoenix.svg           # source artwork (edit this, regenerate the rest)
│   ├── phoenix.ico           # embedded into Phoenix.exe (src/app.rc.in)
│   └── icons/                # phoenix-16..256.png (multi-size icon set)
├── src/
│   ├── main.cpp              # entry point + self-tests + theme setup
│   ├── core/
│   │   ├── HttpClient.*      # WinHTTP wrapper (size probe, GET, Range, segments)
│   │   ├── ResumeStore.*     # .phoenix-state persistence (pause/crash recovery)
│   │   ├── DownloadEngine.*  # one download on a worker thread + Qt signals
│   │   ├── DownloadQueue.*   # coordinates several engines (concurrency limit, scheduler)
│   │   ├── RateLimiter.*     # shared token-bucket speed limiter for the segment workers
│   │   ├── SingleInstance.*   # QLocalServer pipe: forwards phoenix:// links to the running instance
│   │   ├── HttpListener.*    # loopback HTTP server (127.0.0.1) for browser/link-catching
│   │   ├── ClipboardWatcher.*# polls clipboard for download URLs, emits urlDetected
│   │   ├── UrlCodec.*        # phoenix:// protocol: percent-encode/decode, build + parse
│   │   ├── UrlMatcher.*      # URL classification (isDownloadUrl, extractFirstUrl)
│   │   ├── ProtocolRegistrar.* # registers phoenix:// handler in HKCU on first launch
│   │   └── PowerControl.*    # sleep / hibernate / shutdown (test mode built in)
│   ├── models/
│   │   ├── DownloadTask.h
│   │   └── DownloadItem.h    # one queue entry
│   └── gui/
│       ├── MainWindow.*      # queue table + controls + status bar
│       ├── AddDialog.*       # new-download dialog
│       └── theme.qss         # dark Phoenix style sheet
├── tools/
│   └── make_icon/            # dev tool: SVG -> PNG sizes + .ico (needs qt6-svg)
└── .vscode/                  # build / run / debug tasks
```

## 🐦‍🔥 Changing the icon

1. Edit `assets/phoenix.svg` (the flame-bird artwork).
2. Regenerate the icon set from MSYS2:

   ```powershell
   cmake -S tools/make_icon -B tools/make_icon/build -G Ninja "-DCMAKE_PREFIX_PATH=C:/msys64/mingw64"
   cmake --build tools/make_icon/build   # runs make_icon -> rewrites assets/icons/*.png + phoenix.ico
   ```

3. Rebuild Phoenix: the window/taskbar icon comes from the PNGs via `Phoenix.qrc`, and the `.exe` file icon from `phoenix.ico` via `src/app.rc.in`. Done.

> Needs `mingw-w64-x86_64-qt6-svg` (already installed). To use your *own* image instead of the SVG, drop a square PNG over `assets/icons/phoenix-256.png` and re-add the other sizes.

## 🧭 Platform & design decisions

- **Windows-native** by choice, not by accident (v0.2+). Phoenix uses **WinHTTP**
  directly for the HTTP layer and **Winsock** for the loopback link-catcher —
  no network stack abstraction layer. That buys first-class Windows behaviour
  (proxy config, TLS, HttpApi-style resume semantics) at the price of
  Windows-only portability. The core (queue, scheduler, rate limiter, resume
  store, tests) is deliberately Qt-independent so it could ride on another
  transport (libcurl… ) behind `HttpClient` in a future port.
- **Qt6 only for the GUI shell** — the entire download engine is pure
  C++17/WinAPI and is covered by headless self-tests that run without a GUI
  or a display server.
- **`phoenix://` protocol + loopback listener + clipboard watcher** are three
  complementary link-catching paths so Phoenix never depends on a specific
  browser or store. The loopback HTTP server binds `127.0.0.1` only — nothing
  is exposed to the network.
- **Resume via sidecar state** (`.phoenix-state`): pause, crash or a forced
  kill never loses progress; segments resume from their recorded offsets.
- **Self-contained test strategy**: every feature lands with a `--self-test-*`
  flag (see below) so the same binary that runs on your desk runs on CI.

## 📄 License

Proprietary — **all rights reserved** (no license file, no grant of use).
You may browse, fork-for-review and run the app yourself, but you may **not**
redistribute, sell, or reuse this code (in whole or in part, including in
derivative or closed-source projects) without written permission from the
author.

Before contributing, read the license note above: by opening a pull request
you agree that your contribution is offered under the same terms.
