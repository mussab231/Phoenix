#include "core/ResumeStore.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr char kMagic[] = "PHX2";

#ifdef _WIN32
// UTF-8 byte string -> wide string for the Win32 rename calls below.
std::wstring wideUtf8(const std::string& s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(),
                        n);
    return w;
}
#endif

bool parseInt64(const std::string& s, std::int64_t& out) {
    try {
        size_t pos = 0;
        long long v = std::stoll(s, &pos);
        if (pos != s.size())
            return false;
        out = static_cast<std::int64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::string ResumeStore::statePathFor(const std::string& outputPath) {
    return outputPath + ".phoenix-state";
}

bool ResumeStore::save(const std::string& statePath, const ResumeData& data) {
    // Write to a sibling temp file first, flush it to disk, then atomically
    // rename over the real path. A crash mid-write can therefore never leave
    // a truncated/corrupt ".phoenix-state" file behind.
    const std::string tmpPath = statePath + ".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::remove(tmpPath.c_str());
        return false;
    }
    out << kMagic << '\n' << data.url << '\n' << data.total << '\n'
        << data.segments.size() << '\n';
    for (const auto& s : data.segments)
        out << s.start << ' ' << s.end << ' ' << s.done << '\n';
    // Identity validators, written after the segment list so older readers
    // (and older state files) keep working either way.
    out << data.etag << '\n' << data.lastModified << '\n';
    out.flush();
    if (!out) {
        out.close();
        std::remove(tmpPath.c_str());
        return false;
    }
    out.close();
#ifdef _WIN32
    // MoveFileExW with REPLACE_EXISTING is atomic on the same volume.
    if (!MoveFileExW(wideUtf8(tmpPath).c_str(), wideUtf8(statePath).c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
#else
    if (std::rename(tmpPath.c_str(), statePath.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
#endif
}

std::optional<ResumeData> ResumeStore::load(const std::string& statePath) {
    std::ifstream in(statePath);
    if (!in.is_open())
        return std::nullopt;

    std::string line;
    if (!std::getline(in, line) || line != kMagic)
        return std::nullopt;

    ResumeData data;
    if (!std::getline(in, data.url) || data.url.empty())
        return std::nullopt;
    if (!std::getline(in, line) || !parseInt64(line, data.total) || data.total <= 0)
        return std::nullopt;
    std::int64_t count = 0;
    if (!std::getline(in, line) || !parseInt64(line, count) || count < 1 ||
        count > 1000000)
        return std::nullopt;

    data.segments.reserve(std::min<int64_t>(count, 4096));
    for (std::int64_t i = 0; i < count; ++i) {
        if (!std::getline(in, line))
            return std::nullopt;
        std::istringstream parts(line);
        SegmentState s;
        if (!(parts >> s.start >> s.end >> s.done))
            return std::nullopt;
        std::int64_t segLen = s.end - s.start + 1;
        if (s.start < 0 || s.end < s.start || s.done < 0 || s.done > segLen)
            return std::nullopt;
        data.segments.push_back(s);
    }
    // Optional tail: ETag and Last-Modified. Missing lines (older state files)
    // simply leave both empty, which falls back to the URL+total check.
    if (std::getline(in, data.etag))
        std::getline(in, data.lastModified);
    return data;
}

void ResumeStore::remove(const std::string& statePath) {
    std::remove(statePath.c_str());
}
