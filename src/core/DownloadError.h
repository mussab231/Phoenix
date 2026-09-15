#pragma once

#include <stdexcept>
#include <string>

// Structured download failure. Every error path throws one of these instead of
// a bare runtime_error, so retry logic can branch on a stable category instead
// of pattern-matching message strings (which silently breaks whenever a
// message is reworded and turns a permanent failure into a retry loop, or vice
// versa).
class DownloadError : public std::runtime_error {
public:
    enum class Category {
        // Worth retrying: socket/connection-level, timeouts, short reads,
        // simulated transient errors.
        Transient,
        // Never retry: user cancel/stop.
        Cancelled,
        // Never retry: server refused or cannot serve the request (404, 403,
        // Range not supported, malformed/wrong Content-Range).
        Http,
        // Never retry: URL or local file problem (bad URL, disk full,
        // permission, preallocate failure).
        Local,
    };

    DownloadError(Category category, const std::string& message)
        : std::runtime_error(message), m_category(category) {}

    Category category() const { return m_category; }

    static bool isTransient(const std::exception& e) {
        const auto* d = dynamic_cast<const DownloadError*>(&e);
        return d ? d->category() == Category::Transient : true;
    }

private:
    Category m_category;
};
