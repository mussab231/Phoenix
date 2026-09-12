#include "core/HttpClient.h"

#include "core/ResumeStore.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <exception>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

std::wstring toWide(const std::string& s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0)
        throw std::runtime_error("URL encoding failed");
    std::wstring w(static_cast<size_t>(n) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

[[noreturn]] void fail(const char* what) {
    throw std::runtime_error(std::string(what) + " (Win32 error " +
                             std::to_string(GetLastError()) + ")");
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

UrlParts crackUrl(const std::string& url) {
    std::wstring wurl = toWide(url);
    URL_COMPONENTS comp{};
    comp.dwStructSize = sizeof(comp);
    comp.dwHostNameLength = static_cast<DWORD>(-1);
    comp.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &comp))
        fail("Invalid URL");
    UrlParts p;
    p.host.assign(comp.lpszHostName, comp.dwHostNameLength);
    p.path.assign(comp.lpszUrlPath, comp.dwUrlPathLength);
    if (p.path.empty())
        p.path = L"/";
    p.port = comp.nPort;
    p.secure = (comp.nScheme == INTERNET_SCHEME_HTTPS);
    return p;
}

// RAII holder for session + connection + request handles.
class Session {
public:
    explicit Session(const UrlParts& parts) {
        hSession_ = WinHttpOpen(L"Phoenix/0.3",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession_)
            fail("WinHttpOpen");
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hSession_, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                         sizeof(policy));
        // Allow parallel segment connections against the same host.
        DWORD maxConns = 16;
        WinHttpSetOption(hSession_, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &maxConns,
                         sizeof(maxConns));
        WinHttpSetTimeouts(hSession_, 15000, 15000, 30000, 30000);
        hConnect_ = WinHttpConnect(hSession_, parts.host.c_str(), parts.port, 0);
        if (!hConnect_)
            fail("WinHttpConnect");
    }

    ~Session() {
        if (hRequest_)
            WinHttpCloseHandle(hRequest_);
        if (hConnect_)
            WinHttpCloseHandle(hConnect_);
        if (hSession_)
            WinHttpCloseHandle(hSession_);
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    HINTERNET openRequest(const UrlParts& parts, const wchar_t* method) {
        DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
        hRequest_ = WinHttpOpenRequest(hConnect_, method, parts.path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest_)
            fail("WinHttpOpenRequest");
        return hRequest_;
    }

private:
    HINTERNET hSession_ = nullptr;
    HINTERNET hConnect_ = nullptr;
    HINTERNET hRequest_ = nullptr;
};

DWORD queryStatus(HINTERNET hRequest) {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(hRequest,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX))
        fail("Status query failed");
    return status;
}

// String form (not FLAG_NUMBER) so files > 4 GB still parse. -1 if absent.
std::int64_t queryContentLength(HINTERNET hRequest) {
    wchar_t buf[64]{};
    DWORD bufLen = sizeof(buf);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
                             WINHTTP_HEADER_NAME_BY_INDEX, buf, &bufLen,
                             WINHTTP_NO_HEADER_INDEX))
        return -1;
    try {
        return std::stoll(buf);
    } catch (...) {
        return -1;
    }
}

std::int64_t fileSizeOnDisk(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open())
        return -1;
    return static_cast<std::int64_t>(in.tellg());
}

} // namespace

std::int64_t HttpClient::getFileSize(const std::string& url) {
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest = session.openRequest(parts, L"HEAD");

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("HEAD send failed");
    if (!WinHttpReceiveResponse(hRequest, nullptr))
        fail("HEAD response failed");

    DWORD status = queryStatus(hRequest);
    if (status != 200)
        throw std::runtime_error("HEAD request returned HTTP " + std::to_string(status));

    return queryContentLength(hRequest);
}

bool HttpClient::supportsRanges(const std::string& url) {
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest = session.openRequest(parts, L"GET");

    const wchar_t* probe = L"Range: bytes=0-0";
    if (!WinHttpAddRequestHeaders(hRequest, probe, static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD))
        fail("Range probe failed");
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("Range probe send failed");
    if (!WinHttpReceiveResponse(hRequest, nullptr))
        fail("Range probe response failed");

    return queryStatus(hRequest) == 206;
}

