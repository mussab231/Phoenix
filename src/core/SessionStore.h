#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "models/DownloadItem.h"

// Persists the in-flight part of the download queue (items that are not yet
// Completed/Failed) to a small text file so restarting Phoenix restores the
// queue and unfinished downloads continue from their resume data. STL + Win32
// only (no Qt), mirroring ResumeStore.
class SessionStore {
public:
    // %LOCALAPPDATA%\Phoenix\session.txt (empty if the folder could not be
    // resolved; the folder is created if missing).
    static std::string defaultPath();

    // Writes items that are Running / Idle / Paused. Running and Idle items
    // load back as Idle (auto-start); Paused items stay paused.
    static bool save(const std::string& path,
                     const std::vector<DownloadItem>& items);

    // Reads back the queue. ids come back as 0; state is Idle (auto-resume)
    // or Paused. Empty vector for a missing or corrupt file.
    static std::vector<DownloadItem> load(const std::string& path);

    static bool exists(const std::string& path);
    static void remove(const std::string& path);
};