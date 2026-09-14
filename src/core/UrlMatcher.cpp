#include "core/UrlMatcher.h"

#include <cctype>

namespace UrlMatcher {

namespace {

// True if s[at...] begins "http://" or "https://" (ASCII case-insensitive).
bool startsWithHttp(const std::string& s, size_t at) {
    auto lo = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    if (at + 7 > s.size())
        return false;
    if (!(lo(s[at]) == 'h' && lo(s[at + 1]) == 't' && lo(s[at + 2]) == 't' &&
          lo(s[at + 3]) == 'p'))
        return false;
    // Either "http://" (offset 4 is ':') or "https://" (offset 4 is 's').
    if (s[at + 4] == ':') {
        if (!(s[at + 5] == '/' && s[at + 6] == '/'))
            return false;
    } else if (lo(s[at + 4]) == 's') {
        if (at + 8 >= s.size())
            return false;
        if (!(s[at + 5] == ':' && s[at + 6] == '/' && s[at + 7] == '/'))
            return false;
    } else {
        return false;
    }
    return true;
}

} // namespace

bool isDownloadUrl(const std::string& text) {
    return startsWithHttp(text, 0);
}

std::string extractFirstUrl(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (!startsWithHttp(text, i))
            continue;
        size_t end = i;
        while (end < text.size()) {
            char c = text[end];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\')
                break;
            ++end;
        }
        if (end > i + 8)
            return text.substr(i, end - i);
    }
    return {};
}

} // namespace UrlMatcher