void HttpClient::download(const std::string& url, const std::string& outputPath,
                          ProgressCallback progress, const std::atomic<bool>* stopFlag,
                          std::int64_t rangeStart, std::int64_t rangeEnd,
                          std::int64_t writeOffset, bool truncate) {
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest = session.openRequest(parts, L"GET");

    if (rangeStart >= 0) {
        std::wstring range = L"Range: bytes=" + std::to_wstring(rangeStart) + L"-";
        if (rangeEnd >= rangeStart)
            range += std::to_wstring(rangeEnd);
        if (!WinHttpAddRequestHeaders(hRequest, range.c_str(),
                                      static_cast<DWORD>(-1),
                                      WINHTTP_ADDREQ_FLAG_ADD))
            fail("Range header failed");
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("GET send failed");
    if (!WinHttpReceiveResponse(hRequest, nullptr))
        fail("GET response failed");

    DWORD status = queryStatus(hRequest);
    if (status != 200 && status != 206)
        throw std::runtime_error("GET request returned HTTP " + std::to_string(status));
    if (rangeStart >= 0 && status == 200)
        throw std::runtime_error("Server ignored Range request");

    std::int64_t total = queryContentLength(hRequest);

    // Segment workers MUST NOT truncate: they share one preallocated file and
    // segment 0 starts at offset 0, which would otherwise wipe the file.
    std::ofstream out;
    if (truncate) {
        out.open(outputPath, std::ios::binary | std::ios::trunc);
    } else {
        out.open(outputPath, std::ios::binary | std::ios::in | std::ios::out);
        if (out.is_open())
            out.seekp(static_cast<std::streamoff>(writeOffset));
    }
    if (!out.is_open())
        throw std::runtime_error("Cannot open output file: " + outputPath);
    if (!out)
        throw std::runtime_error("Cannot seek output file: " + outputPath);

    std::vector<char> chunk(64 * 1024);
    std::int64_t received = 0;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
        if (stopFlag && stopFlag->load())
            throw std::runtime_error("cancelled");
        DWORD toRead = available > chunk.size() ? static_cast<DWORD>(chunk.size())
                                                : available;
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), toRead, &got) || got == 0)
            break;
        out.write(chunk.data(), got);
        if (!out)
            throw std::runtime_error("Disk write failed");
        received += got;
        if (progress)
            progress(writeOffset + received, total < 0 ? -1 : writeOffset + total);
    }
    out.close();

    if (stopFlag && stopFlag->load())
        throw std::runtime_error("cancelled");
}

