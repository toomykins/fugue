// Parsing of .dmi icon files (PNG metadata, the legacy container) and plain PNG/GIF icons. Where the format is
// silent, the integer and float parsers use the usual decimal-parsing rules.
#include "render/dmi.h"
#include "render/parsedouble.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

#include "render/gif.h"
#include "render/jpeg.h"
#include "render/inflate.h"

namespace fugue::render {
namespace {

constexpr size_t kMaxDescription = 16u << 20;  // cap on the inflated zTXt Description
constexpr uint32_t kMaxImageDim = 1u << 15;    // IHDR width/height cap
constexpr int64_t kMaxCellDim = 8192;
constexpr int64_t kDefaultCellDim = 32;  // cell size when the metadata omits width/height
constexpr int64_t kMaxDirs = 8;
constexpr int64_t kMaxFrames = 1 << 16;
// Cap on an animated GIF's composited frames.
constexpr size_t kMaxGifAnimBytes = size_t(512) << 20;

ParseResult fail(const char* cls, std::string detail) {
    ParseResult r;
    r.errClass = cls;
    r.errDetail = std::move(detail);
    return r;
}

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) a++;
    while (b > a && isSpace(s[b - 1])) b--;
    return std::string(s.substr(a, b - a));
}

// A syntax error gives 0; a range error clamps.
// `ok` reports "no error at all" (the hotspot list skips any error).
int64_t goAtoi(const std::string& s, bool* ok = nullptr) {
    if (ok) *ok = false;
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    if (i >= s.size()) return 0;
    uint64_t v = 0;
    const uint64_t limit = uint64_t(INT64_MAX) + unsigned(neg);
    bool range = false;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        unsigned digit = unsigned(s[i] - '0');
        if (v > (limit - digit) / 10) {
            range = true;
            v = limit;
        } else if (!range)
            v = v * 10 + digit;
    }
    if (ok) *ok = !range;
    if (neg && v == uint64_t(INT64_MAX) + 1) return INT64_MIN;
    return neg ? -int64_t(v) : int64_t(v);
}

// Plain decimals only ("12", "1.5", ".5", "1.", "1e3", signs). Anything else (inf,
// nan, hex floats, garbage, overflow) is skipped.
bool goParseFloat(const std::string& s, double& out) {
    size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        i++;
        digits++;
    }
    if (i < s.size() && s[i] == '.') {
        i++;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            digits++;
        }
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
        size_t ed = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            i++;
            ed++;
        }
        if (!ed) return false;
    }
    if (i != s.size()) return false;
    const char* b = s.data();
    if (*b == '+') b++;
    auto r = parseDouble(b, s.data() + s.size(), out);
    if (r.ec == std::errc::result_out_of_range) return false;  // ±Inf with an error -> skipped
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(',', a);
        out.push_back(trim(std::string_view(s).substr(a, b == std::string::npos ? std::string::npos : b - a)));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

// The "# BEGIN DMI" ... "# END DMI" block. Returns an error class, or nullptr.
const char* parseMetadata(const std::string& text, Icon& icon, std::string& detail) {
    IconState* cur = nullptr;
    bool seenBegin = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view raw(text.data() + pos, (nl == std::string::npos ? text.size() : nl) - pos);
        bool last = nl == std::string::npos;
        pos = last ? text.size() + 1 : nl + 1;
        if (last && raw.empty()) break;  // a trailing newline does not make an extra line
        std::string line = trim(raw);
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (line.find("BEGIN DMI") != std::string::npos) seenBegin = true;
            if (line.find("END DMI") != std::string::npos) break;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(std::string_view(line).substr(0, eq)), val = trim(std::string_view(line).substr(eq + 1));
        if (key == "state") {
            icon.states.emplace_back();
            cur = &icon.states.back();
            cur->name = unquote(val);
            continue;
        }
        if (!cur) {
            if (key == "version")
                icon.version = val;
            else if (key == "width")
                icon.width = int(goAtoi(val));
            else if (key == "height")
                icon.height = int(goAtoi(val));
            continue;
        }
        if (key == "dirs")
            cur->dirs = goAtoi(val);
        else if (key == "frames")
            cur->frames = goAtoi(val);
        else if (key == "delay") {
            std::vector<double> v;
            for (auto& part : splitComma(val)) {
                double d;
                if (!part.empty() && goParseFloat(part, d)) v.push_back(d);
            }
            cur->hasDelays = !v.empty();
            cur->delays = std::move(v);
        } else if (key == "movement")
            cur->movement = val == "1";
        else if (key == "rewind")
            cur->rewind = val == "1";
        else if (key == "loop")
            cur->loop = goAtoi(val);
        else if (key == "hotspot") {
            std::vector<int64_t> v;
            for (auto& part : splitComma(val)) {
                bool ok;
                int64_t n = goAtoi(part, &ok);
                if (!part.empty() && ok) v.push_back(n);
            }
            cur->hasHotspot = !v.empty();
            cur->hotspot = std::move(v);
        }
    }
    if (!seenBegin) {
        detail = "no BEGIN DMI";
        return "no_metadata";
    }
    int64_t cell = 0;
    for (auto& s : icon.states) {
        if (s.dirs <= 0) s.dirs = 1;
        if (s.frames <= 0) s.frames = 1;
        if (s.dirs > kMaxDirs) {
            detail = "dirs exceeds cap";
            return "dirs_cap";
        }
        if (s.frames > kMaxFrames) {
            detail = "frames exceeds cap";
            return "frames_cap";
        }
        s.firstCell = cell;
        cell += s.dirs * s.frames;
    }
    return nullptr;
}

