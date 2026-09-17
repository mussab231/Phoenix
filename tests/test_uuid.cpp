#include "core/Uuid.h"

#include <catch2/catch_all.hpp>

#include <cctype>
#include <regex>
#include <set>
#include <string>

TEST_CASE("Uuid matches the v4 layout", "[uuid]") {
    // 8-4-4-4-12 lowercase hex; the version nibble is 4 and the variant bits
    // are 8/9/a/b.
    const std::string u = Uuid::create();
    static const std::regex re(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
        std::regex::icase);
    REQUIRE(std::regex_match(u, re));
}

TEST_CASE("Uuid is lowercase", "[uuid]") {
    const std::string u = Uuid::create();
    REQUIRE(u.size() == 36);
    for (const char c : u)
        REQUIRE_FALSE(std::isupper(static_cast<unsigned char>(c)));
}

TEST_CASE("Uuids are unique across many draws", "[uuid]") {
    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i)
        seen.insert(Uuid::create());
    REQUIRE(seen.size() == 2000);
}
