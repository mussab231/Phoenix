#include "core/Checksum.h"

#include <catch2/catch_all.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <windows.h>

namespace {

std::string writeTemp(const std::string& name, const std::string& bytes) {
    char tmp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tmp);
    const std::string path = std::string(tmp) + name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path;
}

} // namespace

TEST_CASE("sha256File of empty string", "[checksum]") {
    // Known vector: SHA-256("") = e3b0c44298fc1c149afbf4c8996fb924...
    const std::string path = writeTemp("phoenix_unit_empty.bin", "");
    REQUIRE(Checksum::sha256File(path) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("sha256File of 'abc'", "[checksum]") {
    const std::string path = writeTemp("phoenix_unit_abc.bin", "abc");
    REQUIRE(Checksum::sha256File(path) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256File is stable across reads", "[checksum]") {
    const std::string path = writeTemp("phoenix_unit_rep.bin", "Phoenix!");
    const std::string a = Checksum::sha256File(path);
    const std::string b = Checksum::sha256File(path);
    REQUIRE(a == b);
    REQUIRE_FALSE(a.empty());
}

TEST_CASE("sha256File distinguishes different content", "[checksum]") {
    const std::string p1 = writeTemp("phoenix_unit_d1.bin", "data v1");
    const std::string p2 = writeTemp("phoenix_unit_d2.bin", "data v2");
    REQUIRE(Checksum::sha256File(p1) != Checksum::sha256File(p2));
}

TEST_CASE("sha256File returns empty for missing file", "[checksum]") {
    REQUIRE(Checksum::sha256File("C:/does/not/exist/nope.bin").empty());
}

TEST_CASE("hexEquals is case-insensitive and whitespace-tolerant", "[checksum]") {
    const std::string h = "ba7816bf8f01cfea414140de5dae2223";
    REQUIRE(Checksum::hexEquals(h, h));
    REQUIRE(Checksum::hexEquals(h, "BA7816BF8F01CFEA414140DE5DAE2223"));
    // Whitespace anywhere in the digest is ignored (a pasted hash with a
    // trailing newline or wrapped lines still matches).
    REQUIRE(Checksum::hexEquals(h, " ba7816bf8f01cfea414140de5dae2223\n"));
    REQUIRE(Checksum::hexEquals(h, "ba7816bf 8f01cfea 414140de 5dae2223"));
    REQUIRE_FALSE(Checksum::hexEquals(h, "ba7816bf8f01cfea414140de5dae2224"));
    REQUIRE_FALSE(Checksum::hexEquals(h, ""));
    REQUIRE_FALSE(Checksum::hexEquals("", h));
}

TEST_CASE("hexEquals rejects different lengths", "[checksum]") {
    REQUIRE_FALSE(Checksum::hexEquals("ab", "abc"));
}
