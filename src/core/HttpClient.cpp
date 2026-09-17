#include "core/HttpClient.h"

#include "core/DownloadError.h"
#include "core/MimeMap.h"
#include "core/RateLimiter.h"
#include "core/ResumeStore.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
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
        throw DownloadError(DownloadError::Category::Local, "URL encoding failed");
    std::wstring w(static_cast<size_t>(n) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                               nullptr);
    if (n <= 0)
        return {};
    std::string s(static_cast<size_t>(n) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr,
                        nullptr);
    return s;
}

[[noreturn]] void fail(const std::string& what,
                       DownloadError::Category cat = DownloadError::Category::Transient) {
    throw DownloadError(cat, what + " (Win32 error " +
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

// Read the redirect target of a 3xx response. Empty when absent.
std::wstring queryLocation(HINTERNET hRequest) {
    DWORD len = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                        WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &len, WINHTTP_NO_HEADER_INDEX);
    if (len == 0)
        return {};
    std::wstring out(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION,
                             WINHTTP_HEADER_NAME_BY_INDEX, out.data(), &len,
                             WINHTTP_NO_HEADER_INDEX))
        return {};
    if (!out.empty() && out.back() == L'\0')
        out.pop_back();
    return out;
}

// RAII holder for session + connection + request handles.
class Session {
public:
    explicit Session(const UrlParts& parts) {
        hSession_ = WinHttpOpen(L"Phoenix/1.0",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession_)
            fail("WinHttpOpen");
        // Never silently follow an https link down to plain http: the URL,
        // path and any embedded credentials would then cross the network in
        // the clear.
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(hSession_, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                         sizeof(policy));
        // Reject legacy protocols: TLS 1.2/1.3 only, no SSLv3/TLS 1.0 downgrade.
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 |
                       WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos,
                         sizeof(protos));
        // Negotiate HTTP/2 when the server offers it (Windows 10 1607+; older
        // builds and plain-HTTP servers fall back to HTTP/1.1 transparently).
        DWORD httpProto = WINHTTP_PROTOCOL_FLAG_HTTP2;
        WinHttpSetOption(hSession_, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,
                         &httpProto, sizeof(httpProto));
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
        // Reuse one Session across many chunk requests on the same worker
        // thread: close the previous request handle before opening a new one.
        if (hRequest_) {
            WinHttpCloseHandle(hRequest_);
            hRequest_ = nullptr;
        }
        DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
        hRequest_ = WinHttpOpenRequest(hConnect_, method, parts.path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest_)
            fail("WinHttpOpenRequest");
        return hRequest_;
    }

    // Sends `method` to `parts` and, if the server redirects, follows the
    // Location chain manually (automatic redirects are disabled at the session
    // level so we can vet every hop). A redirect from https to plain http is
    // refused: it would leak the URL, path, and any credentials in the clear.
    // `setupHeaders` (re)applies custom headers on every hop so Range and other
    // request options survive the redirect. Returns the final request handle,
    // already sent and with its response received.
    using HeaderSetup = std::function<void(HINTERNET)>;
    HINTERNET secureRequest(UrlParts parts, const wchar_t* method,
                            const HeaderSetup& setupHeaders = {}) {
        constexpr int kMaxRedirects = 8;
        for (int hop = 0; hop < kMaxRedirects; ++hop) {
            HINTERNET hRequest = openRequest(parts, method);
            if (setupHeaders)
                setupHeaders(hRequest);
            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                fail("send failed");
            if (!WinHttpReceiveResponse(hRequest, nullptr))
                fail("response failed");

            const DWORD status = queryStatus(hRequest);
            if (status < 300 || status >= 400)
                return hRequest; // final answer; the caller inspects it

            const std::wstring location = queryLocation(hRequest);
            if (location.empty())
                return hRequest; // 3xx with no Location: let the caller report it

            UrlParts next = crackUrl(toUtf8(location));
            if (parts.secure && !next.secure)
                throw DownloadError(DownloadError::Category::Http,
                                    "Refused HTTPS->HTTP downgrade at " +
                                        toUtf8(location));
            // Reconnect if the redirect crosses hosts: the existing
            // connection handle is bound to the original host/port.
            if (next.host != parts.host || next.port != parts.port) {
                if (hConnect_) {
                    WinHttpCloseHandle(hConnect_);
                    hConnect_ = nullptr;
                }
                hConnect_ = WinHttpConnect(hSession_, next.host.c_str(),
                                           next.port, 0);
                if (!hConnect_)
                    fail("WinHttpConnect (redirect)");
            }
            parts = next;
        }
        throw DownloadError(DownloadError::Category::Http,
                            "Too many redirects");
    }

