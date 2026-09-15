#pragma once

#include <cstdint>
#include <string>

// SHA-256 file integrity. A download can complete with bytes that were
// silently altered in transit or written to the wrong offsets; a content
// hash is the last line of defence against shipping a corrupt file as if
// it were good.
class Checksum {
public:
    // Returns lowercase hex SHA-256 of the file, or an empty string when the
    // file cannot be read. Safe on files larger than RAM: streamed in chunks.
    static std::string sha256File(const std::string& path);

    // Case-insensitive comparison of two hex digests; whitespace is ignored so
    // a hash pasted with a trailing newline still matches.
    static bool hexEquals(const std::string& a, const std::string& b);
};
