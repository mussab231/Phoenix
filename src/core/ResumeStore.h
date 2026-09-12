#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct SegmentState {
    std::int64_t start = 0; // absolute file offset where the segment begins
    std::int64_t end = -1;  // inclusive
    std::int64_t done = 0;  // bytes already downloaded within [start, end]
};

struct ResumeData {
    std::string url;
    std::int64_t total = -1;
    std::vector<SegmentState> segments;
};

// Persists segmented-download progress in a small sidecar text file so a
// download can continue after pause, crash, or network loss. STL only.
class ResumeStore {
public:
    static std::string statePathFor(const std::string& outputPath);
    static bool save(const std::string& statePath, const ResumeData& data);
    static std::optional<ResumeData> load(const std::string& statePath);
    static void remove(const std::string& statePath);
};