private:
    HINTERNET hSession_ = nullptr;
    HINTERNET hConnect_ = nullptr;
    HINTERNET hRequest_ = nullptr;
};

// Parse "bytes X-Y/TOTAL" from a 206 response into [rangeStart, rangeEnd]
// and the full resource size. Returns false if the header is missing or
// malformed. This is how we verify a worker actually received the byte range
// it asked for, instead of trusting a bare 206 status code.
bool queryContentRange(HINTERNET hRequest, std::int64_t& rangeStart,
                       std::int64_t& rangeEnd, std::int64_t& total) {
    wchar_t buf[96]{};
    DWORD bufLen = sizeof(buf);
    if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_RANGE,
                             WINHTTP_HEADER_NAME_BY_INDEX, buf, &bufLen,
                             WINHTTP_NO_HEADER_INDEX))
        return false;
    std::wstring text = buf;
    if (text.rfind(L"bytes ", 0) != 0)
        return false;
    text.erase(0, 6);
    const auto slash = text.find(L'/');
    if (slash == std::wstring::npos)
        return false;
    const auto dash = text.find(L'-');
    if (dash == std::wstring::npos || dash > slash)
        return false;
    try {
        rangeStart = static_cast<std::int64_t>(std::stoll(text.substr(0, dash)));
        rangeEnd = static_cast<std::int64_t>(
            std::stoll(text.substr(dash + 1, slash - dash - 1)));
        const std::wstring tot = text.substr(slash + 1);
        total = (tot == L"*") ? -1
                              : static_cast<std::int64_t>(std::stoll(tot));
    } catch (...) {
        return false;
    }
    if (rangeStart < 0 || rangeEnd < rangeStart)
        return false;
    return true;
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

// Is this failure worth retrying? Now answered by the error's own category
// rather than by pattern-matching the message text: rewording a message can
// no longer flip a permanent failure into a retry loop (or the reverse).
bool isTransient(const std::exception& e) {
    return DownloadError::isTransient(e);
}

// Sleep for `ms` in 50 ms slices so a cancel/stop still returns quickly.
void sleepInterruptible(int ms, const std::atomic<bool>* stopFlag) {
    for (int left = ms; left > 0; left -= 50) {
        if (stopFlag && stopFlag->load())
            return;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(std::min(left, 50)));
    }
}

// Dynamic re-segmentation works in atomic chunks claimed from a shared cursor.
// Per-chunk request overhead is kept low while still balancing fast vs slow
// connections, so cap between 256 KiB and 4 MiB.
constexpr std::int64_t kMinChunkBytes = 256LL * 1024;
constexpr std::int64_t kMaxChunkBytes = 4LL * 1024 * 1024;

