#pragma once

#include <string>

// Pure name/type resolution helpers for downloaded files. Kept free of Qt and
// WinHTTP so the unit-test suite can exercise them directly; the network probe
// that feeds them lives in HttpClient::guessFilename.

namespace MimeMap {

// ASCII lowercase; the MIME table and extension comparisons are case-blind.
std::string toLower(std::string s);

// A name is "typed" when it carries a suffix after a dot that is not the
// leading character (".bashrc" is a dotfile, not "bashrc" + ".rc").
bool hasExtension(const std::string& name);

// Reverses percent-encoding (%41 -> A). Passes invalid sequences through
// untouched so a stray % never eats following characters.
std::string percentDecode(const std::string& in);

// Reads `key` from a Content-Disposition-style header: quoted values (with
// escapes) and bare tokens alike.
std::string dispositionField(const std::string& header, const std::string& key);

// Extracts the filename from Content-Disposition. Prefers the RFC 5987
// `filename*=UTF-8''...` form (the only one able to carry non-ASCII), and
// falls back to `filename="..."`.
std::string parseContentDispositionFilename(const std::string& header);

// Maps a MIME type to an extension (mp4, mkv, exe, zip, ...). Parameters are
// stripped, case is ignored. Returns empty for unknown types and for
// application/octet-stream, which carries no usable information.
std::string mimeToExtension(const std::string& mime);

// Last path segment of a URL with query/fragment removed and decoded.
std::string urlFilename(const std::string& url);

} // namespace MimeMap
