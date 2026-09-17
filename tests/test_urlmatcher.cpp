#include "core/UrlMatcher.h"

#include <catch2/catch_all.hpp>

#include <string>

TEST_CASE("isDownloadUrl accepts http and https", "[urlmatcher]") {
    REQUIRE(UrlMatcher::isDownloadUrl("http://example.com/f.zip"));
    REQUIRE(UrlMatcher::isDownloadUrl("https://example.com/f.zip"));
    REQUIRE(UrlMatcher::isDownloadUrl("HTTPS://EXAMPLE.COM/f.zip"));
}

TEST_CASE("isDownloadUrl rejects non-urls", "[urlmatcher]") {
    REQUIRE_FALSE(UrlMatcher::isDownloadUrl("ftp://example.com/f.zip"));
    REQUIRE_FALSE(UrlMatcher::isDownloadUrl("example.com/f.zip"));
    REQUIRE_FALSE(UrlMatcher::isDownloadUrl(""));
    REQUIRE_FALSE(UrlMatcher::isDownloadUrl("http:/example.com"));
    // Scheme prefix alone is not a URL.
    REQUIRE_FALSE(UrlMatcher::isDownloadUrl("https://"));
}

TEST_CASE("extractFirstUrl finds the first token", "[urlmatcher]") {
    REQUIRE(UrlMatcher::extractFirstUrl("see https://a.b/f.zip now") ==
            "https://a.b/f.zip");
    REQUIRE(UrlMatcher::extractFirstUrl("\"https://a.b/f.zip\"") ==
            "https://a.b/f.zip");
    // A stop at a quote keeps the URL clean.
    REQUIRE(UrlMatcher::extractFirstUrl("<a href='https://a.b/f.zip'>x</a>") ==
            "https://a.b/f.zip");
}

TEST_CASE("extractFirstUrl ignores plain text", "[urlmatcher]") {
    REQUIRE(UrlMatcher::extractFirstUrl("no links here").empty());
    REQUIRE(UrlMatcher::extractFirstUrl("").empty());
}

TEST_CASE("extractFirstUrl picks the first of several", "[urlmatcher]") {
    const std::string text = "a https://first/x.zip and https://second/y.zip";
    REQUIRE(UrlMatcher::extractFirstUrl(text) == "https://first/x.zip");
}

TEST_CASE("extractFirstUrl keeps query strings and fragments", "[urlmatcher]") {
    REQUIRE(UrlMatcher::extractFirstUrl("go https://a.b/f.zip?tok=1#frag end") ==
            "https://a.b/f.zip?tok=1#frag");
}