// Walks the chunks without checking CRCs; the first text chunk keyed "Description" wins.
// Returns nullptr and sets `text`, or an error class.
const char* extractDescription(const uint8_t* d, size_t n, std::string& text, std::string& detail) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n < 8 || std::memcmp(d, sig, 8) != 0) {
        detail = "not a PNG file";
        return "not_png";
    }
    size_t p = 8;
    while (p + 8 <= n) {
        size_t length = be32(d + p);
        const char* type = reinterpret_cast<const char*>(d + p + 4);
        size_t start = p + 8;
        if (start + length + 4 > n) {
            detail = "truncated PNG chunk";
            return "truncated_chunk";
        }
        const uint8_t* chunk = d + start;
        bool isZ = std::memcmp(type, "zTXt", 4) == 0, isT = std::memcmp(type, "tEXt", 4) == 0;
        if (isZ || isT) {
            const void* nul = length ? std::memchr(chunk, 0, length) : nullptr;
            if (nul) {
                size_t kl = size_t(static_cast<const uint8_t*>(nul) - chunk);
                if (kl == 11 && std::memcmp(chunk, "Description", 11) == 0) {
                    const uint8_t* rest = chunk + kl + 1;
                    size_t rl = length - kl - 1;
                    if (isT) {
                        text.assign(reinterpret_cast<const char*>(rest), rl);
                        return nullptr;
                    }
                    if (rl < 1) {
                        detail = "malformed zTXt chunk";
                        return "malformed_ztxt";
                    }
                    std::vector<uint8_t> out;
                    ZStatus z = zlibInflate(rest + 1, rl - 1, out, kMaxDescription);
                    if (z == ZStatus::Limit) {
                        detail = "Description exceeds cap";
                        return "description_cap";
                    }
                    if (z != ZStatus::Ok) {
                        detail = "zTXt zlib/inflate failed";
                        return "bad_zlib";
                    }
                    text.assign(out.begin(), out.end());
                    return nullptr;
                }
            }
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        p = start + length + 4;
    }
    detail = "no Description chunk";
    return "no_metadata";
}

bool dimsTooBig(const uint8_t* d, size_t n) {
    if (n < 24 || std::memcmp(d + 12, "IHDR", 4) != 0) return false;
    return be32(d + 16) > kMaxImageDim || be32(d + 20) > kMaxImageDim;
}

std::unique_ptr<Icon> plainIcon(RgbaImage&& img, const char* version) {
    auto ic = std::make_unique<Icon>();
    ic->version = version;
    ic->width = img.w;
    ic->height = img.h;
    ic->plainImage = true;
    ic->states.emplace_back();  // the unnamed default state, 1 dir, 1 frame
    ic->sheet = std::move(img);
    ic->sheetCols = 1;
    return ic;
}

// ---- legacy container ----

