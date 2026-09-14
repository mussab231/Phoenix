#include "core/SessionStore.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

constexpr char kMagic[] = "PHXS1";

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                                nullptr);
    if (n <= 0)
        return {};
    std::string s(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

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

// URLs and paths never contain newlines (same assumption as ResumeStore).
bool illegible(const std::string& s) {
    return s.find('\n') != std::string::npos ||
           s.find('\r') != std::string::npos;
}

// Resume flagged with Running/Idle -> auto-start on the next launch.
bool willAutoResume(DownloadState state) { return state != DownloadState::Paused; }

} // namespace

std::string SessionStore::defaultPath() {
    wchar_t buf[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf) != S_OK)
        return {};
    std::wstring dir = buf;
    if (!dir.empty() && dir.back() != L'\\')
        dir += L'\\';
    dir += L"Phoenix";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return wideToUtf8(dir) + "\\session.txt";
}

bool SessionStore::save(const std::string& path,
                        const std::vector<DownloadItem>& items) {
    std::vector<const DownloadItem*> wanted;
    for (const auto& it : items) {
        if (it.state == DownloadState::Completed || it.state == DownloadState::Failed)
            continue;
        if (it.url.empty() || it.outputPath.empty() || illegible(it.url) ||
            illegible(it.outputPath))
            continue;
        wanted.push_back(&it);
    }

    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;

    out << kMagic << '\n' << wanted.size() << '\n';
    for (const auto* it : wanted) {
        out << it->url << '\n'
            << it->outputPath << '\n'
            << it->segments << '\n'
            << it->scheduledAt << '\n'
            << static_cast<long long>(it->maxSpeedBps) << '\n'
            << (willAutoResume(it->state) ? 1 : 0) << '\n';
    }
    out.close();
    return static_cast<bool>(out);
}

std::vector<DownloadItem> SessionStore::load(const std::string& path) {
    std::vector<DownloadItem> result;
    std::ifstream in(path);
    if (!in.is_open())
        return result;

    std::string line;
    if (!std::getline(in, line) || line != kMagic)
        return result;
    if (!std::getline(in, line))
        return result;
    std::int64_t count = 0;
    if (!parseInt64(line, count) || count < 0 || count > 10000)
        return result;

    result.reserve(static_cast<size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        DownloadItem it;
        if (!std::getline(in, it.url) || !std::getline(in, it.outputPath))
            return result;
        if (it.url.empty() || it.outputPath.empty())
            return result;
        std::int64_t seg = 0, sched = 0, speed = 0, resume = 0;
        if (!std::getline(in, line) || !parseInt64(line, seg))
            return result;
        if (!std::getline(in, line) || !parseInt64(line, sched))
            return result;
        if (!std::getline(in, line) || !parseInt64(line, speed))
            return result;
        if (!std::getline(in, line) || !parseInt64(line, resume))
            return result;

        it.segments = seg < 1 ? 1 : (seg > 16 ? 16 : static_cast<int>(seg));
        it.scheduledAt = sched > 0 ? sched : 0;
        it.maxSpeedBps = speed > 0 ? static_cast<double>(speed) : 0.0;
        it.state = (resume == 0) ? DownloadState::Paused : DownloadState::Idle;
        it.statusText = (it.state == DownloadState::Paused) ? "Paused" : "Queued";
        result.push_back(std::move(it));
    }
    return result;
}

bool SessionStore::exists(const std::string& path) {
    std::ifstream in(path);
    return in.is_open();
}

void SessionStore::remove(const std::string& path) {
    std::remove(path.c_str());
}