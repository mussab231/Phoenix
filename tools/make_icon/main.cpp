// Dev tool: renders the Phoenix artwork to a set of PNG files and a
// multi-size .ico for the executable.
//
// Source priority (the first that exists wins):
//   1. --png <source.png>   explicit raster source
//   2. assets/phoenix-source.png   the committed master artwork
//   3. assets/phoenix.svg          vector fallback
//
// Run after changing the artwork:
//   cmake -S tools/make_icon -B tools/make_icon/build -G Ninja "-DCMAKE_PREFIX_PATH=C:/msys64/mingw64"
//   cmake --build tools/make_icon/build
// Usage: make_icon.exe <build-dir> [--png <source.png>]   (writes into <repo>/assets)

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>
#include <string>
#include <vector>

namespace {

struct IconDirEntry { // ICONDIRENTRY, little-endian
    unsigned char width;      // 0 = 256
    unsigned char height;     // 0 = 256
    unsigned char colorCount; // 0 = truecolor
    unsigned char reserved;   // 0
    unsigned short planes;    // 1
    unsigned short bitCount;  // 32
    unsigned int bytes;       // size of image data
    unsigned int offset;      // from start of file
};

void put16(std::vector<unsigned char>& v, unsigned short x) {
    v.push_back(static_cast<unsigned char>(x & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xFF));
}

void put32(std::vector<unsigned char>& v, unsigned int x) {
    v.push_back(static_cast<unsigned char>(x & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 16) & 0xFF));
    v.push_back(static_cast<unsigned char>((x >> 24) & 0xFF));
}

QImage render(QSvgRenderer& svg, int size) {
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    svg.render(&p);
    p.end();
    return img;
}

bool writeFile(const std::string& path, const unsigned char* data, size_t n) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(data, 1, n, f) == n;
    std::fclose(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    // Default run: from tools/make_icon/build -> repo root is ../../../.
    std::string svgPath = "../../../assets/phoenix.svg";
    std::string base = "../..";
    std::string pngSource; // optional raster source instead of the SVG
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--png") {
            pngSource = argv[i + 1];
            break;
        }
    }
    if (argc > 1 && argv[1][0] != '\0') {
        // argv[1] is the build directory: tools/make_icon/build
        base = std::string(argv[1]) + "/../../.."; // repo root
        svgPath = base + "/assets/phoenix.svg";
    }
    // Without an explicit --png, prefer the committed master raster artwork:
    // the shipped icon set is generated from it, so defaulting to the SVG
    // would silently change the app icon. The SVG stays a fallback.
    if (pngSource.empty()) {
        const std::string master = base + "/assets/phoenix-source.png";
        if (QFile::exists(QString::fromStdString(master)))
            pngSource = master;
    }

    QImage pngSrc;
    if (!pngSource.empty()) {
        pngSrc = QImage(QString::fromLocal8Bit(pngSource.c_str()));
        if (pngSrc.isNull()) {
            std::fprintf(stderr, "Cannot load %s\n", pngSource.c_str());
            return 1;
        }
    }

    QSvgRenderer svg;
    if (pngSrc.isNull()) {
        svg.load(QString::fromStdString(svgPath));
        if (!svg.isValid()) {
            std::fprintf(stderr, "Cannot load %s\n", svgPath.c_str());
            return 1;
        }
    }

    const std::vector<int> sizes = {16, 24, 32, 48, 64, 128, 256};
    std::vector<std::vector<unsigned char>> pngs; // raw PNG bytes per size
    pngs.reserve(sizes.size());

    for (size_t i = 0; i < sizes.size(); ++i) {
        QImage img;
        if (!pngSrc.isNull()) {
            img = pngSrc
                      .scaled(sizes[i], sizes[i], Qt::IgnoreAspectRatio,
                              Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_ARGB32);
        } else {
            img = render(svg, sizes[i]);
        }
        std::string pngPath =
            base + "/assets/icons/phoenix-" + std::to_string(sizes[i]) + ".png";
        if (!img.save(QString::fromStdString(pngPath), "PNG")) {
            std::fprintf(stderr, "Cannot write %s\n", pngPath.c_str());
            return 1;
        }
        std::fprintf(stdout, "Wrote %s\n", pngPath.c_str());

        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        pngs.push_back(std::vector<unsigned char>(bytes.begin(), bytes.end()));
    }

    // Assemble the ICO container. Vista+ accepts PNG-compressed entries.
    const unsigned int count = static_cast<unsigned int>(sizes.size());
    const unsigned int headerSize = 6;
    const unsigned int entrySize = 16;
    std::vector<IconDirEntry> entries;
    unsigned int offset = headerSize + count * entrySize;
    std::vector<unsigned char> ico;
    // ICONDIR
    put16(ico, 0);
    put16(ico, 1);
    put16(ico, static_cast<unsigned short>(count));
    for (size_t i = 0; i < sizes.size(); ++i) {
        IconDirEntry e;
        e.width = sizes[i] >= 256 ? 0 : static_cast<unsigned char>(sizes[i]);
        e.height = sizes[i] >= 256 ? 0 : static_cast<unsigned char>(sizes[i]);
        e.colorCount = 0;
        e.reserved = 0;
        e.planes = 1;
        e.bitCount = 32;
        e.bytes = static_cast<unsigned int>(pngs[i].size());
        e.offset = offset;
        offset += e.bytes;
        entries.push_back(e);
    }
    for (const auto& e : entries) {
        ico.push_back(e.width);
        ico.push_back(e.height);
        ico.push_back(e.colorCount);
        ico.push_back(e.reserved);
        put16(ico, e.planes);
        put16(ico, e.bitCount);
        put32(ico, e.bytes);
        put32(ico, e.offset);
    }
    for (const auto& png : pngs)
        ico.insert(ico.end(), png.begin(), png.end());

    std::string icoPath = base + "/assets/phoenix.ico";
    if (!writeFile(icoPath, ico.data(), ico.size())) {
        std::fprintf(stderr, "Cannot write %s\n", icoPath.c_str());
        return 1;
    }
    std::fprintf(stdout, "Wrote %s (%u bytes)\n", icoPath.c_str(),
                 static_cast<unsigned int>(ico.size()));
    return 0;
}