struct LegacyReader {
    const uint8_t* d;
    size_t n, o = 0;
    bool err = false;
    bool need(size_t k) {
        if (err || o + k > n) {
            err = true;
            return false;
        }
        return true;
    }
    uint32_t u8() {
        if (!need(1)) return 0;
        return d[o++];
    }
    uint32_t u16() {
        if (!need(2)) return 0;
        uint32_t v = d[o] | (d[o + 1] << 8);
        o += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24);
        o += 4;
        return v;
    }
    const uint8_t* bytes(size_t k) {
        if (!need(k)) return nullptr;
        const uint8_t* p = d + o;
        o += k;
        return p;
    }
};

int legacyBits(int ncolors) {
    if (ncolors <= 1) return 0;
    int b = 0, v = 1;
    while (v < ncolors) {
        v += v;
        b++;
    }
    return b;
}

std::vector<int> unpackIndices(const uint8_t* src, size_t srcLen, int bits, int n) {
    std::vector<int> out(size_t(n), 0);
    if (!bits) return out;
    for (int i = 0; i < n; i++) {
        int v = 0;
        for (int k = 0; k < bits; k++) {
            size_t bi = size_t(i) * size_t(bits) + size_t(k);
            if ((bi >> 3) >= srcLen) break;
            v |= ((src[bi >> 3] >> (bi & 7)) & 1) << k;
        }
        out[size_t(i)] = v;
    }
    return out;
}

constexpr int kLegacyDim = 32;
constexpr int kLegacyPixels = kLegacyDim * kLegacyDim;
constexpr int kMaxLegacyCells = 16384;

struct LegacyImage {
    uint32_t low = 0, high = 0;
    bool has = false;
    std::vector<int> idx;
};

// one cell's 1024 palette indices; false = malformed
bool readLegacyImage(LegacyReader& r, int bits, int ncolors, std::vector<int>& idx) {
    int nruns = int(r.u16());
    if (r.err) return false;
    if (nruns > kLegacyPixels) return false;
    if (nruns == 0) {
        size_t nb = (size_t(kLegacyPixels) * size_t(bits) + 7) / 8;
        const uint8_t* raw = r.bytes(nb);
        if (r.err) return false;
        idx = unpackIndices(raw, nb, bits, kLegacyPixels);
        for (int v : idx)
            if (v >= ncolors) return false;
        return true;
    }
    const uint8_t* lens = r.bytes(size_t(nruns));
    if (r.err) return false;
    int total = 0;
    for (int i = 0; i < nruns; i++) total += lens[i];
    if (total > kLegacyPixels) return false;
    int padded = (nruns + 7) / 8 * 8;
    size_t cb = size_t(padded) * size_t(bits) / 8;
    const uint8_t* cp = r.bytes(cb);
    if (r.err) return false;
    std::vector<int> cols = unpackIndices(cp, cb, bits, padded);
    idx.clear();
    idx.reserve(kLegacyPixels);
    for (int i = 0; i < nruns; i++) {
        if (cols[size_t(i)] >= ncolors) return false;
        for (int k = 0; k < lens[i]; k++) idx.push_back(cols[size_t(i)]);
    }
    while (idx.size() < size_t(kLegacyPixels)) idx.push_back(0);  // a short cell is transparent to the end
    return true;
}

