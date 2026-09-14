#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class RateLimiter;

// Thin WinHTTP wrapper. Pure Win32 + STL, no Qt dependency.
class HttpClient {
public:
    using ProgressCallback = std::function<void(std::int64_t received, std::int64_t total)>;

    // Test-only hook: while > 0, the next segment download attempt throws a
    // "simulated transient error" once (used by --self-test-retry).
    static std::atomic<int> g_testFakeFailures;

    // HEAD request. Returns file size, or -1 if the server did not send one.
    // Throws std::runtime_error on failure.
    static std::int64_t getFileSize(const std::string& url);

    // Probes Range support with a 1-byte request (expects HTTP 206).
    static bool supportsRanges(const std::string& url);

    // Parallel segmented download. Splits the file into numSegments ranges
    // (clamped to 1..16), preallocates the output file, and downloads every
    // segment on its own thread/connection. Falls back to a single
    // connection when the size is unknown or the server ignores Range.
    //
    // limiter (optional) caps the aggregate throughput across all segments;
    // maxAttempts/baseRetryMs enable smart retry for transient errors only
    // (cancellations, HTTP status errors, disk failures are never retried).
    // Throws std::runtime_error on failure or cancellation.
    static void downloadSegmented(const std::string& url,
                                  const std::string& outputPath,
                                  ProgressCallback progress = {},
                                  const std::atomic<bool>* stopFlag = nullptr,
                                  int numSegments = 8,
                                  RateLimiter* limiter = nullptr,
                                  int maxAttempts = 1,
                                  int baseRetryMs = 1000);

    // GET download. Optional byte range [rangeStart, rangeEnd] (inclusive,
    // -1 = open end). Data is written at file offset writeOffset so segments
    // can share one preallocated file. truncate=false is REQUIRED for segment
    // workers (even at offset 0) so they never wipe the shared file.
    // Throws std::runtime_error on failure or cancellation.
    static void download(const std::string& url,
                         const std::string& outputPath,
                         ProgressCallback progress = {},
                         const std::atomic<bool>* stopFlag = nullptr,
                         std::int64_t rangeStart = -1,
                         std::int64_t rangeEnd = -1,
                         std::int64_t writeOffset = 0,
                         bool truncate = true,
                         RateLimiter* limiter = nullptr);

private:
    // One pass of the segmented machinery. Called repeatedly by
    // downloadSegmented until it succeeds or maxAttempts is exhausted.
    static void segmentedCore(const std::string& url,
                              const std::string& outputPath,
                              ProgressCallback progress,
                              const std::atomic<bool>* stopFlag,
                              int numSegments,
                              RateLimiter* limiter);
};
