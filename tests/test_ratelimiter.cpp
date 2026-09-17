#include "core/RateLimiter.h"

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <thread>

TEST_CASE("unlimited limiter consumes instantly", "[ratelimiter]") {
    RateLimiter lim(0.0);
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(lim.consume(1024 * 1024));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 500);
}

TEST_CASE("negative rate is treated as unlimited", "[ratelimiter]") {
    RateLimiter lim(-100.0);
    REQUIRE(lim.rate() == 0.0);
    REQUIRE(lim.consume(1024 * 1024));
}

TEST_CASE("rate is stored and clamped to non-negative", "[ratelimiter]") {
    RateLimiter lim(1000.0);
    REQUIRE(lim.rate() == 1000.0);
    lim.setRate(-5.0);
    REQUIRE(lim.rate() == 0.0);
}

TEST_CASE("capped rate throttles throughput", "[ratelimiter]") {
    // 2000 B/s with a ~0.5s burst cap: consuming 4 KiB must take roughly
    // 1.5-2.5s. Timing tests stay loose so a loaded CI box never flakes.
    RateLimiter lim(2000.0);
    const auto t0 = std::chrono::steady_clock::now();
    constexpr std::int64_t kBytes = 4096;
    REQUIRE(lim.consume(kBytes));
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double expected = static_cast<double>(kBytes) / 2000.0;
    REQUIRE(secs >= expected * 0.8);
    REQUIRE(secs <= expected * 3.0 + 1.0);
}

TEST_CASE("consume honours the stop flag", "[ratelimiter]") {
    // A tiny rate with a pre-set stop flag must not block.
    RateLimiter lim(1.0);
    std::atomic<bool> stop{true};
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE_FALSE(lim.consume(10 * 1000 * 1000, &stop));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);
}

TEST_CASE("zero-byte consume always succeeds", "[ratelimiter]") {
    RateLimiter lim(1.0);
    REQUIRE(lim.consume(0));
}
