# Phoenix 🐦‍🔥 — Download Manager

A fast **multi-segment download manager for Windows**, built with **C++17**, **WinHTTP** and **Qt6**.
Goal: match — then surpass — Internet Download Manager.

> 🇸🇾 **بالعربي:** برنامج تحميل سريع لويندوز، بيقسّم الملف لعدة مقاطع وبيحمّلها بالتوازي،
> وبيدعم الاستكمال بعد الإيقاف أو انقطاع النت، مع قائمة تحميلات متعددة. مكتوب بلغة C++ ومبني على WinHTTP وQt6.

---

## ✨ Features (v0.6)

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
| Headless self-tests (`--self-test`, `-mt`, `-resume`, `-queue`, `-schedule`, `-sleep`) | ✅ |
| Speed limiter + smart retry | 🔜 v0.7 |
| Browser integration (link catching) | 🔜 v0.8 |

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

## ▶️ Run

```powershell
.\build\Phoenix.exe                    # GUI (queue table + scheduler + power actions)
.\build\Phoenix.exe --self-test        # quick HEAD + GET smoke test
.\build\Phoenix.exe --self-test-mt     # 10 MB over 8 connections + content check
.\build\Phoenix.exe --self-test-resume # forced cancel at ~2 MB, then resume
.\build\Phoenix.exe --self-test-queue  # two downloads at once through the queue
.\build\Phoenix.exe --self-test-schedule # starts a download at +2.5s (verifies it waits)
.\build\Phoenix.exe --self-test-sleep  # queue finishes -> "sleep" action fires (no-op in test)
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

## 📄 License

TBD — not chosen yet.
