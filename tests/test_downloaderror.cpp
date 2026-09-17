#include "core/DownloadError.h"

#include <catch2/catch_all.hpp>

#include <stdexcept>
#include <string>

TEST_CASE("DownloadError keeps its category", "[downloaderror]") {
    const DownloadError http(DownloadError::Category::Http, "404");
    REQUIRE(http.category() == DownloadError::Category::Http);
    REQUIRE(std::string(http.what()) == "404");

    const DownloadError transient(DownloadError::Category::Transient, "timeout");
    REQUIRE(transient.category() == DownloadError::Category::Transient);
}

TEST_CASE("isTransient classifies DownloadError by category", "[downloaderror]") {
    DownloadError transient(DownloadError::Category::Transient, "short read");
    DownloadError cancelled(DownloadError::Category::Cancelled, "cancelled");
    DownloadError http(DownloadError::Category::Http, "404");
    DownloadError local(DownloadError::Category::Local, "disk full");

    REQUIRE(DownloadError::isTransient(transient));
    REQUIRE_FALSE(DownloadError::isTransient(cancelled));
    REQUIRE_FALSE(DownloadError::isTransient(http));
    REQUIRE_FALSE(DownloadError::isTransient(local));
}

TEST_CASE("isTransient defaults unknown exceptions to retryable", "[downloaderror]") {
    // A plain runtime_error carries no category: retrying is the safe default
    // (worst case we re-fetch a range we already have; the alternative is a
    // user-visible failure on a one-off socket hiccup).
    const std::runtime_error plain("connection reset");
    REQUIRE(DownloadError::isTransient(plain));
}

TEST_CASE("DownloadError is catchable as std::exception", "[downloaderror]") {
    try {
        throw DownloadError(DownloadError::Category::Local, "bad path");
    } catch (const std::exception& e) {
        REQUIRE(std::string(e.what()) == "bad path");
    }
}
