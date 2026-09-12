#pragma once

#include <cstdint>
#include <string>

enum class DownloadState {
    Idle,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct DownloadTask {
    std::string url;
    std::string outputPath;
    std::int64_t totalBytes = -1;    // -1 = server did not report a size
    std::int64_t receivedBytes = 0;
    DownloadState state = DownloadState::Idle;
    std::string errorMessage;
};
