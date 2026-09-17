#include "core/ResumeStore.h"

#include <catch2/catch_all.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <windows.h>

namespace {

std::string tempPath(const std::string& name) {
    char tmp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tmp);
    return std::string(tmp) + name;
}

} // namespace

TEST_CASE("ResumeStore round-trips a segmented state", "[resumestore]") {
    const std::string path = tempPath("phoenix_unit_resume.txt");
    ResumeStore::remove(path);

    ResumeData in;
    in.url = "https://example.com/file.zip";
    in.total = 1048576;
    in.etag = "\"abc123\"";
    in.lastModified = "Wed, 21 Oct 2015 07:28:00 GMT";
    in.segments = {{0, 524287, 524288}, {524288, 1048575, 262144}};
    REQUIRE(ResumeStore::save(path, in));

    const auto out = ResumeStore::load(path);
    REQUIRE(out.has_value());
    REQUIRE(out->url == in.url);
    REQUIRE(out->total == in.total);
    REQUIRE(out->etag == in.etag);
    REQUIRE(out->lastModified == in.lastModified);
    REQUIRE(out->segments.size() == 2);
    REQUIRE(out->segments[0].start == 0);
    REQUIRE(out->segments[0].end == 524287);
    REQUIRE(out->segments[0].done == 524288);
    REQUIRE(out->segments[1].done == 262144);

    ResumeStore::remove(path);
    REQUIRE_FALSE(ResumeStore::load(path).has_value());
}

TEST_CASE("ResumeStore tolerates a missing file", "[resumestore]") {
    REQUIRE_FALSE(ResumeStore::load(tempPath("phoenix_unit_missing.txt")).has_value());
}

TEST_CASE("ResumeStore rejects a corrupt file", "[resumestore]") {
    const std::string path = tempPath("phoenix_unit_corrupt.txt");
    { std::ofstream f(path, std::ios::trunc); f << "GARBAGE\nnot-a-state\n"; }
    REQUIRE_FALSE(ResumeStore::load(path).has_value());
    ResumeStore::remove(path);
}

TEST_CASE("ResumeStore statePathFor appends the suffix", "[resumestore]") {
    REQUIRE(ResumeStore::statePathFor("C:/dir/file.zip") ==
            "C:/dir/file.zip.phoenix-state");
}

TEST_CASE("ResumeStore rejects bad segment ranges", "[resumestore]") {
    const std::string path = tempPath("phoenix_unit_badseg.txt");
    { std::ofstream f(path, std::ios::trunc);
      f << "PHX2\nhttps://x.y/f.zip\n100\n1\n50 10 5\n"; } // end < start
    REQUIRE_FALSE(ResumeStore::load(path).has_value());
    ResumeStore::remove(path);
}

TEST_CASE("ResumeStore save can overwrite an existing state", "[resumestore]") {
    const std::string path = tempPath("phoenix_unit_overwrite.txt");
    ResumeData a;
    a.url = "https://a.b/one.zip";
    a.total = 100;
    a.segments = {{0, 99, 50}};
    REQUIRE(ResumeStore::save(path, a));

    ResumeData b;
    b.url = "https://a.b/two.zip";
    b.total = 200;
    b.segments = {{0, 199, 200}};
    REQUIRE(ResumeStore::save(path, b));

    const auto out = ResumeStore::load(path);
    REQUIRE(out.has_value());
    REQUIRE(out->url == "https://a.b/two.zip");
    REQUIRE(out->total == 200);
    REQUIRE(out->segments[0].done == 200);
    ResumeStore::remove(path);
}
