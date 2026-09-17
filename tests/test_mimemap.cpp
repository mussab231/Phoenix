#include "core/MimeMap.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("hasExtension", "[mimemap]") {
    using namespace MimeMap;
    REQUIRE(hasExtension("setup.exe"));
    REQUIRE(hasExtension("archive.tar.gz"));
    REQUIRE_FALSE(hasExtension("noext"));
    REQUIRE_FALSE(hasExtension(""));
    REQUIRE_FALSE(hasExtension(".bashrc")); // dotfile, not a typed name
}

TEST_CASE("percentDecode", "[mimemap]") {
    using namespace MimeMap;
    REQUIRE(percentDecode("plain") == "plain");
    REQUIRE(percentDecode("a%41b") == "aAb");
    REQUIRE(percentDecode("%E2%82%AC") == "\xE2\x82\xAC"); // euro sign, UTF-8
    // Truncated/invalid escapes must pass through instead of swallowing bytes.
    REQUIRE(percentDecode("100%") == "100%");
    REQUIRE(percentDecode("%zz") == "%zz");
}

TEST_CASE("toLower is ASCII-only", "[mimemap]") {
    using namespace MimeMap;
    REQUIRE(toLower("Video/MP4") == "video/mp4");
    // UTF-8 high bytes are left untouched (no Turkish-i style surprises).
    const std::string utf8 = "\xC3\x9C";
    REQUIRE(toLower(utf8) == utf8);
}

TEST_CASE("dispositionField parses quoted and bare values", "[mimemap]") {
    using namespace MimeMap;
    const std::string h = R"(attachment; filename="report.pdf"; size=1234)";
    REQUIRE(dispositionField(h, "filename=") == "report.pdf");
    REQUIRE(dispositionField(h, "size=") == "1234");
    REQUIRE(dispositionField(h, "missing=") == "");
    // Escaped quote inside a quoted value is kept literally.
    REQUIRE(dispositionField(R"(attachment; filename="a\"b")", "filename=") ==
            "a\"b");
    // Bare (unquoted) token stops at the semicolon.
    REQUIRE(dispositionField("attachment; filename=notes.txt; x=y", "filename=") ==
            "notes.txt");
}

TEST_CASE("parseContentDispositionFilename prefers RFC 5987", "[mimemap]") {
    using namespace MimeMap;
    // The encoded form is the only one able to carry non-ASCII names.
    REQUIRE(parseContentDispositionFilename(
                "attachment; filename*=UTF-8''%E2%82%AC%20tip.pdf") ==
            "\xE2\x82\xAC tip.pdf");
    // Falls back to the legacy quoted form when the encoded one is absent.
    REQUIRE(parseContentDispositionFilename(
                R"(attachment; filename="movie.mkv")") == "movie.mkv");
    // And to a bare token.
    REQUIRE(parseContentDispositionFilename("attachment; filename=movie.mkv") ==
            "movie.mkv");
    REQUIRE(parseContentDispositionFilename("inline") == "");
}

TEST_CASE("mimeToExtension covers common types", "[mimemap]") {
    using namespace MimeMap;
    REQUIRE(mimeToExtension("video/mp4") == "mp4");
    REQUIRE(mimeToExtension("VIDEO/X-MATROSKA") == "mkv");
    REQUIRE(mimeToExtension("application/x-msdownload") == "exe");
    REQUIRE(mimeToExtension("application/vnd.microsoft.portable-executable") ==
            "exe");
    REQUIRE(mimeToExtension("application/zip") == "zip");
    REQUIRE(mimeToExtension("application/pdf") == "pdf");
    // Parameters are stripped before lookup.
    REQUIRE(mimeToExtension("text/html; charset=utf-8") == "html");
    // Unknown / uninformative types yield nothing rather than a wrong guess.
    REQUIRE(mimeToExtension("application/octet-stream") == "");
    REQUIRE(mimeToExtension("application/x-unknown") == "");
    REQUIRE(mimeToExtension("") == "");
}

TEST_CASE("urlFilename takes the last path segment", "[mimemap]") {
    using namespace MimeMap;
    REQUIRE(urlFilename("https://cdn.example.com/files/setup.exe") == "setup.exe");
    REQUIRE(urlFilename("https://example.com/a/b/movie.mkv?token=[REDACTED]") ==
            "movie.mkv");
    REQUIRE(urlFilename("https://example.com/d#frag") == "d");
    // Percent-encoded names are decoded.
    REQUIRE(urlFilename("https://example.com/my%20file.zip") == "my file.zip");
    // Root or query-only URLs carry no name. The host itself is never
    // mistaken for a filename ("https://example.com" -> "").
    REQUIRE(urlFilename("https://example.com") == "");
    REQUIRE(urlFilename("https://example.com/") == "");
    REQUIRE(urlFilename("https://example.com/?id=1234") == "");
    // A scheme-less path still resolves by its last segment.
    REQUIRE(urlFilename("files/setup.exe") == "setup.exe");
}
