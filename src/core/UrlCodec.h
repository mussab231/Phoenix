#pragma once

#include <string>
#include <string_view>

// phoenix:// protocol helpers: percent-decode/encode and (de)serialisation of
// incoming URLs. The payload after "phoenix://" is the percent-encoded target
// URL, optionally followed by "?name=<encoded filename>".
namespace UrlCodec {

// Percent-decode a string (handles %XX; '+' stays literal).
std::string decode(std::string_view in);

// Percent-encode everything except unreserved characters (RFC 3986).
std::string encode(const std::string& in);

// Builds "phoenix://<encoded-url>?name=<encoded-file>" (name optional).
std::string buildProtocolLink(const std::string& url, const std::string& fileName = {});

// Parses a phoenix:// link. Returns false if the argument does not look like
// a phoenix link. On success, out pairs receive the decoded values.
bool parseProtocolLink(const std::string& arg, std::string& outUrl,
                       std::string& outFileName);

} // namespace UrlCodec