// Streams exactly [from, to] of url into outputPath at file offset `from`,
// using a persistent Session so consecutive chunks on the same worker thread
// reuse the TCP/TLS connection. Returns the number of bytes written (which
// must equal to-from+1, or the server cut the reply short and the caller
// retries). Throws on failure/cancellation.
std::int64_t streamRange(Session& session, const UrlParts& parts,
                         const std::string& outputPath,
                         const std::atomic<bool>* stopFlag, std::int64_t from,
                         std::int64_t to, RateLimiter* limiter) {
    if (HttpClient::g_testFakeFailures.load(std::memory_order_relaxed) > 0 &&
        HttpClient::g_testFakeFailures.fetch_sub(1) > 0)
        throw DownloadError(DownloadError::Category::Transient, "simulated transient error");

    HINTERNET hRequest = session.secureRequest(
        parts, L"GET", [from, to](HINTERNET h) {
            std::wstring range = L"Range: bytes=" + std::to_wstring(from) + L"-" +
                                 std::to_wstring(to);
            if (!WinHttpAddRequestHeaders(h, range.c_str(),
                                          static_cast<DWORD>(-1),
                                          WINHTTP_ADDREQ_FLAG_ADD))
                fail("Range header failed");
        });

    DWORD status = queryStatus(hRequest);
    if (status != 206) {
        if (status == 200)
            throw DownloadError(DownloadError::Category::Http,
                                "Server ignored Range request");
        throw DownloadError(DownloadError::Category::Http,
                            "GET request returned HTTP " +
                                std::to_string(status));
    }

    // A bare 206 tells us "some range", not "the range we asked for". A broken
    // or lying server can return a different byte span while still answering
    // 206; trusting it would silently corrupt the file. Verify the Content-Range
    // header actually matches the requested [from, to] before writing anything.
    std::int64_t crStart = -1, crEnd = -1, crTotal = -1;
    if (!queryContentRange(hRequest, crStart, crEnd, crTotal))
        throw DownloadError(DownloadError::Category::Http,
                            "Server returned a malformed Content-Range");
    if (crStart != from || crEnd != to)
        throw DownloadError(DownloadError::Category::Http,
                            "Server returned the wrong byte range");

    std::ofstream out(outputPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!out.is_open())
        throw DownloadError(DownloadError::Category::Local,
                            "Cannot open output file: " + outputPath);
    out.seekp(static_cast<std::streamoff>(from));
    if (!out)
        throw DownloadError(DownloadError::Category::Local,
                            "Cannot seek output file: " + outputPath);

    std::vector<char> chunk(64 * 1024);
    std::int64_t written = 0;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
        if (stopFlag && stopFlag->load())
            throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
        DWORD toRead = available > chunk.size() ? static_cast<DWORD>(chunk.size())
                                                 : available;
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), toRead, &got) || got == 0)
            break;
        if (limiter && !limiter->consume(got, stopFlag))
            throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
        out.write(chunk.data(), got);
        if (!out)
            throw DownloadError(DownloadError::Category::Local,
                                "Disk write failed");
        written += got;
    }
    out.close();

    if (stopFlag && stopFlag->load())
        throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
    if (written != to - from + 1)
        throw DownloadError(DownloadError::Category::Transient, "short read");
    return written;
}

} // namespace

std::atomic<int> HttpClient::g_testFakeFailures{0};

std::int64_t HttpClient::getFileSize(const std::string& url) {
    UrlParts parts = crackUrl(url);
    // Some servers/CDNs answer HEAD with no Content-Length (or a bogus one),
    // or reject HEAD outright. Instead of failing the whole download, fall
    // back to asking for a single byte via GET and read the true size from
    // Content-Range; anything that serves Range to the segmented downloader
    // necessarily also answers this probe.
    try {
        Session session(parts);
        HINTERNET hRequestHttp = session.secureRequest(parts, L"HEAD");
        const DWORD status = queryStatus(hRequestHttp);
        const std::int64_t len = (status == 200)
                                     ? queryContentLength(hRequestHttp)
                                     : -1;
        if (len >= 0) {
            // HEAD worked and gave us a real size — use it and bail early so
            // the segmented path never pays for an extra round-trip.
            return len;
        }
    } catch (...) {
        // fall through to the GET-byte probe below
    }

    Session probe(parts);
    HINTERNET hRequest = probe.secureRequest(
        parts, L"GET", [](HINTERNET h) {
            const wchar_t* range = L"Range: bytes=0-0";
            if (!WinHttpAddRequestHeaders(h, range, static_cast<DWORD>(-1),
                                          WINHTTP_ADDREQ_FLAG_ADD))
                fail("Range probe header failed");
        });

    const DWORD status = queryStatus(hRequest);
    if (status != 206)
        fail("Range probe returned HTTP " + std::to_string(status));

    std::int64_t rs = -1, re = -1, rt = -1;
    if (!queryContentRange(hRequest, rs, re, rt) || rt < 0)
        fail("Range probe returned no usable Content-Range");
    return rt;
}

