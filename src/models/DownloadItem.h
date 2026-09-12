#pragma once

#include "models/DownloadTask.h" // DownloadState

#include <cstdint>
#include <string>

// One entry in the download queue. Idle = waiting for a free slot.
struct DownloadItem {
    int id = 0;
    std::string url;
    std::string outputPath;
    int segments = 8;
    DownloadState state = DownloadState::Idle;
    std::int64_t totalBytes = -1;
    std::int64_t receivedBytes = 0;
    double speedBps = 0.0;
    std::string statusText = "Queued";
    std::string errorMessage;
};
