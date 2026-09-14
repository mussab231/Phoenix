#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Shared token bucket used to cap the aggregate download speed across all
// segmented connections at once. rate() == 0 means unlimited.
// STL only, so it lives fine next to the WinHTTP core.
class RateLimiter {
public:
    explicit RateLimiter(double bytesPerSecond = 0.0);

    void setRate(double bytesPerSecond);
    double rate() const;

    // Blocks until `bytes` tokens are available. Returns false early when
    // stopFlag becomes set (so a slow download stays cancelable).
    bool consume(std::int64_t bytes, const std::atomic<bool>* stopFlag = nullptr);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    double m_rate = 0.0; // bytes per second
    double m_tokens = 0.0;
    std::chrono::steady_clock::time_point m_last;
};