// Reads one response header as UTF-8; empty string when absent.
namespace {
// The length probe passes WINHTTP_NO_OUTPUT_BUFFER, which always fails with
// ERROR_INSUFFICIENT_BUFFER and reports the needed size: only an empty size
// means the header is genuinely missing, the boolean return must be ignored.
std::string readHeaderUtf8(HINTERNET hRequest, DWORD which) {
    DWORD len = 0;
    WinHttpQueryHeaders(hRequest, which, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &len, WINHTTP_NO_HEADER_INDEX);
    if (len == 0)
        return {};
    std::wstring w(static_cast<size_t>(len / sizeof(wchar_t)), L'\0');
    if (!WinHttpQueryHeaders(hRequest, which, WINHTTP_HEADER_NAME_BY_INDEX,
                             w.data(), &len, WINHTTP_NO_HEADER_INDEX))
        return {};
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return toUtf8(w);
}
} // namespace

void HttpClient::getResourceIdentity(const std::string& url,
                                    std::string& etag,
                                    std::string& lastModified) {
    etag.clear();
    lastModified.clear();
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest;
    try {
        hRequest = session.secureRequest(parts, L"HEAD");
    } catch (...) {
        return; // identity unknown, not fatal: fall back to URL+total check
    }

    // WinHTTP gives headers as wide strings; the length probe always fails
    // with ERROR_INSUFFICIENT_BUFFER, so readHeaderUtf8 (which ignores that
    // first return value) is what actually yields the values.
    etag = readHeaderUtf8(hRequest, WINHTTP_QUERY_ETAG);
    lastModified = readHeaderUtf8(hRequest, WINHTTP_QUERY_LAST_MODIFIED);
}

bool HttpClient::supportsRanges(const std::string& url) {
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest = session.secureRequest(
        parts, L"GET", [](HINTERNET h) {
            const wchar_t* probe = L"Range: bytes=0-0";
            if (!WinHttpAddRequestHeaders(h, probe, static_cast<DWORD>(-1),
                                          WINHTTP_ADDREQ_FLAG_ADD))
                fail("Range probe failed");
        });

    return queryStatus(hRequest) == 206;
}

std::string HttpClient::guessFilename(const std::string& url,
                                      const std::string& suggestedName) {
    using namespace MimeMap;

    // 1. An explicit, already-typed suggestion (the browser knows the real
    //    name from its own Content-Disposition handling) always wins.
    if (!suggestedName.empty() && hasExtension(suggestedName))
        return suggestedName;

    std::string name = suggestedName;
    std::string mime;

    // 2/4. Probe the server for Content-Disposition and Content-Type. Any
    // network failure is non-fatal: we fall back to the URL-derived name.
    try {
        UrlParts parts = crackUrl(url);
        Session session(parts);
        HINTERNET hRequest = session.secureRequest(parts, L"HEAD");
        const std::string disposition =
            readHeaderUtf8(hRequest, WINHTTP_QUERY_CONTENT_DISPOSITION);
        if (!disposition.empty()) {
            std::string fn = parseContentDispositionFilename(disposition);
            if (!fn.empty())
                name = fn;
        }
        mime = readHeaderUtf8(hRequest, WINHTTP_QUERY_CONTENT_TYPE);
    } catch (...) {
        // HEAD rejected or timed out: keep what we have.
    }

    // 3. URL's last path segment.
    if (name.empty())
        name = urlFilename(url);

    // 4. Type-derived extension when nothing above gave a typed name. An
    // empty name (root URL) still gets a real file name, not a bare ".html".
    if (!hasExtension(name) && !mime.empty()) {
        const std::string ext = mimeToExtension(mime);
        if (!ext.empty())
            name = name.empty() ? ("download." + ext) : (name + "." + ext);
    }

    if (name.empty())
        name = "download.bin";
    return name;
}

