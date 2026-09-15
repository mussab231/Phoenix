#include "core/Checksum.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace {
constexpr std::size_t kChunk = 1 << 20; // 1 MiB: keeps peak memory flat
}

std::string Checksum::sha256File(const std::string& path) {
    QFile f(QString::fromStdString(path));
    const std::int64_t size = QFileInfo(f).size();
    if (size < 0 || !f.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash h(QCryptographicHash::Sha256);
    if (size == 0)
        return h.result().toHex().toStdString();

    while (!f.atEnd()) {
        const QByteArray chunk = f.read(kChunk);
        if (chunk.isEmpty())
            return {};
        h.addData(chunk);
    }
    return h.result().toHex().toStdString();
}

std::string trimHex(std::string s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c); };
    s.erase(std::remove_if(s.begin(), s.end(), isSpace), s.end());
    return s;
}

bool Checksum::hexEquals(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty())
        return false;
    std::string x = trimHex(a);
    std::string y = trimHex(b);
    if (x.size() != y.size())
        return false;
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(x[i])) !=
            std::tolower(static_cast<unsigned char>(y[i])))
            return false;
    }
    return true;
}
