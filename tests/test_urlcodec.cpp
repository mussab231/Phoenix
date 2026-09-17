#include "core/UrlCodec.h"

#include <catch2/catch_all.hpp>

#include <string>

TEST_CASE("UrlCodec round-trips plain ASCII", "[urlcodec]") {
    const std::string in = "https://example.com/file.zip";
    REQUIRE(UrlCodec::decode(UrlCodec::encode(in)) == in);
}

TEST_CASE("UrlCodec encodes reserved characters", "[urlcodec]") {
    // Spaces and non-ASCII must be percent-encoded; unreserved stay literal.
    REQUIRE(UrlCodec::encode("a b") == "a%20b");
    REQUIRE(UrlCodec::encode("a-b_c.d~e") == "a-b_c.d~e");
    REQUIRE(UrlCodec::encode("https://x.y/z?a=1&b=2") ==
            "https%3A%2F%2Fx.y%2Fz%3Fa%3D1%26b%3D2");
}

TEST_CASE("UrlCodec decodes %XX and leaves '+' literal", "[urlcodec]") {
    REQUIRE(UrlCodec::decode("a%20b") == "a b");
    REQUIRE(UrlCodec::decode("a+b") == "a+b");
    REQUIRE(UrlCodec::decode("%C3%A9") == "\xC3\xA9"); // UTF-8 e-acute
    // Truncated/invalid escapes pass through unchanged.
    REQUIRE(UrlCodec::decode("100%") == "100%");
    REQUIRE(UrlCodec::decode("%ZZ") == "%ZZ");
}

TEST_CASE("UrlCodec round-trips unicode and query", "[urlcodec]") {
    const std::string in = "https://example.com/a b.zip?name=\xD9\x85\xD9\x84\xD9\x81";
    const std::string enc = UrlCodec::encode(in);
    REQUIRE(UrlCodec::decode(enc) == in);
}

TEST_CASE("phoenix:// link round trip with name", "[urlcodec]") {
    const std::string link =
        UrlCodec::buildProtocolLink("https://x.y/a b.zip", "my file.zip");
    REQUIRE(link.rfind("phoenix://", 0) == 0);
    std::string url, name;
    REQUIRE(UrlCodec::parseProtocolLink(link, url, name));
    REQUIRE(url == "https://x.y/a b.zip");
    REQUIRE(name == "my file.zip");
}

TEST_CASE("phoenix:// link without name", "[urlcodec]") {
    const std::string link = UrlCodec::buildProtocolLink("https://x.y/f.zip");
    std::string url, name;
    REQUIRE(UrlCodec::parseProtocolLink(link, url, name));
    REQUIRE(url == "https://x.y/f.zip");
    REQUIRE(name.empty());
}

TEST_CASE("parseProtocolLink rejects non-phoenix input", "[urlcodec]") {
    std::string url, name;
    REQUIRE_FALSE(UrlCodec::parseProtocolLink("https://x.y/f.zip", url, name));
    REQUIRE_FALSE(UrlCodec::parseProtocolLink("", url, name));
    REQUIRE_FALSE(UrlCodec::parseProtocolLink("phoenix:/", url, name));
}

TEST_CASE("parseProtocolLink decodes embedded question mark", "[urlcodec]") {
    // A '?' inside the encoded URL must survive as data, not a separator.
    const std::string link =
        UrlCodec::buildProtocolLink("https://x.y/s?a=1", "n.zip");
    std::string url, name;
    REQUIRE(UrlCodec::parseProtocolLink(link, url, name));
    REQUIRE(url == "https://x.y/s?a=1");
    REQUIRE(name == "n.zip");
}
