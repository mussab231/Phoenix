#include "core/SessionStore.h"
#include "models/DownloadItem.h"

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

DownloadItem makeItem(const std::string& url, const std::string& out, DownloadState state) {
    DownloadItem it;
    it.id = 1;
    it.uuid = "f47ac10b-58cc-4372-a567-0e02b2c3d479";
    it.url = url;
    it.outputPath = out;
    it.segments = 8;
    it.scheduledAt = 0;
    it.maxSpeedBps = 0.0;
    it.state = state;
    return it;
}

} // namespace

TEST_CASE("SessionStore round-trips a live queue", "[sessionstore]") {
    const std::string path = tempPath("phoenix_unit_session.txt");
    SessionStore::remove(path);

    std::vector<DownloadItem> in;
    in.push_back(makeItem("https://a.b/one.zip", "C:/dl/one.zip", DownloadState::Idle));
    in.push_back(makeItem("https://a.b/two.zip", "C:/dl/two.zip", DownloadState::Paused));
    in.push_back(makeItem("https://a.b/three.zip", "C:/dl/three.zip", DownloadState::Running));
    in[2].expectedSha256 = "66f77f71710117f71712eb5766f3151e8aecec390ebe0b0349c84961f1a547e6";
    in[2].segments = 4;
    in[2].maxSpeedBps = 512000.0;

    REQUIRE(SessionStore::save(path, in));
    REQUIRE(SessionStore::exists(path));

    const auto out = SessionStore::load(path);
    REQUIRE(out.size() == 3);
    REQUIRE(out[0].url == "https://a.b/one.zip");
    // Idle/Running reload as auto-start (Idle).
    REQUIRE(out[0].state == DownloadState::Idle);
    REQUIRE(out[1].state == DownloadState::Paused);
    REQUIRE(out[2].state == DownloadState::Idle);
    // The expected hash survives the round trip.
    REQUIRE(out[2].expectedSha256 ==
            "66f77f71710117f71712eb5766f3151e8aecec390ebe0b0349c84961f1a547e6");
    REQUIRE(out[2].segments == 4);
    REQUIRE(out[2].maxSpeedBps == 512000.0);

    SessionStore::remove(path);
    REQUIRE_FALSE(SessionStore::exists(path));
}

TEST_CASE("SessionStore drops completed and failed items", "[sessionstore]") {
    const std::string path = tempPath("phoenix_unit_session_drop.txt");
    SessionStore::remove(path);

    std::vector<DownloadItem> in;
    in.push_back(makeItem("https://a.b/done.zip", "C:/dl/done.zip", DownloadState::Completed));
    in.push_back(makeItem("https://a.b/err.zip", "C:/dl/err.zip", DownloadState::Failed));
    in.push_back(makeItem("https://a.b/live.zip", "C:/dl/live.zip", DownloadState::Idle));

    REQUIRE(SessionStore::save(path, in));
    const auto out = SessionStore::load(path);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].url == "https://a.b/live.zip");
    SessionStore::remove(path);
}

TEST_CASE("SessionStore load on missing file is empty", "[sessionstore]") {
    const std::string path = tempPath("phoenix_unit_session_missing.txt");
    SessionStore::remove(path);
    REQUIRE(SessionStore::load(path).empty());
    REQUIRE_FALSE(SessionStore::exists(path));
}

TEST_CASE("SessionStore rejects a corrupt file", "[sessionstore]") {
    const std::string path = tempPath("phoenix_unit_session_bad.txt");
    { std::ofstream f(path, std::ios::trunc); f << "JUNK\nmore junk\n"; }
    REQUIRE(SessionStore::load(path).empty());
    SessionStore::remove(path);
}

TEST_CASE("SessionStore rejects newline-injected fields", "[sessionstore]") {
    // A malicious or accidental newline in a URL would shift every field after
    // it; such an item must be dropped rather than parsed into garbage.
    const std::string path = tempPath("phoenix_unit_session_nl.txt");
    SessionStore::remove(path);
    std::vector<DownloadItem> in;
    in.push_back(makeItem("https://a.b/f.zip\nextra", "C:/dl/f.zip", DownloadState::Idle));
    in.push_back(makeItem("https://a.b/g.zip", "C:/dl/g.zip", DownloadState::Idle));

    REQUIRE(SessionStore::save(path, in));
    const auto out = SessionStore::load(path);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].url == "https://a.b/g.zip");
    SessionStore::remove(path);
}

TEST_CASE("SessionStore clamps out-of-range values on load", "[sessionstore]") {
    const std::string path = tempPath("phoenix_unit_session_clamp.txt");
    { std::ofstream f(path, std::ios::trunc);
      f << "PHXS1\n1\nhttps://a.b/x.zip\nC:/dl/x.zip\n99\n0\n-5\n1\n"; }
    const auto out = SessionStore::load(path);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].segments == 16);   // 99 clamped to the max
    REQUIRE(out[0].maxSpeedBps == 0.0); // negative -> unlimited
    SessionStore::remove(path);
}