ParseResult parseLegacy(const uint8_t* data, size_t n) {
    LegacyReader r{data, n};
    uint32_t version = r.u8();
    r.bytes(3);
    if (version != 4) return fail("legacy_version", "legacy container version is not 4");
    r.u8();  // icon-level default dir count
    int ncolors = int(r.u16());
    if (r.err) return fail("legacy_malformed", "truncated");
    if (ncolors > 256) return fail("legacy_malformed", "palette > 256");
    std::vector<std::array<uint8_t, 4>> pal;
    pal.resize(static_cast<size_t>(ncolors));
    for (int i = 0; i < ncolors; i++) {
        const uint8_t* b = r.bytes(3);
        if (r.err) return fail("legacy_malformed", "truncated palette");
        pal[size_t(i)] = {b[0], b[2], b[1], uint8_t(i == 0 ? 0 : 255)};  // stored R,B,G; entry 0 is the transparent key
    }
    int bits = legacyBits(ncolors);
    int nstates = int(r.u16());
    if (r.err) return fail("legacy_malformed", "truncated");

    auto icon = std::make_unique<Icon>();
    icon->version = "legacy 4";
    icon->width = icon->height = kLegacyDim;
    std::vector<LegacyImage> flat;
    flat.reserve(64);
    std::vector<std::pair<size_t, size_t>> stateImgs;  // (first flat index, count)
    int64_t total = 0;
    for (int si = 0; si < nstates; si++) {
        int dirs = int(r.u16()), frameCount = int(r.u16()), delay = int(r.u16());
        r.u8();  // flags: not interpreted
        int nameLen = int(r.u16());
        std::string name;
        if (nameLen > 0) {
            const uint8_t* nb = r.bytes(size_t(nameLen));
            if (r.err) return fail("legacy_malformed", "truncated name");
            size_t l = 0;
            while (l < size_t(nameLen) && nb[l]) l++;
            name.assign(reinterpret_cast<const char*>(nb), l);
        }
        if (r.err) return fail("legacy_malformed", "truncated state");
        if (dirs <= 0 || frameCount <= 0) return fail("legacy_malformed", "state has no dirs/frames");
        if (dirs > int(kMaxDirs)) return fail("legacy_malformed", "state dirs > 8");
        int64_t cells = int64_t(dirs) * frameCount;
        total += cells;
        if (total > kMaxLegacyCells) return fail("legacy_malformed", "cell cap");
        size_t first = flat.size();
        for (int64_t j = 0; j < cells; j++) {
            uint32_t key = r.u32();
            if (r.err) return fail("legacy_malformed", "truncated key");
            LegacyImage im;
            im.low = key & 0xffff;
            im.high = key >> 16;
            if (im.high == 0) {
                if (!readLegacyImage(r, bits, ncolors, im.idx)) return fail("legacy_malformed", "bad image data");
                im.has = true;
            }
            flat.push_back(std::move(im));
        }
        stateImgs.emplace_back(first, size_t(cells));
        IconState st;
        st.name = name;
        st.dirs = dirs;
        st.frames = frameCount;
        st.hasDelays = true;
        st.delays.assign(size_t(frameCount), double(delay < 1 ? 1 : delay));  // max(delay, 1)
        st.firstCell = total - cells;
        icon->states.push_back(std::move(st));
    }
    // back-references: an earlier image with high==0 and the same low; transform 4 is a vertical flip
    for (size_t i = 0; i < flat.size(); i++) {
        if (flat[i].has) continue;
        const LegacyImage* src = nullptr;
        for (size_t j = 0; j < i; j++)
            if (flat[j].high == 0 && flat[j].low == flat[i].low && flat[j].has) {
                src = &flat[j];
                break;
            }
        if (!src) return fail("legacy_transform", "back-reference to a missing image");
        if (flat[i].high != 4) return fail("legacy_transform", "unverified transform code");
        std::vector<int> o(src->idx.size());
        for (int y = 0; y < kLegacyDim; y++)
            std::copy(src->idx.begin() + (kLegacyDim - 1 - y) * kLegacyDim,
                      src->idx.begin() + (kLegacyDim - y) * kLegacyDim, o.begin() + y * kLegacyDim);
        flat[i].idx = std::move(o);
        flat[i].has = true;
    }
    // Lay the cells out in a sheet, indexed the same way as the PNG path.
    int cols = 1;
    while (int64_t(cols) * cols < total) cols++;
    int rows = int((total + cols - 1) / cols);
    if (rows < 1) rows = 1;
    icon->sheetCols = cols;
    icon->sheet.w = cols * kLegacyDim;
    icon->sheet.h = rows * kLegacyDim;
    icon->sheet.px.assign(size_t(icon->sheet.w) * size_t(icon->sheet.h) * 4, 0);
    if (ncolors == 0 && total > 0) return fail("legacy_malformed", "cells but no palette");
    for (size_t si = 0; si < icon->states.size(); si++) {
        const IconState& st = icon->states[si];
        for (int64_t f = 0; f < st.frames; f++)
            for (int64_t dd = 0; dd < st.dirs; dd++) {
                const LegacyImage& im = flat[stateImgs[si].first + size_t(f * st.dirs + dd)];
                int64_t cell = st.firstCell + f * st.dirs + dd;
                int x0 = int(cell % cols) * kLegacyDim, y0 = int(cell / cols) * kLegacyDim;
                for (int y = 0; y < kLegacyDim; y++)
                    for (int x = 0; x < kLegacyDim; x++) {
                        const auto& c = pal[size_t(im.idx[size_t(y * kLegacyDim + x)])];
                        std::memcpy(&icon->sheet.px[(size_t(y0 + y) * size_t(icon->sheet.w) + size_t(x0 + x)) * 4],
                                    c.data(), 4);
                    }
            }
    }
    // trailer: a flag byte then, if set, one [u16][u8] per image; consumed for framing only
    if (r.o < r.n) {
        if (r.u8() > 0)
            for (size_t i = 0; i < flat.size(); i++) {
                r.u16();
                r.u8();
            }
    }
    if (r.err) return fail("legacy_malformed", "truncated trailer");
    ParseResult ok;
    ok.icon = std::move(icon);
    return ok;
}

}  // namespace