void HttpClient::downloadSegmented(const std::string& url,
                                   const std::string& outputPath,
                                   ProgressCallback progress,
                                   const std::atomic<bool>* stopFlag,
                                   int numSegments) {
    if (numSegments < 1)
        numSegments = 1;
    if (numSegments > 16)
        numSegments = 16;

    const std::string statePath = ResumeStore::statePathFor(outputPath);

    std::int64_t total = getFileSize(url);
    if (total <= 0 || numSegments == 1 || !supportsRanges(url)) {
        ResumeStore::remove(statePath); // no stale state without segmentation
        download(url, outputPath, progress, stopFlag);
        return;
    }
    if (total < numSegments)
        numSegments = static_cast<int>(total);

    // Segment boundaries are deterministic so a later run lines up exactly.
    std::vector<std::int64_t> segStart(static_cast<size_t>(numSegments));
    std::vector<std::int64_t> segEnd(static_cast<size_t>(numSegments));
    for (int i = 0; i < numSegments; ++i) {
        segStart[static_cast<size_t>(i)] = i * total / numSegments;
        segEnd[static_cast<size_t>(i)] = (i + 1) * total / numSegments - 1;
    }

    // Try to resume from a previous run.
    std::vector<std::atomic<std::int64_t>> segDone(
        static_cast<size_t>(numSegments));
    bool resuming = false;
    if (auto saved = ResumeStore::load(statePath)) {
        bool ok = saved->url == url && saved->total == total &&
                  saved->segments.size() == static_cast<size_t>(numSegments) &&
                  fileSizeOnDisk(outputPath) == total;
        for (int i = 0; ok && i < numSegments; ++i) {
            const SegmentState& s = saved->segments[static_cast<size_t>(i)];
            ok = s.start == segStart[static_cast<size_t>(i)] &&
                 s.end == segEnd[static_cast<size_t>(i)];
        }
        if (ok) {
            for (int i = 0; i < numSegments; ++i)
                segDone[static_cast<size_t>(i)].store(
                    saved->segments[static_cast<size_t>(i)].done);
            resuming = true;
        } else {
            ResumeStore::remove(statePath); // stale/mismatched: start fresh
        }
    }
    if (!resuming) {
        // Preallocate so every segment can write at its own offset safely.
        std::ofstream pre(outputPath, std::ios::binary | std::ios::trunc);
        if (!pre.is_open())
            throw std::runtime_error("Cannot open output file: " + outputPath);
        pre.seekp(static_cast<std::streamoff>(total - 1));
        if (!pre)
            throw std::runtime_error("Failed to preallocate output file (seek)");
        pre.put('\0');
        pre.flush();
        if (!pre)
            throw std::runtime_error("Failed to preallocate output file (write)");
        pre.close();
        if (fileSizeOnDisk(outputPath) != total)
            throw std::runtime_error("Failed to preallocate output file (size)");
    }

    std::int64_t initial = 0;
    for (int i = 0; i < numSegments; ++i)
        initial += segDone[static_cast<size_t>(i)].load();
    std::atomic<std::int64_t> agg{initial};
    if (progress)
        progress(initial, total);

    auto saveNow = [&] {
        ResumeData rd;
        rd.url = url;
        rd.total = total;
        rd.segments.reserve(static_cast<size_t>(numSegments));
        for (int i = 0; i < numSegments; ++i) {
            SegmentState s;
            s.start = segStart[static_cast<size_t>(i)];
            s.end = segEnd[static_cast<size_t>(i)];
            s.done = segDone[static_cast<size_t>(i)].load();
            rd.segments.push_back(s);
        }
        ResumeStore::save(statePath, rd);
    };

    // Background saver: flush progress every second so pause/crash loses ~1s.
    std::atomic<bool> workersAlive{true};
    std::thread saver([&] {
        while (workersAlive.load()) {
            for (int k = 0; k < 10 && workersAlive.load(); ++k)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!workersAlive.load())
                break;
            if (stopFlag && stopFlag->load())
                break;
            saveNow();
        }
    });

    std::mutex errMutex;
    std::exception_ptr firstError;

    auto worker = [&](int i) {
        try {
            std::int64_t from =
                segStart[static_cast<size_t>(i)] + segDone[static_cast<size_t>(i)].load();
            if (from > segEnd[static_cast<size_t>(i)])
                return; // segment already complete
            std::int64_t last = from;
            download(url, outputPath,
                     [&](std::int64_t cur, std::int64_t) {
                         std::int64_t delta = cur - last;
                         last = cur;
                         if (delta > 0) {
                             segDone[static_cast<size_t>(i)].fetch_add(delta);
                             if (progress)
                                 progress(agg.fetch_add(delta) + delta, total);
                         }
                     },
                     stopFlag, from, segEnd[static_cast<size_t>(i)], from,
                     false); // never truncate the shared file
        } catch (...) {
            std::lock_guard<std::mutex> lock(errMutex);
            if (!firstError)
                firstError = std::current_exception();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(numSegments));
    for (int i = 0; i < numSegments; ++i)
        threads.emplace_back(worker, i);
    for (auto& t : threads)
        t.join();

    workersAlive = false;
    saver.join();
    saveNow(); // final flush: resume point stays fresh even on cancel

    if (firstError)
        std::rethrow_exception(firstError);
    if (stopFlag && stopFlag->load())
        throw std::runtime_error("cancelled");
    ResumeStore::remove(statePath); // completed: no resume data needed
    if (progress)
        progress(total, total);
}
