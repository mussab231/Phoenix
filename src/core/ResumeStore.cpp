#include "core/ResumeStore.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

constexpr char kMagic[] = "PHX2";

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
    std::ofstream out(statePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << kMagic << '\n' << data.url << '\n' << data.total << '\n'
        << data.segments.size() << '\n';
    for (const auto& s : data.segments)
        out << s.start << ' ' << s.end << ' ' << s.done << '\n';
    out.close();
    return static_cast<bool>(out);
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
    return data;
}

void ResumeStore::remove(const std::string& statePath) {
    std::remove(statePath.c_str());
}
