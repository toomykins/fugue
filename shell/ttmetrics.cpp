#include "shell/ttmetrics.h"
#include "platform/file.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QFontInfo>
#include <QFontMetrics>
#include <QRawFont>
#include <QString>

#include "fugue_env.h"
#include "render/dsmaptext.h"
#include "render/gdifon.h"
#include "render/maptextpaint.h"

namespace fugue::shell {
namespace {
std::atomic<uint64_t> g_fontGeneration{0};

uint16_t be16(const QByteArray& t, int o) { return uint16_t((uint8_t(t[o]) << 8) | uint8_t(t[o + 1])); }

// The VDMX record GDI reads tmAscent and tmDescent from: the first ratio range covering a 1:1 device (or the "any"
// range 0:0-0), then the record whose yPelHeight is the ppem. Returns false when there is no VDMX or no such record.
bool vdmx(const QByteArray& t, int ppem, int& yMax, int& yMin) {
    if (t.size() < 6) return false;
    int numRatios = be16(t, 4);
    int ratios = 6, offsets = ratios + 4 * numRatios;
    if (t.size() < offsets + 2 * numRatios) return false;
    for (int r = 0; r < numRatios; r++) {
        int x = uint8_t(t[ratios + 4 * r + 1]), y0 = uint8_t(t[ratios + 4 * r + 2]),
            y1 = uint8_t(t[ratios + 4 * r + 3]);
        bool any = x == 0 && y0 == 0 && y1 == 0;
        if (!any && !(y0 <= x && x <= y1)) continue;  // a 1:1 device ratio lies in [yStart/x, yEnd/x]
        int g = be16(t, offsets + 2 * r);
        if (t.size() < g + 4) return false;
        int recs = be16(t, g);
        for (int i = 0; i < recs; i++) {
            int e = g + 4 + 6 * i;
            if (t.size() < e + 6) return false;
            if (be16(t, e) == ppem) {
                yMax = int16_t(be16(t, e + 2));
                yMin = int16_t(be16(t, e + 4));
                return true;
            }
        }
        return false;
    }
    return false;
}

bool sameFamily(const QString& want, const QString& got) {
    if (want.compare(got, Qt::CaseInsensitive) == 0) return true;
    static const char* twins[][2] = {
        {"arial", "liberation sans"}, {"times new roman", "liberation serif"}, {"courier new", "liberation mono"}};
    for (const auto& t : twins)
        if (want.compare(t[0], Qt::CaseInsensitive) == 0 && got.compare(t[1], Qt::CaseInsensitive) == 0) return true;
    return false;
}

// Where a stock bitmap face's file is looked for: FUGUE_FONT_DIR, ~/.fugue/fonts, Windows' font folder, then the
// folders Wine's packages install its compatible faces to (on macOS also its app bundles' and the Library font folders).
std::vector<std::string> bitmapFontDirs() {
    std::vector<std::string> d;
    if (std::string v = fugueEnvOr("FONT_DIR"); !v.empty()) d.push_back(v);
    const std::string home = platform::homeDir();
    if (!home.empty()) d.push_back(home + "/.fugue/fonts");
    if (const char* w = std::getenv("WINDIR"); w && *w) d.push_back(std::string(w) + "/Fonts");
    for (const char* p : {"/usr/share/wine/fonts", "/usr/local/share/wine/fonts", "/opt/wine-stable/share/wine/fonts",
                          "/opt/wine-staging/share/wine/fonts", "/opt/wine-devel/share/wine/fonts"})
        d.push_back(p);
#ifdef __APPLE__
    for (const char* p :
         {"/opt/homebrew/share/wine/fonts", "/Applications/Wine Stable.app/Contents/Resources/wine/share/wine/fonts",
          "/Applications/Wine Staging.app/Contents/Resources/wine/share/wine/fonts",
          "/Applications/Wine Devel.app/Contents/Resources/wine/share/wine/fonts"})
        d.push_back(p);
    if (!home.empty()) d.push_back(home + "/Library/Fonts");
    d.push_back("/Library/Fonts");
#endif
    return d;
}

// The first file found for `file` (lower-case name; the upper-case spelling is tried too) that holds raster strikes.
std::vector<render::FonStrike> loadBitmapFace(const std::string& file) {
    std::string upper = file;
    for (char& c : upper) c = char(std::toupper(uint8_t(c)));
    for (const std::string& dir : bitmapFontDirs())
        for (const std::string& name : {file, upper}) {
            std::ifstream in(dir + "/" + name, std::ios::binary);
            if (!in) continue;
            std::ostringstream ss;
            ss << in.rdbuf();
            auto strikes = render::parseFon(ss.str());
            if (strikes.empty()) continue;
            std::fprintf(stderr, "[maptext] bitmap face from %s/%s\n", dir.c_str(), name.c_str());
            return strikes;
        }
    return {};
}

}  // namespace

void installTrueTypeMetrics() {
    render::setTrueTypeResolver([](const std::string& family, int em, bool bold, bool italic,
                                   render::TrueTypeFace& out) {
        QString want = QString::fromStdString(family);
        QFont f(want);
        f.setPixelSize(em);
        f.setBold(bold);
        f.setItalic(italic);
        f.setHintingPreference(QFont::PreferFullHinting);
        f.setStyleStrategy(QFont::NoFontMerging);
        f.setKerning(false);  // GDI sums the advances without kerning
        if (!sameFamily(want, QFontInfo(f).family())) return false;
        QFontMetrics m(f);
        int yMax = 0, yMin = 0;
        if (vdmx(QRawFont::fromFont(f).fontTable("VDMX"), em, yMax, yMin)) {
            out.ascent = yMax;
            out.descent = -yMin;
        } else {
            out.ascent = m.ascent();
            out.descent = m.descent();
        }
        out.advance = [m](char32_t cp) { return m.horizontalAdvance(QString::fromUcs4(&cp, 1)); };
        out.overhang = [m](char32_t cp) { return cp > 0xFFFF ? 0 : std::max(0, -m.rightBearing(QChar(char16_t(cp)))); };
        return true;
    });
}

bool addResourceFont(const QByteArray& file) {
    static std::mutex mutex;
    static std::set<QByteArray> added;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!added.insert(QCryptographicHash::hash(file, QCryptographicHash::Sha1)).second) return false;
    }
    if (QFontDatabase::addApplicationFontFromData(file) < 0) return false;
    // Both hooks forget what they resolved and drew when they are set: a family that was missing is asked for again.
    installTrueTypeMetrics();
    installMaptextGlyphs();
    g_fontGeneration++;
    return true;
}
uint64_t resourceFontGeneration() { return g_fontGeneration; }

