#include "core/MimeMap.h"

#include <cstddef>
#include <map>

namespace MimeMap {

std::string toLower(std::string s) {
    for (char& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z')
            c = static_cast<char>(u + 32);
    }
    return s;
}

bool hasExtension(const std::string& name) {
    const auto pos = name.find_last_of('.');
    return pos != std::string::npos && pos > 0 && pos < name.size() - 1;
}

std::string percentDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += in[i];
    }
    return out;
}

std::string dispositionField(const std::string& header, const std::string& key) {
    const auto pos = header.find(key);
    if (pos == std::string::npos)
        return {};
    std::size_t i = pos + key.size();
    while (i < header.size() && (header[i] == ' ' || header[i] == '\t'))
        ++i;
    std::string out;
    if (i < header.size() && header[i] == '"') {
        ++i;
        while (i < header.size() && header[i] != '"') {
            if (header[i] == '\\' && i + 1 < header.size())
                ++i; // keep the escaped character itself
            out += header[i++];
        }
    } else {
        while (i < header.size() && header[i] != ';' && header[i] != ',' &&
               header[i] != ' ' && header[i] != '\t' && header[i] != '\r' &&
               header[i] != '\n')
            out += header[i++];
    }
    return out;
}

std::string parseContentDispositionFilename(const std::string& header) {
    const std::string encoded = dispositionField(header, "filename*=");
    if (!encoded.empty()) {
        // charset''name -> drop the charset token, percent-decode the rest.
        const auto sep = encoded.find("''");
        const std::string body =
            sep == std::string::npos ? encoded : encoded.substr(sep + 2);
        return percentDecode(body);
    }
    return dispositionField(header, "filename=");
}

std::string mimeToExtension(const std::string& mimeRaw) {
    const std::string mime = toLower(mimeRaw);
    const auto semi = mime.find(';');
    const std::string base = semi == std::string::npos ? mime : mime.substr(0, semi);

    static const std::map<std::string, std::string> kMap = {
        {"application/pdf", "pdf"},
        {"application/zip", "zip"},
        {"application/x-7z-compressed", "7z"},
        {"application/x-rar-compressed", "rar"},
        {"application/x-tar", "tar"},
        {"application/gzip", "gz"},
        {"application/x-bzip2", "bz2"},
        {"application/x-xz", "xz"},
        {"application/x-msdownload", "exe"},
        {"application/vnd.microsoft.portable-executable", "exe"},
        {"application/x-msi", "msi"},
        {"application/x-apple-diskimage", "dmg"},
        {"application/vnd.android.package-archive", "apk"},
        {"application/java-archive", "jar"},
        {"application/x-iso9660-image", "iso"},
        {"application/json", "json"},
        {"application/xml", "xml"},
        {"application/msword", "doc"},
        {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "docx"},
        {"application/vnd.ms-excel", "xls"},
        {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "xlsx"},
        {"application/vnd.ms-powerpoint", "ppt"},
        {"application/vnd.openxmlformats-officedocument.presentationml.presentation", "pptx"},
        {"text/plain", "txt"},
        {"text/html", "html"},
        {"text/css", "css"},
        {"text/csv", "csv"},
        {"text/xml", "xml"},
        {"image/jpeg", "jpg"},
        {"image/png", "png"},
        {"image/gif", "gif"},
        {"image/webp", "webp"},
        {"image/bmp", "bmp"},
        {"image/svg+xml", "svg"},
        {"image/tiff", "tif"},
        {"image/x-icon", "ico"},
        {"audio/mpeg", "mp3"},
        {"audio/mp3", "mp3"},
        {"audio/mp4", "m4a"},
        {"audio/x-m4a", "m4a"},
        {"audio/aac", "aac"},
        {"audio/x-wav", "wav"},
        {"audio/wav", "wav"},
        {"audio/x-flac", "flac"},
        {"audio/ogg", "ogg"},
        {"video/mp4", "mp4"},
        {"video/x-msvideo", "avi"},
        {"video/avi", "avi"},
        {"video/x-matroska", "mkv"},
        {"video/quicktime", "mov"},
        {"video/webm", "webm"},
        {"video/x-flv", "flv"},
        {"video/x-ms-wmv", "wmv"},
        {"video/mpeg", "mpg"},
        {"video/ogg", "ogv"},
    };
    const auto it = kMap.find(base);
    return it == kMap.end() ? std::string() : it->second;
}

std::string urlFilename(const std::string& url) {
    // Peel the scheme and authority off first: the name comes from the path
    // only, so "https://example.com" (no path) has no name at all.
    std::string path = url;
    const auto scheme = path.find("://");
    if (scheme != std::string::npos) {
        const auto hostSlash = path.find('/', scheme + 3);
        if (hostSlash == std::string::npos)
            return {}; // host only, no path component
        path = path.substr(hostSlash);
    }
    const auto q = path.find_first_of("?#");
    const std::string clean = q == std::string::npos ? path : path.substr(0, q);
    const auto slash = clean.find_last_of('/');
    const std::string name =
        slash == std::string::npos ? clean : clean.substr(slash + 1);
    return name.empty() ? std::string() : percentDecode(name);
}

} // namespace MimeMap
