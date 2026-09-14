#include "core/RateLimiter.h"

#include <algorithm>

namespace {

constexpr double kMinChunk = 128.0 * 1024.0; // 128 KiB floor for the burst cap

} // namespace

RateLimiter::RateLimiter(double bytesPerSecond)
    : m_rate(bytesPerSecond < 0 ? 0.0 : bytesPerSecond),
      m_last(std::chrono::steady_clock::now()) {}

double RateLimiter::rate() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rate;
}

void RateLimiter::setRate(double bytesPerSecond) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rate = bytesPerSecond < 0 ? 0.0 : bytesPerSecond;
        if (m_rate <= 0.0)
            m_tokens = 0.0; // unlimited: no point keeping a full bucket
    }
    m_cv.notify_all();
}

bool RateLimiter::consume(std::int64_t bytes, const std::atomic<bool>* stopFlag) {
    if (bytes <= 0)
        return true;
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_rate <= 0.0)
        return true; // unlimited

    for (;;) {
        if (stopFlag && stopFlag->load())
            return false;

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - m_last).count();
        m_last = now;
        m_tokens += m_rate * dt;

        const double cap = std::max(m_rate * 0.5, kMinChunk);
        if (m_tokens > cap)
            m_tokens = cap;

        if (m_tokens >= static_cast<double>(bytes)) {
            m_tokens -= static_cast<double>(bytes);
            return true;
        }

        const double deficitS =
            (static_cast<double>(bytes) - m_tokens) / m_rate;
        const auto slice = std::min<long long>(
            50, static_cast<long long>(deficitS * 1000.0));
        m_cv.wait_for(lock, std::chrono::milliseconds(std::max<long long>(1, slice)));
    }
}