void HttpClient::download(const std::string& url, const std::string& outputPath,
                          ProgressCallback progress, const std::atomic<bool>* stopFlag,
                          std::int64_t rangeStart, std::int64_t rangeEnd,
                          std::int64_t writeOffset, bool truncate,
                          RateLimiter* limiter) {
    // Test-only: each call decrements; the first N calls fail with a realistic
    // transient error so --self-test-retry can watch recovery.
    if (g_testFakeFailures.load(std::memory_order_relaxed) > 0 &&
        g_testFakeFailures.fetch_sub(1) > 0)
        throw DownloadError(DownloadError::Category::Transient, "simulated transient error");
    UrlParts parts = crackUrl(url);
    Session session(parts);
    HINTERNET hRequest = session.secureRequest(
        parts, L"GET", [rangeStart, rangeEnd](HINTERNET h) {
            if (rangeStart < 0)
                return;
            std::wstring range = L"Range: bytes=" + std::to_wstring(rangeStart) + L"-";
            if (rangeEnd >= rangeStart)
                range += std::to_wstring(rangeEnd);
            if (!WinHttpAddRequestHeaders(h, range.c_str(),
                                          static_cast<DWORD>(-1),
                                          WINHTTP_ADDREQ_FLAG_ADD))
                fail("Range header failed");
        });

    DWORD status = queryStatus(hRequest);
    if (status != 200 && status != 206)
        throw DownloadError(DownloadError::Category::Http,
                            "GET request returned HTTP " + std::to_string(status));
    if (rangeStart >= 0 && status == 200)
        throw DownloadError(DownloadError::Category::Http,
                            "Server ignored Range request");

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
        throw DownloadError(DownloadError::Category::Local,
                            "Cannot open output file: " + outputPath);
    if (!out)
        throw DownloadError(DownloadError::Category::Local,
                            "Cannot seek output file: " + outputPath);

    std::vector<char> chunk(64 * 1024);
    std::int64_t received = 0;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
        if (stopFlag && stopFlag->load())
            throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
        DWORD toRead = available > chunk.size() ? static_cast<DWORD>(chunk.size())
                                                 : available;
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), toRead, &got) || got == 0)
            break;
        if (limiter && !limiter->consume(got, stopFlag))
            throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
        out.write(chunk.data(), got);
        if (!out)
            throw DownloadError(DownloadError::Category::Local,
                                "Disk write failed");
        received += got;
        if (progress)
            progress(writeOffset + received, total < 0 ? -1 : writeOffset + total);
    }
    out.close();

    if (stopFlag && stopFlag->load())
        throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
}