void installMaptextGlyphs() {
    auto files = std::make_shared<std::map<std::string, std::vector<render::FonStrike>>>();
    // Windows has two files of each face, digitised for 96 and for 120 dpi, and stock draws from both.
    for (const auto& [family, file] : {std::pair<std::string, std::string>{"small fonts", "smalle.fon"},
                                       {"small fonts", "smallf.fon"},
                                       {"ms sans serif", "sserife.fon"},
                                       {"ms sans serif", "sseriff.fon"},
                                       {"courier", "coure.fon"},
                                       {"courier", "courf.fon"}})
        for (auto& strike : loadBitmapFace(file)) (*files)[family].push_back(std::move(strike));
    render::setGlyphRasterizer([files](const render::DsFace& face, char32_t cp, render::GlyphMask& out) {
        if (face.bitmap) {
            auto it = files->find(face.family);
            if (it != files->end() && render::fonGlyph(it->second, face, cp, out)) return true;
        }
        QFont f;
        if (face.bitmap) {
            const bool mono = face.family == "courier";
            QString want = face.family == "small fonts" ? "Small Fonts" : mono ? "Courier" : "MS Sans Serif";
            f = QFont(want);
            if (!sameFamily(want, QFontInfo(f).family())) {
                f = QFontDatabase::systemFont(mono ? QFontDatabase::FixedFont : QFontDatabase::GeneralFont);
                f.setStyleHint(mono ? QFont::Monospace : QFont::SansSerif);
            }
            f.setStyleStrategy(QFont::NoAntialias);
        } else {
            f = QFont(QString::fromStdString(face.family));
        }
        f.setPixelSize(std::max(1, face.em));
        f.setBold(face.bold);
        f.setItalic(face.italic);
        f.setKerning(false);
        f.setHintingPreference(QFont::PreferFullHinting);
        QFontMetrics m(f);
        const QString s = QString::fromUcs4(&cp, 1);
        // Room for overhang on either side of the advance. The baseline sits at the face's ascent, so every face on a
        // line shares the layout's baseline.
        const int pad = std::max(2, face.em / 3);
        const int w = std::max(1, m.horizontalAdvance(s)) + 2 * pad;
        const int h = std::max(face.height, m.ascent() + m.descent()) + 2 * pad;
        QImage img(w, h, QImage::Format_Alpha8);
        img.fill(0);
        {
            QPainter p(&img);
            p.setFont(f);
            p.setPen(QColor(0, 0, 0, 255));
            p.drawText(QPointF(pad, pad + face.ascent), s);
        }
        out.w = w;
        out.h = h;
        out.left = -pad;
        out.top = -pad;
        out.a.resize(size_t(w) * size_t(h));
        for (int y = 0; y < h; y++) std::memcpy(&out.a[size_t(y) * size_t(w)], img.constScanLine(y), size_t(w));
        return true;
    });
}

}  // namespace fugue::shell