int Icon::stateIndex(const std::string& name) const {
    for (size_t i = 0; i < states.size(); i++)
        if (states[i].name == name) return int(i);
    return -1;
}

bool Icon::frame(const IconState& s, int64_t dirSlot, int64_t fr, std::vector<uint8_t>& out, std::string* err) const {
    int64_t cell = s.cellIndex(dirSlot, fr);
    if (cell < 0) {
        if (err) *err = "dir/frame out of range";
        return false;
    }
    int64_t x0 = (cell % sheetCols) * width, y0 = (cell / sheetCols) * height;
    if (x0 + width > sheet.w || y0 + height > sheet.h) {
        if (err) *err = "cell outside sheet";
        return false;
    }
    out.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; y++)
        std::memcpy(&out[size_t(y) * size_t(width) * 4], &sheet.px[(size_t(y0 + y) * size_t(sheet.w) + size_t(x0)) * 4],
                    size_t(width) * 4);
    return true;
}

ParseResult parseIcon(const uint8_t* d, size_t n) {
    // Sniff order: legacy container, then GIF/JPEG/BMP by magic, then PNG.
    if (n >= 4 && d[0] >= 2 && d[0] <= 4 && d[1] == 'D' && d[2] == 'M' && d[3] == 'I') return parseLegacy(d, n);
    if (n >= 6 && (std::memcmp(d, "GIF87a", 6) == 0 || std::memcmp(d, "GIF89a", 6) == 0)) {
        std::string why;
        // In stock BYOND 516 an animated GIF is an animated icon: one unnamed state of every frame, composited on the
        // logical screen, with the GIF's delays and loop count (gif.h). A single-frame GIF takes the still path below.
        GifAnimation anim;
        // A GIF the animator refuses (for example, its frames would exceed kMaxGifAnimBytes) still draws its first frame.
        const bool animated = decodeGifAnimation(d, n, anim, why, kMaxGifAnimBytes);
        if (animated && anim.frames > 1) {
            if (anim.w > kMaxCellDim || anim.h > kMaxCellDim || anim.frames > kMaxFrames)
                return fail("plain_cap", "plain image size");
            auto ic = std::make_unique<Icon>();
            ic->version = "gif";
            ic->width = anim.w;
            ic->height = anim.h;
            ic->plainImage = true;
            IconState st;
            st.frames = anim.frames;
            st.hasDelays = true;
            st.delays = std::move(anim.delays);
            st.loop = anim.loop;
            ic->states.push_back(std::move(st));
            ic->sheet = std::move(anim.strip);  // frames stacked vertically: cell i at row i*height
            ic->sheetCols = 1;
            ParseResult ok;
            ok.icon = std::move(ic);
            return ok;
        }
        RgbaImage img;
        if (!decodeGifFirstFrame(d, n, img, why)) return fail("gif_decode", why);
        if (img.w <= 0 || img.h <= 0 || img.w > kMaxCellDim || img.h > kMaxCellDim)
            return fail("plain_cap", "plain image size");
        ParseResult ok;
        ok.icon = plainIcon(std::move(img), "gif");
        return ok;
    }
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) {
        // A JPEG is a plain one-frame opaque icon, decoded as libjpeg does (render/jpeg.h). It is sniffed by magic, so a
        // JPEG under a .gif name is one too.
        RgbaImage img;
        std::string why;
        if (!decodeJpeg(d, n, img, why, uint64_t(kMaxCellDim) * uint64_t(kMaxCellDim))) return fail("jpeg_decode", why);
        if (img.w > kMaxCellDim || img.h > kMaxCellDim) return fail("plain_cap", "plain image size");
        ParseResult ok;
        ok.icon = plainIcon(std::move(img), "jpeg");
        return ok;
    }
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return fail("bmp_refused", "BMP refused");

    std::string text, detail;
    const char* cls = extractDescription(d, n, text, detail);
    if (cls) {
        if (std::strcmp(cls, "no_metadata") == 0) {
            // A PNG with no DMI Description is a plain image.
            RgbaImage img;
            std::string why;
            if (!dimsTooBig(d, n) && decodePng(d, n, img, why) == PngStatus::Ok && img.w > 0 && img.h > 0 &&
                img.w <= kMaxCellDim && img.h <= kMaxCellDim) {
                ParseResult ok;
                ok.icon = plainIcon(std::move(img), "png");
                return ok;
            }
        }
        return fail(cls, detail);
    }
    auto icon = std::make_unique<Icon>();
    cls = parseMetadata(text, *icon, detail);
    if (cls) return fail(cls, detail);
    if (icon->width == 0) icon->width = int(kDefaultCellDim);
    if (icon->height == 0) icon->height = int(kDefaultCellDim);
    if (dimsTooBig(d, n)) return fail("dimension_cap", "IHDR dimensions exceed cap");
    std::string why;
    PngStatus ps = decodePng(d, n, icon->sheet, why);
    if (ps != PngStatus::Ok) return fail(ps == PngStatus::Unsupported ? "png_unsupported" : "png_decode", why);
    if (icon->width <= 0 || icon->height <= 0) return fail("cell_size", "invalid cell size");
    if (icon->width > kMaxCellDim || icon->height > kMaxCellDim) return fail("cell_cap", "cell size exceeds cap");
    icon->sheetCols = icon->sheet.w / icon->width;
    if (icon->sheetCols <= 0) return fail("sheet_narrow", "sheet narrower than a cell");
    ParseResult ok;
    ok.icon = std::move(icon);
    return ok;
}

