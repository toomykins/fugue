// Bounded legacy-v4 DMI, GIF and JPEG decoding.
// PNG/DMI metadata parsing and playback remain in shell/dmiicon.cpp.
#include "render/dmi.h"
#include "render/gif.h"
#include "render/jpeg.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace fugue::assets {
namespace {
constexpr int64_t kMaxCellDim = 8192, kMaxDirs = 8, kMaxFrames = 65536;
// An animated GIF's composited frames may fill the sheet every other icon may have, 64 million pixels (shell/dmiicon.cpp).
// Stock BYOND 516 draws a 480x480 GIF of 138 frames (121 MiB of frames) as a game's lobby splash
// (tests/data/splash-gif-world).
constexpr size_t kMaxGifAnimBytes = size_t(256) << 20;
ParseResult fail(const char* cls, std::string detail) {
    ParseResult r;
    r.errClass = cls;
    r.errDetail = std::move(detail);
    return r;
}

std::unique_ptr<Icon> plainIcon(RgbaImage&& img, const char* version) {
    auto ic = std::make_unique<Icon>();
    ic->version = version;
    ic->width = img.w;
    ic->height = img.h;
    ic->plainImage = true;
    ic->states.emplace_back();   // the unnamed default state, 1 dir, 1 frame
    ic->sheet = std::move(img);
    ic->sheetCols = 1;
    return ic;
}

// ---- legacy container ----

struct LegacyReader {
    const uint8_t* d;
    size_t n, o = 0;
    bool err = false;
    bool need(size_t k) { if (err || o + k > n) { err = true; return false; } return true; }
    uint32_t u8() { if (!need(1)) return 0; return d[o++]; }
    uint32_t u16() { if (!need(2)) return 0; uint32_t v = d[o] | (d[o + 1] << 8); o += 2; return v; }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24); o += 4; return v; }
    const uint8_t* bytes(size_t k) { if (!need(k)) return nullptr; const uint8_t* p = d + o; o += k; return p; }
};

int legacyBits(int ncolors) {
    if (ncolors <= 1) return 0;
    int b = 0, v = 1;
    while (v < ncolors) { v += v; b++; }
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
        for (int v : idx) if (v >= ncolors) return false;
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
    while (idx.size() < size_t(kLegacyPixels)) idx.push_back(0);   // a short cell is transparent to the end
    return true;
}

ParseResult parseLegacy(const uint8_t* data, size_t n, uint64_t maxPixels) {
    LegacyReader r{data, n};
    uint32_t version = r.u8();
    r.bytes(3);
    if (version != 4) return fail("legacy_version", "legacy container version is not 4");
    r.u8();   // icon-level default dir count
    int ncolors = int(r.u16());
    if (r.err) return fail("legacy_malformed", "truncated");
    if (ncolors > 256) return fail("legacy_malformed", "palette > 256");
    std::vector<std::array<uint8_t, 4>> pal;
    pal.resize(static_cast<size_t>(ncolors));
    for (int i = 0; i < ncolors; i++) {
        const uint8_t* b = r.bytes(3);
        if (r.err) return fail("legacy_malformed", "truncated palette");
        pal[size_t(i)] = {b[0], b[2], b[1], uint8_t(i == 0 ? 0 : 255)};   // stored R,B,G; entry 0 is the transparent key
    }
    int bits = legacyBits(ncolors);
    int nstates = int(r.u16());
    if (r.err) return fail("legacy_malformed", "truncated");

    auto icon = std::make_unique<Icon>();
    icon->version = "legacy 4";
    icon->width = icon->height = kLegacyDim;
    std::vector<LegacyImage> flat;
    flat.reserve(64);
    std::vector<std::pair<size_t, size_t>> stateImgs;   // (first flat index, count)
    int64_t total = 0;
    for (int si = 0; si < nstates; si++) {
        int dirs = int(r.u16()), frameCount = int(r.u16()), delay = int(r.u16());
        if (r.u8() != 0) return fail("legacy_flags", "unverified state flags");
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
        if (dirs != 1 && dirs != 4 && dirs != kMaxDirs) return fail("legacy_malformed", "unsupported dirs");
        int64_t cells = int64_t(dirs) * frameCount;
        total += cells;
        if (total > kMaxLegacyCells) return fail("legacy_malformed", "cell cap");
        if (uint64_t(total) * kLegacyPixels > maxPixels) return fail("pixel_limit", "legacy cell budget");
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
        st.delays.assign(size_t(frameCount), double(delay < 1 ? 1 : delay));   // max(delay, 1)
        st.firstCell = total - cells;
        icon->states.push_back(std::move(st));
    }
    // back-references: an earlier image with high==0 and the same low; transform 4 is a vertical flip
    std::map<uint32_t, const LegacyImage*> originals;
    for (size_t i = 0; i < flat.size(); i++) {
        if (flat[i].has) { originals.emplace(flat[i].low, &flat[i]); continue; }
        const auto found = originals.find(flat[i].low);
        const LegacyImage* src = found == originals.end() ? nullptr : found->second;
        if (!src) return fail("legacy_transform", "back-reference to a missing image");
        if (flat[i].high != 4) return fail("legacy_transform", "unverified transform code");
        std::vector<int> o(src->idx.size());
        for (int y = 0; y < kLegacyDim; y++)
            std::copy(src->idx.begin() + (kLegacyDim - 1 - y) * kLegacyDim, src->idx.begin() + (kLegacyDim - y) * kLegacyDim, o.begin() + y * kLegacyDim);
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
    if (uint64_t(icon->sheet.w) * icon->sheet.h > maxPixels) return fail("pixel_limit", "legacy packed sheet budget");
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
                        std::memcpy(&icon->sheet.px[(size_t(y0 + y) * size_t(icon->sheet.w) + size_t(x0 + x)) * 4], c.data(), 4);
                    }
            }
    }
    // trailer: a flag byte then, if set, one [u16][u8] per image; consumed for framing only
    if (r.o < r.n) {
        if (r.u8() > 0)
            for (size_t i = 0; i < flat.size(); i++) { r.u16(); r.u8(); }
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
    if (cell < 0) { if (err) *err = "dir/frame out of range"; return false; }
    int64_t x0 = (cell % sheetCols) * width, y0 = (cell / sheetCols) * height;
    if (x0 + width > sheet.w || y0 + height > sheet.h) { if (err) *err = "cell outside sheet"; return false; }
    out.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; y++)
        std::memcpy(&out[size_t(y) * size_t(width) * 4], &sheet.px[(size_t(y0 + y) * size_t(sheet.w) + size_t(x0)) * 4], size_t(width) * 4);
    return true;
}

