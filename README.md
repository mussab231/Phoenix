# Phoenix 🐦‍🔥 — Download Manager

A fast **multi-segment download manager for Windows**, built with **C++17**, **WinHTTP** and **Qt6**.
Goal: match — then surpass — Internet Download Manager.

> 🇸🇾 **بالعربي:** برنامج تحميل سريع لويندوز، بيقسّم الملف لعدة مقاطع وبيحمّلها بالتوازي،
> وبيدعم الاستكمال بعد الإيقاف أو انقطاع النت. مكتوب بلغة C++ ومبني على WinHTTP وQt6.

---

## ✨ Features (v0.3)

| Feature | Status |
|---------|--------|
| Parallel segmented downloads (1–16 connections) | ✅ |
| Automatic resume after pause / crash / network loss (`.phoenix-state`) | ✅ |
| Smart fallback to single connection when server ignores `Range` | ✅ |
| Qt GUI: progress bar, live speed, connection count | ✅ |
| Byte-for-byte verified downloads (segmented == single-connection) | ✅ |
| Headless self-tests (`--self-test`, `--self-test-mt`, `--self-test-resume`) | ✅ |
| Download queue (multiple files) | 🔜 v0.4 |
| Scheduler + auto-shutdown on completion | 🔜 v0.5 |
| Speed limiter + smart retry | 🔜 v0.6 |
| Browser integration (link catching) | 🔜 v0.7 |

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
.\build\Phoenix.exe                  # GUI
.\build\Phoenix.exe --self-test        # quick HEAD + GET smoke test
.\build\Phoenix.exe --self-test-mt     # 10 MB over 8 connections + content check
.\build\Phoenix.exe --self-test-resume # forced cancel at ~2 MB, then resume
```

## 🗂️ Project layout

```
Phoenix/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # entry point + self-tests
│   ├── core/
│   │   ├── HttpClient.*      # WinHTTP wrapper (size probe, GET, Range, segments)
│   │   ├── ResumeStore.*     # .phoenix-state persistence (pause/crash recovery)
│   │   └── DownloadEngine.*  # worker thread + Qt signals
│   ├── models/
│   │   └── DownloadTask.h
│   └── gui/
│       └── MainWindow.*      # Qt Widgets interface
└── .vscode/                  # build / run / debug tasks
```

## 📄 License

TBD — not chosen yet.
