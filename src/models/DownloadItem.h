#pragma once

#include "models/DownloadTask.h" // DownloadState

#include <cstdint>
#include <string>

// One entry in the download queue. Idle = waiting for a free slot.
struct DownloadItem {
    int id = 0;
    // Stable cross-session identity: the int id is only unique within one
    // process, so a persisted-and-restored item or a native-host request that
    // survives a restart also carries this. Empty only for legacy items.
    std::string uuid;
    std::string url;
    std::string outputPath;
    int segments = 8;
    std::int64_t scheduledAt = 0; // epoch millis; 0 = start as soon as possible
    double maxSpeedBps = 0.0;     // 0 = unlimited
    DownloadState state = DownloadState::Idle;
    std::int64_t totalBytes = -1;
    std::int64_t receivedBytes = 0;
    double speedBps = 0.0;
    std::string statusText = "Queued";
    std::string errorMessage;
    // Optional SHA-256 (hex) the finished file must match; empty = unchecked.
    std::string expectedSha256;
};