// The frame a free-running state shows `elapsed` deciseconds into its clock (render/scene.cpp passes deciseconds since
// the Unix epoch, or since the icon start of the atom, turf, image or screen object for a finite loop). rewind plays
// 0..n-1 then back down to 1; loop n holds the last frame after n plays.
int frameAtTime(const IconState& st, double elapsed) {
    if (st.frames <= 1) return 0;
    struct Step {
        int frame;
        double delay;
    };
    std::vector<Step> steps;
    auto delay = [&](int64_t i) { return (i >= 0 && size_t(i) < st.delays.size()) ? st.delays[size_t(i)] : 1.0; };
    for (int64_t i = 0; i < st.frames; i++) steps.push_back({int(i), delay(i)});
    if (st.rewind && st.frames > 2)
        for (int64_t i = st.frames - 2; i >= 1; i--) steps.push_back({int(i), delay(i)});
    int last = steps.back().frame;
    double total = 0;
    for (auto& s : steps) total += s.delay;
    if (total <= 0) return last;
    if (elapsed < 0) return steps.front().frame;
    if (st.loop > 0 && elapsed >= total * double(st.loop)) return last;
    double t = std::fmod(elapsed, total);
    double acc = 0;
    for (auto& s : steps) {
        if (t < acc + s.delay) return s.frame;
        acc += s.delay;
    }
    return last;
}

int dirIndex(int dirs, int byondDir) {
    // Dir slots: S=0 N=1 E=2 W=3 SE=4 SW=5 NE=6 NW=7; BYOND dir bits: N=1 S=2 E=4 W=8.
    enum { N = 1, S = 2, E = 4, W = 8 };
    switch (dirs) {
        case 1: return 0;
        case 4:
            switch (byondDir) {
                case S: return 0;
                case N: return 1;
                case E:
                case N | E:
                case S | E: return 2;  // diagonals collapse to their horizontal component
                case W:
                case N | W:
                case S | W: return 3;
                default: return 0;
            }
        case 8:
            switch (byondDir) {
                case S: return 0;
                case N: return 1;
                case E: return 2;
                case W: return 3;
                case S | E: return 4;
                case S | W: return 5;
                case N | E: return 6;
                case N | W: return 7;
                default: return 0;
            }
        default: return -1;
    }
}

}  // namespace fugue::render