void HttpClient::segmentedCore(const std::string& url,
                               const std::string& outputPath,
                               ProgressCallback progress,
                               const std::atomic<bool>* stopFlag,
                               int numSegments,
                               RateLimiter* limiter) {
    if (numSegments < 1)
        numSegments = 1;
    if (numSegments > 16)
        numSegments = 16;

    const std::string statePath = ResumeStore::statePathFor(outputPath);

    std::int64_t total = getFileSize(url);
    if (total <= 0 || numSegments == 1 || !supportsRanges(url)) {
        ResumeStore::remove(statePath); // no stale state without segmentation
        download(url, outputPath, progress, stopFlag, -1, -1, 0, true, limiter);
        return;
    }
    if (total < numSegments)
        numSegments = static_cast<int>(total);

    // Identity validators for the resume decision below. Many servers expose
    // an ETag or Last-Modified; if present and mismatched on resume, the file
    // changed under us and the saved byte-ranges must be discarded.
    std::string etag, lastModified;
    getResourceIdentity(url, etag, lastModified);

    // Dynamic re-segmentation: no fixed per-connection ranges. A per-launch
    // chunk size keeps request overhead low, and a shared cursor hands each
    // idle connection the next unclaimed chunk, so a fast connection that
    // finished its share automatically takes over the remaining bytes of a
    // slow one. No empty slot ever goes unused, no queue waits for a straggler.
    const std::int64_t chunkBytes = std::min(
        std::max(total / (numSegments * 4), kMinChunkBytes), kMaxChunkBytes);
    const std::int64_t numSlots = (total + chunkBytes - 1) / chunkBytes;
    std::vector<std::atomic<std::uint8_t>> done(static_cast<size_t>(numSlots));

    // Try to resume the completed byte-ranges from a previous run.
    bool resuming = false;
    std::int64_t initial = 0;
    if (auto saved = ResumeStore::load(statePath)) {
        bool ok = saved->url == url && saved->total == total &&
                  fileSizeOnDisk(outputPath) == total;
        // URL+total agreement still does not prove the content is the same
        // file: a server can swap bytes while keeping the size. If either
        // identity header is exposed and differs, treat the state as stale.
        // Empty saved+current means the server sent nothing twice; we cannot
        // do better than the URL+total check in that case.
        if (ok && (!etag.empty() || !lastModified.empty())) {
            bool sameEtag = etag.empty() ||
                            (!saved->etag.empty() && saved->etag == etag);
            bool sameLm = lastModified.empty() ||
                          (!saved->lastModified.empty() &&
                           saved->lastModified == lastModified);
            ok = sameEtag && sameLm;
        }
        if (ok) {
            for (const auto& s : saved->segments) {
                std::int64_t fs = s.start / chunkBytes;
                std::int64_t fe = s.end / chunkBytes;
                for (std::int64_t slot = fs; slot <= fe && slot < numSlots;
                     ++slot) {
                    std::int64_t slotStart = slot * chunkBytes;
                    std::int64_t slotEnd = std::min(slotStart + chunkBytes - 1,
                                                    total - 1);
                    if (s.start <= slotStart && s.end >= slotEnd) {
                        done[static_cast<size_t>(slot)].store(1);
                        initial += slotEnd - slotStart + 1;
                    }
                }
            }
        }
        resuming = true;
        if (initial == 0)
            resuming = false; // nothing usable in the state file
    }
    if (!resuming) {
        initial = 0;
        ResumeStore::remove(statePath); // stale/mismatched: start fresh
        // Preallocate so every chunk can write at its own offset safely.
        std::ofstream pre(outputPath, std::ios::binary | std::ios::trunc);
        if (!pre.is_open())
            throw DownloadError(DownloadError::Category::Local,
                            "Cannot open output file: " + outputPath);
        pre.seekp(static_cast<std::streamoff>(total - 1));
        if (!pre)
            throw DownloadError(DownloadError::Category::Local,
                                "Failed to preallocate output file (seek)");
        pre.put('\0');
        pre.flush();
        if (!pre)
            throw DownloadError(DownloadError::Category::Local,
                                "Failed to preallocate output file (write)");
        pre.close();
        if (fileSizeOnDisk(outputPath) != total)
            throw DownloadError(DownloadError::Category::Local,
                                "Failed to preallocate output file (size)");
    }

    std::atomic<std::int64_t> next{0};    // next unclaimed byte offset
    std::atomic<std::int64_t> agg{initial};
    if (progress)
        progress(initial, total);

    auto saveNow = [&] {
        ResumeData rd;
        rd.url = url;
        rd.total = total;
        rd.etag = etag;
        rd.lastModified = lastModified;
        rd.segments.reserve(static_cast<size_t>(numSlots / 64) + 16);
        std::int64_t runStart = -1;
        auto flushRun = [&](std::int64_t lastSlot) {
            SegmentState s;
            s.start = runStart * chunkBytes;
            s.end = std::min(lastSlot * chunkBytes + chunkBytes - 1, total - 1);
            s.done = s.end - s.start + 1;
            rd.segments.push_back(s);
            runStart = -1;
        };
        for (std::int64_t slot = 0; slot < numSlots; ++slot) {
            if (done[static_cast<size_t>(slot)].load()) {
                if (runStart < 0)
                    runStart = slot;
            } else if (runStart >= 0) {
                flushRun(slot - 1);
            }
        }
        if (runStart >= 0)
            flushRun(numSlots - 1);
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

    // Per-chunk retries: up to kChunkAttempts, exponential backoff. The
    // caller retries the whole call if the state file lets a later pass
    // continue where this one left off.
    constexpr int kChunkAttempts = 4;
    constexpr int kChunkBaseDelayMs = 1000;

    auto worker = [&](int) {
        try {
            UrlParts parts = crackUrl(url);
            Session session(parts); // one persistent connection per worker
            for (;;) {
                if (stopFlag && stopFlag->load())
                    throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
                std::int64_t pos = next.fetch_add(chunkBytes);
                if (pos >= total)
                    break;
                std::int64_t end = std::min(pos + chunkBytes - 1, total - 1);
                size_t slot = static_cast<size_t>(pos / chunkBytes);
                if (done[slot].load())
                    continue; // already fetched on a previous run
                std::int64_t attempt = 1;
                for (;;) {
                    try {
                        streamRange(session, parts, outputPath, stopFlag, pos,
                                    end, limiter);
                        done[slot].store(1);
                        std::int64_t len = end - pos + 1;
                        if (progress)
                            progress(agg.fetch_add(len) + len, total);
                        break;
                    } catch (const std::exception& e) {
                        bool stopped = stopFlag && stopFlag->load();
                        if (stopped)
                            throw; // user cancel: propagate immediately
                        if (attempt >= kChunkAttempts ||
                            !isTransient(e))
                            throw;
                        sleepInterruptible(
                            kChunkBaseDelayMs * (1 << (attempt - 1)), stopFlag);
                        if (stopFlag && stopFlag->load())
                            throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
                        ++attempt;
                    }
                }
            }
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
        throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
    ResumeStore::remove(statePath); // completed: no resume data needed
    if (progress)
        progress(total, total);
}

void HttpClient::downloadSegmented(const std::string& url,
                                   const std::string& outputPath,
                                   ProgressCallback progress,
                                   const std::atomic<bool>* stopFlag,
                                   int numSegments,
                                   RateLimiter* limiter,
                                   int maxAttempts,
                                   int baseRetryMs) {
    if (maxAttempts < 1)
        maxAttempts = 1;
    if (baseRetryMs < 0)
        baseRetryMs = 0;

    // Whole-call retry: a second pass resumes from the .phoenix-state file, so
    // transient failures early on (HEAD/probe, fallback path) recover too.
    for (int attempt = 1;; ++attempt) {
        try {
            segmentedCore(url, outputPath, progress, stopFlag, numSegments, limiter);
            return;
        } catch (const std::exception& e) {
            if (attempt >= maxAttempts ||
                (stopFlag && stopFlag->load()) || !isTransient(e))
                throw;
            sleepInterruptible(baseRetryMs * (1 << (attempt - 1)), stopFlag);
            if (stopFlag && stopFlag->load())
                throw DownloadError(DownloadError::Category::Cancelled, "cancelled");
        }
    }
}