ParseResult parseIcon(const uint8_t* d, size_t n, uint64_t maxPixels) {
    if (n > 128 * 1024 * 1024) return fail("input_cap", "encoded image size");
    // Sniff order: legacy container, then GIF/JPEG/BMP by magic, then PNG.
    if (n >= 4 && d[0] >= 2 && d[0] <= 4 && d[1] == 'D' && d[2] == 'M' && d[3] == 'I') return parseLegacy(d, n, maxPixels);
    if (n >= 6 && (std::memcmp(d, "GIF87a", 6) == 0 || std::memcmp(d, "GIF89a", 6) == 0)) {
        std::string why;
        // In stock BYOND 516 an animated GIF is an animated icon: one unnamed state of every frame, composited on the
        // logical screen, with the GIF's delays and loop count (gif.h). A single-frame GIF takes the still path below.
        GifAnimation anim;
        // Never turn an over-budget or malformed animation into a still image.
        const bool animated = decodeGifAnimation(d, n, anim, why, size_t(std::min<uint64_t>(kMaxGifAnimBytes, maxPixels * 4)));
        if (!animated) return fail(why == "animation exceeds the size cap" && maxPixels < kMaxGifAnimBytes / 4 ? "pixel_limit" : "gif_decode", why);
        if (animated && anim.frames > 1) {
            if (anim.w > kMaxCellDim || anim.h > kMaxCellDim || anim.frames > kMaxFrames) return fail("plain_cap", "plain image size");
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
            ic->sheet = std::move(anim.strip);   // frames stacked vertically: cell i at row i*height
            ic->sheetCols = 1;
            ParseResult ok;
            ok.icon = std::move(ic);
            return ok;
        }
        RgbaImage img;
        if (!decodeGifFirstFrame(d, n, img, why)) return fail("gif_decode", why);
        if (img.w <= 0 || img.h <= 0 || img.w > kMaxCellDim || img.h > kMaxCellDim) return fail("plain_cap", "plain image size");
        if (uint64_t(img.w) * img.h > maxPixels) return fail("pixel_limit", "GIF first-frame budget");
        ParseResult ok;
        ok.icon = plainIcon(std::move(img), "gif");
        return ok;
    }
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) {
        // A JPEG is a plain one-frame opaque icon, decoded as libjpeg does (render/jpeg.h). It is sniffed by magic, so a
        // JPEG under a .gif name is one too.
        RgbaImage img;
        std::string why;
        if (!decodeJpeg(d, n, img, why, std::min(maxPixels, uint64_t(kMaxCellDim) * uint64_t(kMaxCellDim))))
            return fail(why == "jpeg: image size" && maxPixels < uint64_t(kMaxCellDim) * kMaxCellDim ? "pixel_limit" : "jpeg_decode", why);
        if (img.w > kMaxCellDim || img.h > kMaxCellDim) return fail("plain_cap", "plain image size");
        ParseResult ok;
        ok.icon = plainIcon(std::move(img), "jpeg");
        return ok;
    }
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return fail("bmp_refused", "BMP refused");

    return fail("unsupported_image", "not a supported GIF, JPEG or legacy-v4 DMI");
}
} // namespace fugue::assets
