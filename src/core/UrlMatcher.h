#pragma once

#include <string>

// Heuristics for link catching: does this clipboard/arg value look like a
// download URL we should offer to add?
namespace UrlMatcher {

// Case-insensitive startswith http(s):// with something after it.
bool isDownloadUrl(const std::string& text);

// Finds the first http(s):// token starting at index start (whitespace or
// quote-delimited). Returns "" when none.
std::string extractFirstUrl(const std::string& text);

} // namespace UrlMatcher