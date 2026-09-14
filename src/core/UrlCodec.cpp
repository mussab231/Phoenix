#include "core/UrlCodec.h"

#include <cctype>
#include <stdexcept>

namespace UrlCodec {

namespace {

const char* kHex = "0123456789ABCDEF";

bool isUnreserved(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

std::string hexPair(unsigned char v) {
    std::string out(3, '%');
    out[1] = kHex[v >> 4];
    out[2] = kHex[v & 0x0F];
    return out;
}

} // namespace

std::string decode(std::string_view in) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (i + 2 < in.size() && in[i] == '%') {
            int hi = hexVal(in[i + 1]);
            int lo = hexVal(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 3;
                continue;
            }
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

std::string encode(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        if (isUnreserved(static_cast<char>(c)))
            out.push_back(static_cast<char>(c));
        else
            out += hexPair(c);
    }
    return out;
}

std::string buildProtocolLink(const std::string& url, const std::string& fileName) {
    std::string out = "phoenix://" + encode(url);
    if (!fileName.empty())
        out += "?name=" + encode(fileName);
    return out;
}

bool parseProtocolLink(const std::string& arg, std::string& outUrl,
                       std::string& outFileName) {
    constexpr std::string_view kScheme = "phoenix://";
    if (arg.size() < kScheme.size() ||
        arg.compare(0, kScheme.size(), kScheme, 0, kScheme.size()) != 0)
        return false;

    std::string_view rest(static_cast<const char*>(arg.data()) + kScheme.size(),
                          arg.size() - kScheme.size());

    std::string_view url;
    std::string_view name;
    size_t q = 0;
    for (size_t i = 0; i < rest.size(); ++i)
        if (rest[i] == '?') {
            q = i;
            break;
        }
    if (q == 0) {
        url = rest;
    } else {
        url = rest.substr(0, q);
        std::string_view query = rest.substr(q + 1);
        constexpr std::string_view kName = "name=";
        if (query.size() >= kName.size() &&
            query.compare(0, kName.size(), kName, 0, kName.size()) == 0)
            name = query.substr(kName.size());
    }

    outUrl = decode(url);
    outFileName = decode(name);
    return true;
}

} // namespace UrlCodec