// DDMI recipes: parse and evaluate. See ddmi.h.
#include "render/ddmi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/image.h"

namespace fugue::assets {

namespace {

constexpr uint8_t kOpIconRef = 0x01, kOpColor = 0x03, kOpNumber = 0x04, kOpState = 0x05, kOpDup = 0x06, kOpNop = 0x3f,
                  kOpDir = 0x40, kOpInsert = 0x41, kOpBlend = 0x42, kOpIntensity = 0x43, kOpFlip = 0x44, kOpTurn = 0x45,
                  kOpTurnF = 0x46, kOpShift = 0x47, kOpSwap = 0x48, kOpDrawBox = 0x49, kOpMapColors = 0x4a,
                  kOpScale = 0x4b, kOpCrop = 0x4c, kOpBlendAt = 0x4d, kOpInsertF = 0x4e;

bool payloadLen(uint8_t tag, const uint8_t* d, size_t n, size_t i, size_t& out, std::string* err) {
    size_t avail = n - (i + 1);
    auto fixed = [&](size_t w) {
        if (avail < w) {
            if (err) *err = "DDMI op wants more bytes than remain";
            return false;
        }
        out = w;
        return true;
    };
    switch (tag) {
        case kOpIconRef: return fixed(5);
        case kOpState: {
            const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(d + i + 1, 0, avail));
            if (!nul) {
                if (err) *err = "DDMI state name unterminated";
                return false;
            }
            out = size_t(nul - (d + i + 1)) + 1;
            return true;
        }
        case kOpDir: return fixed(3);
        case kOpDup:
        case kOpNop: return fixed(0);
        case kOpColor: return fixed(4);
        case kOpNumber: return fixed(4);
        case kOpInsert:
        case kOpInsertF: return fixed(7);
        case kOpBlend: return fixed(1);
        case kOpIntensity: return fixed(12);
        case kOpFlip: return fixed(1);
        case kOpTurn:
        case kOpTurnF: return fixed(4);
        case kOpShift: return fixed(4);
        case kOpSwap: return fixed(1);
        case kOpDrawBox: return fixed(8);
        case kOpMapColors:
            if (avail < 1) {
                if (err) *err = "DDMI MapColors op has no count byte";
                return false;
            }
            return fixed(1 + 4 * std::min<size_t>(d[i + 1], 20));
        case kOpScale: return fixed(4);
        case kOpCrop: return fixed(8);
        case kOpBlendAt: return fixed(5);
        default:
            if (err) {
                char t[8];
                std::snprintf(t, sizeof t, "0x%02x", tag);
                *err = std::string("DDMI unrecognised op tag ") + t;
            }
            return false;
    }
}

// Straight-alpha overlay of `top` on `bottom`.
Rgba8 overComposite(Rgba8 bottom, Rgba8 top) {
    if (bottom.a == 0) return top;
    double sa = double(top.a) / 255;
    double da = double(bottom.a) / 255;
    double oa = sa + da * (1 - sa);
    auto round8 = [](double v) -> uint8_t {
        if (v <= 0) return 0;
        if (v >= 255) return 255;
        return uint8_t(std::round(v));
    };
    auto ch = [&](uint8_t topC, uint8_t bottomC) {
        uint8_t premul = round8(double(topC) * sa);
        return clamp8(double(premul) + double(bottomC) * (1 - sa));
    };
    return Rgba8{ch(top.r, bottom.r), ch(top.g, bottom.g), ch(top.b, bottom.b), clamp8(oa * 255)};
}
enum {
    kIconAdd = 0,
    kIconSubtract = 1,
    kIconMultiply = 2,
    kIconOverlay = 3,
    kIconAnd = 4,
    kIconOr = 5,
    kIconUnderlay = 6
};
// R/G/B independently clamped, alpha the PRODUCT 255*sa*da
template <class Op>
Rgba8 channelBlend(Rgba8 self, Rgba8 other, Op op) {
    double sa = double(self.a) / 255;
    double da = double(other.a) / 255;
    return Rgba8{clamp8(op(self.r, other.r)), clamp8(op(self.g, other.g)), clamp8(op(self.b, other.b)),
                 clamp8(255 * sa * da)};
}
// The clamped colour add with the UNION alpha
Rgba8 orBlend(Rgba8 self, Rgba8 other) {
    double sa = double(self.a) / 255;
    double da = double(other.a) / 255;
    double oa = sa + da * (1 - sa);
    auto add = [](uint8_t a, uint8_t b) { return clamp8(double(a) + double(b)); };
    return Rgba8{add(self.r, other.r), add(self.g, other.g), add(self.b, other.b), clamp8(oa * 255)};
}
// One pixel under every IconBlendMode, in DM's own numbering (ADD 0 SUBTRACT 1 MULTIPLY 2 OVERLAY 3 AND 4 OR 5 UNDERLAY 6).
Rgba8 blendPixelIcon(int mode, Rgba8 self, Rgba8 other) {
    if (self.a == 0) { self.r = self.g = self.b = 0; }
    if (other.a == 0) { other.r = other.g = other.b = 0; }
    switch (mode) {
        case kIconOverlay: return overComposite(self, other);
        case kIconUnderlay: return overComposite(other, self);
        case kIconOr: return orBlend(self, other);
        case kIconAdd:
        case kIconAnd:  // DD's ICON_AND is byte-for-byte ICON_ADD
            return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) + double(b); });
        case kIconSubtract:
            return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) - double(b); });
        case kIconMultiply:
            return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) * double(b) / 255; });
        default: return self;
    }
}

// The wire blend byte DD 516 writes -> the DM IconBlendMode it means (mapped, never cast):
// ADD 0, SUBTRACT 1, OR 2, AND 3, MULTIPLY 4, OVERLAY 7, UNDERLAY 8.
bool wireBlend(uint8_t b, int& mode) {
    switch (b) {
        case 0: mode = kIconAdd; return true;
        case 1: mode = kIconSubtract; return true;
        case 2: mode = kIconOr; return true;
        case 3: mode = kIconAnd; return true;
        case 4: mode = kIconMultiply; return true;
        case 7: mode = kIconOverlay; return true;
        case 8: mode = kIconUnderlay; return true;
    }
    return false;
}

Rgba8 pixelAt(const RgbaImage& im, int x, int y) {
    const uint8_t* p = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
    return Rgba8{p[0], p[1], p[2], p[3]};
}
void setPixel(RgbaImage& im, int x, int y, Rgba8 c) {
    uint8_t* p = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
}
bool cellRect(const Icon& ic, int64_t cell, int& x0, int& y0) {
    x0 = int((cell % ic.sheetCols) * ic.width);
    y0 = int((cell / ic.sheetCols) * ic.height);
    return x0 >= 0 && y0 >= 0 && x0 + ic.width <= ic.sheet.w && y0 + ic.height <= ic.sheet.h;
}

// Every pixel of the whole sheet blended with a flat colour.
std::unique_ptr<Icon> blendedColor(const Icon& ic, int mode, Rgba8 col) {
    auto o = std::make_unique<Icon>(ic);
    for (int y = 0; y < o->sheet.h; y++)
        for (int x = 0; x < o->sheet.w; x++)
            setPixel(o->sheet, x, y, blendPixelIcon(mode, pixelAt(ic.sheet, x, y), col));
    return o;
}

// A cell's pixels, row 0 at the top.
struct Cell {
    int w = 0, h = 0;
    std::vector<Rgba8> px;
    Rgba8& at(int x, int y) { return px[size_t(y) * size_t(w) + size_t(x)]; }
    Rgba8 at(int x, int y) const { return px[size_t(y) * size_t(w) + size_t(x)]; }
};
Cell blankCell(int w, int h) { return Cell{w, h, std::vector<Rgba8>(size_t(w) * size_t(h), Rgba8{0, 0, 0, 0})}; }

// One icon_state as cells, frame-major (f * dirs + d), each with its own delay.
struct StateCells {
    IconState meta;
    std::vector<Cell> cells;
    std::vector<double> delay;
    Cell& at(int64_t d, int64_t f) { return cells[size_t(f * meta.dirs + d)]; }
    const Cell& at(int64_t d, int64_t f) const { return cells[size_t(f * meta.dirs + d)]; }
};
struct Cells {
    int w = 0, h = 0;
    std::vector<StateCells> states;
};

Cells explode(const Icon& ic) {
    Cells out;
    out.w = ic.width;
    out.h = ic.height;
    for (const IconState& st : ic.states) {
        StateCells sc;
        sc.meta = st;
        for (int64_t f = 0; f < st.frames; f++)
            for (int64_t d = 0; d < st.dirs; d++) {
                Cell c = blankCell(ic.width, ic.height);
                int64_t cell = st.cellIndex(d, f);
                int rx, ry;
                if (cell >= 0 && cellRect(ic, cell, rx, ry))
                    for (int y = 0; y < ic.height; y++)
                        for (int x = 0; x < ic.width; x++) c.at(x, y) = pixelAt(ic.sheet, rx + x, ry + y);
                sc.cells.push_back(std::move(c));
                sc.delay.push_back(size_t(f) < st.delays.size() ? st.delays[size_t(f)] : 1.0);
            }
        out.states.push_back(std::move(sc));
    }
    return out;
}

std::unique_ptr<Icon> implode(const Cells& c, const std::string& version) {
    auto o = std::make_unique<Icon>();
    o->version = version;
    o->width = c.w;
    o->height = c.h;
    size_t total = 0;
    for (const StateCells& s : c.states) total += s.cells.size();
    // Bounded near-square packing rather than an arbitrarily wide one-row image.
    o->sheetCols = std::max(1, int(std::ceil(std::sqrt(double(total)))));
    o->sheet.w = o->sheetCols * c.w;
    o->sheet.h = std::max(1, int((total + o->sheetCols - 1) / o->sheetCols)) * c.h;
    o->sheet.px.assign(size_t(o->sheet.w) * size_t(o->sheet.h) * 4, 0);
    int64_t next = 0;
    for (const StateCells& s : c.states) {
        IconState st = s.meta;
        st.firstCell = next;
        st.delays.clear();
        for (int64_t f = 0; f < st.frames; f++) st.delays.push_back(s.delay[size_t(f * st.dirs)]);
        st.hasDelays = s.meta.hasDelays || !st.delays.empty();
        for (const Cell& cell : s.cells) {
            int x0 = int(next % o->sheetCols) * c.w, y0 = int(next / o->sheetCols) * c.h;
            for (int y = 0; y < c.h; y++)
                for (int x = 0; x < c.w; x++) setPixel(o->sheet, x0 + x, y0 + y, cell.at(x, y));
            next++;
        }
        o->states.push_back(std::move(st));
    }
    return o;
}

// icon(ic, state, dir, frame, moving), each < 0 or null when not given; null when the icon is left as it is. A state
// lacking the dir or frame is dropped; a named state loses its name, and a movement filter clears the movement flag.
std::unique_ptr<Icon> extractIcon(const Icon& ic, const std::string* state, int dir, int frame, int moving) {
    if (!state && dir < 0 && frame < 0 && moving < 0) return nullptr;
    if (state && ic.stateIndex(*state) < 0) return nullptr;
    Cells c = explode(ic);
    std::vector<StateCells> kept;
    for (const StateCells& s : c.states) {
        if (moving >= 0 && s.meta.movement != (moving != 0)) continue;
        if (state && (s.meta.name != *state || std::any_of(kept.begin(), kept.end(), [&](const StateCells& k) {
                          return k.meta.movement == s.meta.movement;
                      })))
            continue;
        if (s.meta.dirs <= dir || s.meta.frames <= frame) continue;
        StateCells n;
        n.meta = s.meta;
        for (int64_t f = 0; f < s.meta.frames; f++)
            for (int64_t d = 0; d < s.meta.dirs; d++)
                if ((dir < 0 || d == dir) && (frame < 0 || f == frame)) {
                    n.cells.push_back(s.at(d, f));
                    n.delay.push_back(s.delay[size_t(f * s.meta.dirs + d)]);
                }
        if (dir >= 0) n.meta.dirs = 1;
        if (frame >= 0) n.meta.frames = 1;
        if (state) n.meta.name.clear();
        if (moving >= 0) n.meta.movement = false;
        kept.push_back(std::move(n));
    }
    c.states = std::move(kept);
    return implode(c, ic.version);
}

// A copy of ic with every cell rewritten by fn into an nw x nh cell.
template <class Fn>
std::unique_ptr<Icon> mapCells(const Icon& ic, int nw, int nh, Fn fn) {
    Cells c = explode(ic);
    for (StateCells& s : c.states)
        for (Cell& cell : s.cells) {
            Cell d = blankCell(nw, nh);
            fn(static_cast<const Cell&>(cell), d);
            cell = std::move(d);
        }
    c.w = nw;
    c.h = nh;
    return implode(c, ic.version);
}

// The partner a state blends with: other's state of the same name and movement, else its unnamed one, else its first.
const StateCells* partnerOf(const Cells& other, const IconState& st) {
    const StateCells* unnamed = nullptr;
    for (const StateCells& o : other.states) {
        if (o.meta.movement != st.movement) continue;
        if (o.meta.name == st.name) return &o;
        if (o.meta.name.empty()) unnamed = &o;
    }
    return unnamed ? unnamed : &other.states[0];
}

// ic.Blend(other, mode, x, y): other's bottom-left pixel at (x, y), 1-based from ic's bottom-left; pixels other does not
// cover are kept. A state with fewer dirs or frames than its partner grows to match: new dirs copy its first dir, new
// frames repeat its frames in order. Each cell meets the partner's cell of the same dir (else its first) and frame
// (cycling).
std::unique_ptr<Icon> blendIcons(const Icon& ic, int mode, const Icon& other, int x, int y) {
    if (ic.states.empty() || other.states.empty()) return std::make_unique<Icon>(ic);
    Cells c = explode(ic);
    const Cells oc = explode(other);
    for (StateCells& s : c.states) {
        const StateCells& o = *partnerOf(oc, s.meta);
        const int64_t dirs = std::max(s.meta.dirs, o.meta.dirs), frames = std::max(s.meta.frames, o.meta.frames);
        if (dirs != s.meta.dirs || frames != s.meta.frames) {
            StateCells g;
            g.meta = s.meta;
            g.meta.dirs = dirs;
            g.meta.frames = frames;
            for (int64_t f = 0; f < frames; f++)
                for (int64_t d = 0; d < dirs; d++) {
                    int64_t sf = f % s.meta.frames, sd = d < s.meta.dirs ? d : 0;
                    g.cells.push_back(s.at(sd, sf));
                    g.delay.push_back(s.delay[size_t(sf * s.meta.dirs + sd)]);
                }
            s = std::move(g);
        }
        for (int64_t f = 0; f < frames; f++)
            for (int64_t d = 0; d < dirs; d++) {
                Cell& dst = s.at(d, f);
                const Cell& src = o.at(d < o.meta.dirs ? d : 0, f % o.meta.frames);
                for (int j = 0; j < oc.h; j++)
                    for (int i = 0; i < oc.w; i++) {
                        int tx = i + x - 1, ty = c.h - 1 - ((oc.h - 1 - j) + y - 1);
                        if (tx < 0 || ty < 0 || tx >= c.w || ty >= c.h) continue;
                        dst.at(tx, ty) = blendPixelIcon(mode, dst.at(tx, ty), src.at(i, j));
                    }
            }
    }
    return implode(c, ic.version);
}

// DM's /icon.Insert(ins, state, dir, frame, moving, delay) for icons of one tile. dirIdx/frame < 0 mean "not given".
bool insertIcon(const Icon& ic, const Icon& ins, const std::string* state, int dirIdx, int frame, bool moving,
                double delay, std::unique_ptr<Icon>& out, std::string& err) {
    if (ins.states.empty()) {
        out = std::make_unique<Icon>(ic);
        return true;
    }
    Cells c = explode(ic);
    const Cells ic2 = explode(ins);
    if (c.states.empty()) {
        c.w = ic2.w;
        c.h = ic2.h;
    } else if (c.w != ic2.w || c.h != ic2.h) {
        err = "unsupported DDMI recipe shape: Insert of an icon of another size";
        return false;
    }
    const std::string name = state ? *state : std::string();
    const StateCells& src = ic2.states[0];
    auto pow2Dirs = [](int d) { return d == 0 ? 1 : d <= 3 ? 4 : 8; };
    auto applyDelaySign = [](IconState& m, double& dl) {
        if (dl < 0) {
            dl = -dl;
            m.rewind = true;
        } else if (dl > 0)
            m.rewind = false;
    };
    StateCells* t = nullptr;
    for (StateCells& s : c.states)
        if (s.meta.name == name && s.meta.movement == moving) {
            t = &s;
            break;
        }
    if (!t) {
        if (dirIdx < 0 && frame < 0) {
            StateCells n = src;
            double dl = delay;
            applyDelaySign(n.meta, dl);
            n.meta.movement = moving;
            n.meta.name = name;
            if (dl != 0)
                for (double& v : n.delay) v = dl;
            c.states.push_back(std::move(n));
            out = implode(c, ic.version.empty() ? ins.version : ic.version);
            return true;
        }
        StateCells n;
        n.meta = src.meta;
        n.meta.name = name;
        n.meta.movement = moving;
        n.meta.dirs = pow2Dirs(dirIdx < 0 ? 0 : dirIdx);
        n.meta.frames = frame >= 0 && frame <= 15 ? frame + 1 : 1;
        n.cells.assign(size_t(n.meta.dirs * n.meta.frames), blankCell(c.w, c.h));
        n.delay.assign(n.cells.size(), 1.0);
        c.states.push_back(std::move(n));
        t = &c.states.back();
    }
    // The delay written: the one given, else that of the frame before the inserted one (or after it, for frame 1).
    double dl = delay;
    if (dl == 0 && frame >= 0) {
        int64_t fr = t->meta.frames;
        if (frame > 0)
            dl = t->delay[size_t((std::min<int64_t>(frame, fr) - 1) * t->meta.dirs)];
        else
            dl = frame + 1 < fr ? t->delay[size_t((frame + 1) * t->meta.dirs)] : 1.0;
    }
    applyDelaySign(t->meta, dl);
    if (frame >= t->meta.frames + 16) {
        err = "unsupported DDMI recipe shape: Insert frame far past the state's end";
        return false;
    }
    int64_t dirs =
        dirIdx < 0 ? std::max(t->meta.dirs, src.meta.dirs) : (t->meta.dirs <= dirIdx ? pow2Dirs(dirIdx) : t->meta.dirs);
    int64_t frames =
        frame < 0 ? std::max(t->meta.frames, src.meta.frames) : std::max<int64_t>(t->meta.frames, frame + 1);
    if (dirs > t->meta.dirs || frames > t->meta.frames) {
        StateCells g;
        g.meta = t->meta;
        g.meta.dirs = dirs;
        g.meta.frames = frames;
        for (int64_t f = 0; f < frames; f++)
            for (int64_t d = 0; d < dirs; d++) {
                bool had = d < t->meta.dirs && f < t->meta.frames;
                g.cells.push_back(had ? t->at(d, f) : blankCell(c.w, c.h));
                g.delay.push_back(had ? t->delay[size_t(f * t->meta.dirs + d)] : 1.0);
            }
        *t = std::move(g);
    }
    auto put = [&](int64_t d, int64_t f, int64_t sd, int64_t sf) {
        t->at(d, f) = src.at(sd, sf);
        t->delay[size_t(f * t->meta.dirs + d)] = dl != 0 ? dl : src.delay[size_t(sf * src.meta.dirs + sd)];
    };
    const int64_t sdirs = src.meta.dirs, sframes = src.meta.frames;
    if (dirIdx < 0 && frame < 0) {
        if (sdirs == 1 && sframes == 1) {
            for (int64_t f = 0; f < t->meta.frames; f++)
                for (int64_t d = 0; d < t->meta.dirs; d++) put(d, f, 0, 0);
        } else if (sdirs == 1) {
            for (int64_t f = 0; f < sframes; f++)
                for (int64_t d = 0; d < t->meta.dirs; d++) put(d, f, 0, f);
        } else if (sframes == 1) {
            // frame 1 keeps its own cells; later frames take the inserted dirs
            for (int64_t f = 1; f < t->meta.frames; f++)
                for (int64_t d = 0; d < sdirs; d++) put(d, f, d, 0);
        } else {
            for (int64_t f = 0; f < sframes; f++)
                for (int64_t d = 0; d < sdirs; d++) put(d, f, d, f);
        }
    } else if (dirIdx < 0) {
        if (sdirs == 1)
            for (int64_t d = 0; d < t->meta.dirs; d++) put(d, frame, 0, 0);
        else
            for (int64_t d = 0; d < sdirs; d++) put(d, frame, d, 0);
    } else {
        const int64_t sd = dirIdx < sdirs ? dirIdx : 0;
        if (frame >= 0)
            put(dirIdx, frame, sd, frame < sframes ? frame : 0);
        else if (t->meta.frames == 1 && sframes == 1)
            put(dirIdx, 0, sd, 0);
        else {
            err = "unsupported DDMI recipe shape: Insert into one dir of an animated state";
            return false;
        }
    }
    out = implode(c, ic.version.empty() ? ins.version : ic.version);
    return true;
}

// icon.Turn for one cell: a right angle is an exact rotation; any other angle splats each source pixel onto the up to four
// destination pixels around its rotated position, each keeping the source of greatest overlap (float32, as DreamSeeker).
void turnCell(const Cell& s, Cell& d, float deg) {
    const int w = s.w, h = s.h;
    int k = int(deg);
    if (float(k) == deg && k % 90 == 0) {
        k %= 360;
        if (k == 0) {
            d.px = s.px;
            return;
        }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (k == 90 || k == -270)
                    d.at(h - 1 - y, x) = s.at(x, y);
                else if (k == -90 || k == 270)
                    d.at(y, w - 1 - x) = s.at(x, y);
                else
                    d.at(w - 1 - x, h - 1 - y) = s.at(x, y);
            }
        return;
    }
    const float rad = deg * 0.01745328f;
    const float sn = float(std::sin(double(rad))), cs = float(std::cos(double(rad)));
    const float cx = float(w - 1) * 0.5f, cy = float(h - 1) * 0.5f;
    std::vector<float> best(size_t(w) * size_t(h), 0.0f);
    for (int y = 0; y < h; y++) {
        const float ry = float(y) - cy;
        const float a = ry * cs, b = ry * sn;
        for (int x = 0; x < w; x++) {
            const float rx = float(x) - cx;
            const float fy = rx * sn + cy + a;
            const float fx = (rx * cs + cx) - b;
            const int xs[2] = {int(std::floor(double(fx) + 0.1)), int(std::ceil(double(fx) - 0.1))};
            const int ys[2] = {int(std::floor(double(fy) + 0.1)), int(std::ceil(double(fy) - 0.1))};
            for (int i = 0; i < 2; i++) {
                const float wx = 1.0f - (float(xs[i]) - fx) * float(2 * i - 1);
                for (int j = 0; j < 2; j++) {
                    const float wgt = (1.0f - (float(ys[j]) - fy) * float(2 * j - 1)) * wx;
                    const int tx = xs[i], ty = ys[j];
                    if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
                    float& bw = best[size_t(ty) * size_t(w) + size_t(tx)];
                    if (bw <= wgt) {
                        bw = wgt;
                        d.at(tx, ty) = s.at(x, y);
                    }
                }
            }
        }
    }
}

// icon.Scale for one cell: each destination pixel averages the source area it covers, on the grid of the two sizes'
// least common multiple. Transparent pixels add no colour; alpha averages over the whole area. Integer arithmetic.
void scaleCell(const Cell& s, Cell& d) {
    const int sw = s.w, sh = s.h, dw = d.w, dh = d.h;
    auto gcd = [](int a, int b) {
        while (b) {
            int t = a % b;
            a = b;
            b = t;
        }
        return a;
    };
    const int64_t lx = int64_t(dw / gcd(sw, dw)) * sw, ly = int64_t(dh / gcd(sh, dh)) * sh;
    const int64_t kx = lx / sw, ky = ly / sh, px = lx / dw, py = ly / dh;
    int dy = 0;
    for (int64_t y0 = 0; y0 < ly; y0 += py, dy++) {
        const int64_t y1 = y0 + py;
        int dx = 0;
        for (int64_t x0 = 0; x0 < lx; x0 += px, dx++) {
            const int64_t x1 = x0 + px;
            int64_t ws = 0, r = 0, g = 0, b = 0, a = 0;
            int64_t row = y0 / ky;
            for (int64_t yy = row * ky; yy < y1; row++) {
                int64_t hh = ky;
                if (yy < y0) {
                    hh = ky - y0 + yy;
                    yy = y0;
                }
                if (ky + yy > y1) hh = y1 - yy;
                int64_t col = x0 / kx;
                for (int64_t xx = col * kx; xx < x1; col++) {
                    int64_t ww = kx;
                    if (xx < x0) {
                        ww = kx - x0 + xx;
                        xx = x0;
                    }
                    if (kx + xx > x1) ww = x1 - xx;
                    const Rgba8 p = s.at(int(col), int(row));
                    if (p.a != 0) {
                        const int64_t wgt = hh * ww;
                        ws += wgt;
                        r += p.r * wgt;
                        g += p.g * wgt;
                        b += p.b * wgt;
                        a += p.a * wgt;
                    }
                    xx += ww;
                }
                yy += hh;
            }
            if (ws) d.at(dx, dy) = Rgba8{uint8_t(r / ws), uint8_t(g / ws), uint8_t(b / ws), uint8_t(a / (px * py))};
        }
    }
}

int16_t i16At(const std::vector<uint8_t>& p, size_t k) { return int16_t(uint16_t(p[k] | p[k + 1] << 8)); }
int32_t i32At(const std::vector<uint8_t>& p, size_t k) {
    return int32_t(uint32_t(p[k]) | uint32_t(p[k + 1]) << 8 | uint32_t(p[k + 2]) << 16 | uint32_t(p[k + 3]) << 24);
}
float f32At(const std::vector<uint8_t>& p, size_t k) {
    uint32_t b = uint32_t(i32At(p, k));
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// DM's icon.MapColors() in float32 arithmetic: 20 coefficients (rows r g b a, then the constant), or 12 (rows r g b,
// then the constant) leaving alpha as it is.
void mapColorsCell(const std::vector<float>& c, Cell& cell) {
    const bool full = c.size() == 20;
    const int stride = full ? 4 : 3;
    auto ch = [](float v) -> uint8_t {
        if (v <= 0) return 0;
        if (v >= 1) return 255;
        return uint8_t(v * 255 + 0.5f);
    };
    for (Rgba8& p : cell.px) {
        float r = float(p.r) / 255, g = float(p.g) / 255, b = float(p.b) / 255, a = float(p.a) / 255;
        auto out = [&](int i) {
            float v = r * c[size_t(i)] + g * c[size_t(stride + i)] + b * c[size_t(2 * stride + i)];
            if (full)
                v += a * c[size_t(12 + i)] + c[size_t(16 + i)];
            else
                v += c[size_t(9 + i)];
            return ch(v);
        };
        p = Rgba8{out(0), out(1), out(2), full ? out(3) : p.a};
    }
}

// The value stack of a running recipe.
enum class Kind { Icon, Color, Number, String };
struct Slot {
    Kind kind = Kind::Icon;
    const Icon* icon = nullptr;
    std::unique_ptr<Icon> owned;
    // owned is a DrawBox result in implode()'s layout, so a further DrawBox can paint it in place.
    bool boxed = false;
    Rgba8 col{0, 0, 0, 0};
    std::string str;
};

}  // namespace

bool isDdmi(const uint8_t* d, size_t n) { return n >= 8 && std::memcmp(d, "DDMI", 4) == 0; }

bool parseDdmi(const uint8_t* d, size_t n, DdmiRecipe& rec, std::string* err) {
    rec = {};
    if (n > 65536) {
        if (err) *err = "DDMI byte limit";
        return false;
    }
    if (!isDdmi(d, n)) {
        if (err) *err = "not a DDMI recipe (bad magic)";
        return false;
    }
    uint32_t bodyLen = uint32_t(d[4]) | uint32_t(d[5]) << 8 | uint32_t(d[6]) << 16 | uint32_t(d[7]) << 24;
    if (uint64_t(8) + uint64_t(bodyLen) != uint64_t(n)) {
        if (err) *err = "DDMI declared body length does not match the payload size";
        return false;
    }
    size_t i = 8;
    // A tag 0 ends the program.
    std::vector<Kind> stack;
    while (i < n && d[i] != 0) {
        rec.failureOffset = int(i);
        if (rec.ops.size() >= 1024) {
            if (err) *err = "DDMI operation limit";
            return false;
        }
        uint8_t tag = d[i];
        size_t len;
        if (!payloadLen(tag, d, n, i, len, err)) return false;
        DdmiOp op;
        op.offset = int(i);
        op.tag = tag;
        op.payload.assign(d + i + 1, d + i + 1 + len);
        const uint8_t* payload = d + i + 1;
        auto bad = [&] {
            if (err) *err = "unsupported DDMI operands or parameters";
            return false;
        };
        if (tag == kOpDup) return bad();
        if ((tag == kOpBlend || tag == kOpBlendAt)) {
            int mode = 0;
            if (!wireBlend(payload[0], mode)) return bad();
        }
        if (tag == kOpDir || tag == kOpInsert || tag == kOpInsertF) {
            if ((payload[0] > 7 && payload[0] != 255) || (payload[1] > 127 && payload[1] != 255) ||
                (payload[2] > 1 && payload[2] != 255))
                return bad();
        }
        if ((tag == kOpFlip || tag == kOpShift) && payload[0] > 7) return bad();
        if (tag == kOpSwap && payload[0] > 1) return bad();
        if (tag == kOpShift && payload[3] > 1) return bad();
        if (tag == kOpIconRef) {
            if (payload[4] != 8 && !(payload[4] == 1 && i32At(op.payload, 0) == 0xffff)) return bad();
            stack.push_back(Kind::Icon);
        } else if (tag == kOpColor)
            stack.push_back(Kind::Color);
        else if (tag == kOpNumber)
            stack.push_back(Kind::Number);
        else if (tag == kOpState)
            stack.push_back(Kind::String);
        else if (tag != kOpNop) {
            const size_t pops =
                tag == kOpBlend || tag == kOpBlendAt || tag == kOpDrawBox || (tag == kOpDir && stack.size() > 1) ? 1
                : tag == kOpSwap || tag == kOpInsert || tag == kOpInsertF                                        ? 2
                                                                                                                 : 0;
            if (stack.size() <= pops || stack[stack.size() - pops - 1] != Kind::Icon) return bad();
            if ((tag == kOpBlend || tag == kOpBlendAt) && stack.back() != Kind::Icon && stack.back() != Kind::Color)
                return bad();
            if (tag == kOpDrawBox && stack.back() != Kind::Color) return bad();
            if (tag == kOpSwap && (stack.back() != Kind::Color || stack[stack.size() - 2] != Kind::Color)) return bad();
            if ((tag == kOpInsert || tag == kOpInsertF) &&
                (stack[stack.size() - 2] != Kind::Icon || stack.back() == Kind::Color))
                return bad();
            stack.resize(stack.size() - pops);
        }
        if (stack.size() > 32) return bad();
        if (tag == kOpMapColors && payload[0] != 12 && payload[0] != 20) return bad();
        size_t floatStart = 0, floatCount = 0;
        if (tag == kOpMapColors) {
            floatStart = 1;
            floatCount = payload[0];
        } else if (tag == kOpIntensity)
            floatCount = 3;
        else if (tag == kOpTurn || tag == kOpTurnF)
            floatCount = 1;
        else if (tag == kOpInsertF) {
            floatStart = 3;
            floatCount = 1;
        }
        for (size_t f = 0; f < floatCount; ++f) {
            const float value = f32At(op.payload, floatStart + f * 4);
            if (!std::isfinite(value) || std::abs(value) > 1000000) return bad();
        }
        const uint8_t prevTag = rec.ops.empty() ? 0 : rec.ops.back().tag;
        if (tag == kOpIconRef) {
            DdmiRef r;
            r.ordinal = uint32_t(payload[0]) | uint32_t(payload[1]) << 8 | uint32_t(payload[2]) << 16 |
                        uint32_t(payload[3]) << 24;
            r.kind = payload[4];
            op.ref = int(rec.refs.size());
            rec.refs.push_back(std::move(r));
        } else if (tag == kOpState && prevTag == kOpIconRef && i + 1 + len < n && d[i + 1 + len] == kOpDir) {
            // icon(file, state, dir): the state string is popped by the 0x40 that follows
            op.ref = int(rec.refs.size()) - 1;
            rec.refs.back().name.assign(reinterpret_cast<const char*>(payload), len - 1);
            rec.refs.back().hasName = true;
        } else if (tag == kOpDir && (prevTag == kOpIconRef || (prevTag == kOpState && rec.ops.back().ref >= 0))) {
            op.ref = int(rec.refs.size()) - 1;
            rec.refs.back().hasDir = true;
            std::memcpy(rec.refs.back().trailer, payload, 3);
        }
        rec.ops.push_back(std::move(op));
        i += 1 + len;
    }
    if (stack.size() != 1 || stack.front() != Kind::Icon) {
        if (err) *err = "DDMI must end with one icon";
        return false;
    }
    rec.failureOffset = -1;
    return true;
}

bool ddmiRefIsEmptyIcon(const DdmiRef& r) { return r.ordinal == 0xffff && r.kind == 0x01; }

struct DdmiEvaluation::Impl {
    DdmiRecipe rec;
    std::vector<Icon> inputs;
    std::vector<Slot> st;
    size_t next = 0;
    uint64_t work = 0, peak = 0, inputBytes = 256 * 1024;
    DdmiResult res;
    bool done = false;
    bool step(Clock::time_point deadline, uint64_t availableBytes);
};
namespace {
uint64_t iconBytes(const Icon& icon) {
    uint64_t bytes = sizeof(Icon) + icon.sheet.px.capacity() + icon.states.capacity() * sizeof(IconState);
    for (const auto& state : icon.states)
        bytes += state.name.capacity() + state.delays.capacity() * sizeof(double) +
                 state.hotspot.capacity() * sizeof(int64_t);
    return bytes;
}
}  // namespace
DdmiEvaluation::DdmiEvaluation(const DdmiRecipe& recipe, std::vector<Icon> inputs) : impl_(std::make_unique<Impl>()) {
    impl_->rec = recipe;
    impl_->inputs = std::move(inputs);
    for (const auto& input : impl_->inputs) impl_->inputBytes += iconBytes(input);
    impl_->peak = impl_->inputBytes * 2;  // includes final conversion, even for a bare reference
}
DdmiEvaluation::~DdmiEvaluation() = default;
bool DdmiEvaluation::step(Clock::time_point deadline, uint64_t availableBytes) {
    return impl_->step(deadline, availableBytes);
}
DdmiResult DdmiEvaluation::takeResult() { return std::move(impl_->res); }
uint64_t DdmiEvaluation::workPixels() const { return impl_->work; }
uint64_t DdmiEvaluation::peakBytes() const { return impl_->peak; }
bool DdmiEvaluation::Impl::step(Clock::time_point deadline, uint64_t availableBytes) {
    if (done) return true;
    auto refuse = [&](const std::string& why) {
        res.err = why;
        done = true;
        return true;
    };
    if (inputs.size() != rec.refs.size()) return refuse("EvalDDMI got the wrong number of operands");
    auto refuseShape = [&](const std::string& why) { return refuse(why); };
    auto opName = [](const DdmiOp& op) {
        char t[8];
        std::snprintf(t, sizeof t, "0x%02x", op.tag);
        return std::string(t);
    };
    auto put = [&](Slot& s, std::unique_ptr<Icon> ic) {
        s.owned = std::move(ic);
        s.icon = s.owned.get();
        s.boxed = false;
    };
    bool advanced = false;
    while (next < rec.ops.size()) {
        if (advanced && Clock::now() >= deadline) return false;
        advanced = true;
        const DdmiOp& op = rec.ops[next++];
        res.failureOffset = op.offset;
        res.failureOperation = op.tag;
        const std::vector<uint8_t>& p = op.payload;
        if (op.tag == kOpNop) continue;
        Slot push;
        switch (op.tag) {
            case kOpIconRef:
                push.kind = Kind::Icon;
                push.icon = &inputs[size_t(op.ref)];
                st.push_back(std::move(push));
                continue;
            case kOpColor:
                push.kind = Kind::Color;
                push.col = Rgba8{p[0], p[1], p[2], p[3]};
                st.push_back(std::move(push));
                continue;
            case kOpNumber:
                push.kind = Kind::Number;
                st.push_back(std::move(push));
                continue;
            case kOpState:
                push.kind = Kind::String;
                push.str.assign(reinterpret_cast<const char*>(p.data()), p.size() - 1);
                st.push_back(std::move(push));
                continue;
            default: break;
        }
        // An op pops its arguments; the icon below them is the one it changes.
        const int pops =
            op.tag == kOpBlend || op.tag == kOpBlendAt || op.tag == kOpDrawBox || (op.tag == kOpDir && st.size() > 1)
                ? 1
            : op.tag == kOpSwap || op.tag == kOpInsert || op.tag == kOpInsertF ? 2
                                                                               : 0;
        if (int(st.size()) < pops + 1)
            return refuseShape("unsupported DDMI recipe shape: op " + opName(op) + " without its operands");
        uint64_t liveBytes = inputBytes;
        for (const auto& slot : st)
            if (slot.owned) liveBytes += iconBytes(*slot.owned);
        Slot a, b;
        if (pops == 2) {
            b = std::move(st.back());
            st.pop_back();
        }
        if (pops >= 1) {
            a = std::move(st.back());
            st.pop_back();
        }
        Slot* t = &st.back();
        if (t->kind != Kind::Icon)
            return refuseShape("unsupported DDMI recipe shape: op " + opName(op) + " on a non-icon");
        const Icon& ic = *t->icon;
        // Preflight the whole operation, including source explosion, copies,
        // expanded state cells, packing and pixel loops. This also bounds live
        // scratch storage and stack-owned results without mutating dependencies.
        auto count = [](const Icon& icon) {
            uint64_t cells = 0;
            for (const auto& state : icon.states) cells += uint64_t(state.dirs * state.frames);
            return cells;
        };
        uint64_t cells = count(ic), otherPixels = 0, otherStorage = 0;
        int outputWidth = ic.width, outputHeight = ic.height;
        if (op.tag == kOpScale) {
            outputWidth = i16At(p, 0);
            outputHeight = i16At(p, 2);
        }
        if (op.tag == kOpCrop) {
            outputWidth = i16At(p, 4) - i16At(p, 0);
            outputHeight = i16At(p, 6) - i16At(p, 2);
        }
        if (pops && a.kind == Kind::Icon && a.icon) {
            const auto& other = *a.icon;
            otherPixels = uint64_t(other.sheet.w) * other.sheet.h;
            otherStorage = iconBytes(other);
            if (op.tag == kOpInsert || op.tag == kOpInsertF) {
                if (ic.states.empty()) {
                    outputWidth = other.width;
                    outputHeight = other.height;
                }
                uint64_t maxFrames = 1, maxDirs = 1;
                for (const auto& s : other.states) {
                    maxFrames = std::max(maxFrames, uint64_t(s.frames));
                    maxDirs = std::max(maxDirs, uint64_t(s.dirs));
                }
                maxFrames = std::max(maxFrames, uint64_t(std::max(1, int(int8_t(p[1])) + 1)));
                if (int8_t(p[0]) >= 0) maxDirs = p[0] == 0 ? 1 : p[0] <= 3 ? 4 : 8;
                // Existing states can expand in both axes; conservatively bound all.
                for (const auto& s : ic.states) {
                    maxFrames = std::max(maxFrames, uint64_t(s.frames));
                    maxDirs = std::max(maxDirs, uint64_t(s.dirs));
                }
                cells = (ic.states.size() + 1) * maxFrames * maxDirs;
            } else if (op.tag == kOpBlend || op.tag == kOpBlendAt) {
                uint64_t maxFrames = 1, maxDirs = 1;
                for (const auto& s : other.states) {
                    maxFrames = std::max(maxFrames, uint64_t(s.frames));
                    maxDirs = std::max(maxDirs, uint64_t(s.dirs));
                }
                cells = 0;
                for (const auto& s : ic.states)
                    cells += std::max(uint64_t(s.frames), maxFrames) * std::max(uint64_t(s.dirs), maxDirs);
                // Blend iterates the partner cell for each expanded destination cell.
                otherPixels += cells * uint64_t(other.width) * other.height;
            }
        }
        if (outputWidth < 0 || outputHeight < 0 || outputWidth > 8192 || outputHeight > 8192 || cells > 65536) {
            res.limited = true;
            return refuse("DDMI dimension/cell limit");
        }
        const uint64_t sourcePixels = uint64_t(ic.sheet.w) * ic.sheet.h;
        const uint64_t outputPixels = cells * uint64_t(outputWidth) * outputHeight;
        const uint64_t cost = 4 * (sourcePixels + otherPixels + outputPixels);
        // Distinguish repeated pixel visits from concurrently live allocations.
        // Four copies cover explode/expanded cells/packing (including padding),
        // alongside all input and stack-owned sheets. The fixed input reserve
        // covers the bounded program/stack. Dimension/cell bounds above
        // keep these products and sheet strides within uint64_t / int ranges.
        // Include metadata and expanded cell bookkeeping, not only pixel buffers.
        const uint64_t liveBound = liveBytes + 4 * (iconBytes(ic) + otherStorage + outputPixels * 4 + cells * 256);
        peak = std::max(peak, liveBound);
        if (liveBound > 64 * 1024 * 1024) {
            res.limited = true;
            return refuse("DDMI live pixel storage limit");
        }
        if (liveBound > availableBytes) {
            // Earlier completed outputs still occupy this preparation's memory
            // allowance. Restore only the popped arguments; no pixel work ran.
            // The next preparation can resume this exact operation, not its prefix.
            if (pops >= 1) st.push_back(std::move(a));
            if (pops == 2) st.push_back(std::move(b));
            --next;
            return false;
        }
        work += cost;
        int mode = 0;
        switch (op.tag) {
            case kOpDir: {
                const std::string* state = a.kind == Kind::String && pops ? &a.str : nullptr;
                if (auto o = extractIcon(ic, state, int8_t(p[0]), int8_t(p[1]), int8_t(p[2]))) put(*t, std::move(o));
                break;
            }
            case kOpBlend:
            case kOpBlendAt: {
                if (!wireBlend(p[0], mode)) return refuseShape("unsupported DDMI blend mode");
                if (a.kind == Kind::Icon) {
                    int x = op.tag == kOpBlendAt ? i16At(p, 1) : 1, y = op.tag == kOpBlendAt ? i16At(p, 3) : 1;
                    put(*t, blendIcons(ic, mode, *a.icon, x, y));
                } else if (a.kind == Kind::Color) {
                    put(*t, blendedColor(ic, mode, a.col));
                } else {
                    return refuseShape("unsupported DDMI recipe shape: Blend with a number or text operand");
                }
                break;
            }
            case kOpDrawBox: {
                if (a.kind != Kind::Color)
                    return refuseShape("unsupported DDMI recipe shape: DrawBox without a colour");
                // The colour replaces the pixel, alpha included. Corners are inclusive, y counts up from the bottom row,
                // reversed corners are normalised and out-of-range ones clipped.
                int x1 = i16At(p, 0), y1 = i16At(p, 2), x2 = i16At(p, 4), y2 = i16At(p, 6);
                int lx = std::max(std::min(x1, x2), 1), hx = std::min(std::max(x1, x2), ic.width);
                int ly = std::max(std::min(y1, y2), 1), hy = std::min(std::max(y1, y2), ic.height);
                const Rgba8 c = a.col;
                const int h = ic.height;
                // A world that draws a picture a box at a time (vg's holomaps: ~25k boxes on a 480x480 icon) would pay a
                // whole-icon copy per box. Once an icon is a DrawBox result it is painted in place: every cell takes the
                // same box, so the cost is the box's area.
                if (t->boxed && t->owned) {
                    Icon& o = *t->owned;
                    size_t cells = 0;
                    for (const IconState& is : o.states) cells += size_t(is.frames * is.dirs);
                    for (size_t k = 0; k < cells; k++) {
                        const int x0 = int(k % size_t(o.sheetCols)) * o.width, y0 = int(k / size_t(o.sheetCols)) * o.height;
                        for (int y = ly; y <= hy; y++)
                            for (int x = lx; x <= hx; x++) setPixel(o.sheet, x0 + x - 1, y0 + h - y, c);
                    }
                    break;
                }
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    d.px = s.px;
                    for (int y = ly; y <= hy; y++)
                        for (int x = lx; x <= hx; x++) d.at(x - 1, h - y) = c;
                }));
                t->boxed = true;
                break;
            }
            case kOpSwap: {
                if (a.kind != Kind::Color || b.kind != Kind::Color)
                    return refuseShape("unsupported DDMI recipe shape: SwapColor without two colours");
                // An old colour with alpha (or null) matches exactly; one without matches any visible pixel of its rgb
                // and keeps that pixel's alpha.
                const Rgba8 from = a.col, to = b.col;
                const bool exact = p[0] != 0;
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    d.px = s.px;
                    for (Rgba8& px : d.px) {
                        if (exact) {
                            if (px.a != from.a || (from.a != 0 && (px.r != from.r || px.g != from.g || px.b != from.b)))
                                continue;
                            px = to;
                        } else {
                            if (px.a == 0 || px.r != from.r || px.g != from.g || px.b != from.b) continue;
                            px = Rgba8{to.r, to.g, to.b, to.a == 0 ? uint8_t(0) : px.a};
                        }
                    }
                }));
                break;
            }
            case kOpInsert:
            case kOpInsertF: {
                if (b.kind != Kind::Icon && b.kind != Kind::String && b.kind != Kind::Number)
                    return refuseShape("unsupported DDMI recipe shape: Insert state");
                if (a.kind != Kind::Icon) return refuseShape("unsupported DDMI recipe shape: Insert of a non-icon");
                const int dirIdx = int(int8_t(p[0])), frame = int(int8_t(p[1]));
                const double delay = op.tag == kOpInsert ? double(float(i32At(p, 3))) : double(f32At(p, 3));
                if (dirIdx > 7)
                    return refuseShape("unsupported DDMI recipe shape: Insert dir index " + std::to_string(dirIdx));
                std::unique_ptr<Icon> o;
                std::string e;
                if (!insertIcon(ic, *a.icon, b.kind == Kind::String ? &b.str : nullptr, dirIdx, frame, int8_t(p[2]) > 0,
                                delay, o, e))
                    return refuseShape(e);
                put(*t, std::move(o));
                break;
            }
            case kOpMapColors: {
                std::vector<float> c(std::min<size_t>(p[0], 20));
                for (size_t k = 0; k < c.size(); k++) c[k] = f32At(p, 1 + 4 * k);
                if (c.size() != 12 && c.size() != 20)
                    return refuseShape("unsupported DDMI recipe shape: MapColors with " + std::to_string(c.size()) +
                                       " coefficients");
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    d.px = s.px;
                    mapColorsCell(c, d);
                }));
                break;
            }
            case kOpIntensity: {
                float r = f32At(p, 0), g = f32At(p, 4), b2 = f32At(p, 8);
                if (g < 0) g = r;
                if (b2 < 0) b2 = r;
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    d.px = s.px;
                    for (Rgba8& px : d.px)
                        px = Rgba8{clamp8(double(px.r) * r), clamp8(double(px.g) * g), clamp8(double(px.b) * b2), px.a};
                }));
                break;
            }
            case kOpFlip: {
                // S/N mirror top to bottom, E/W left to right; SE/NW and SW/NE mirror across a diagonal, square icons only.
                const int k = p[0];
                if (k > 7 || (k >= 4 && ic.width != ic.height)) return refuseShape("unsupported DDMI Flip");
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    for (int y = 0; y < s.h; y++)
                        for (int x = 0; x < s.w; x++) {
                            if (k <= 1)
                                d.at(x, y) = s.at(x, s.h - 1 - y);
                            else if (k <= 3)
                                d.at(x, y) = s.at(s.w - 1 - x, y);
                            else if (k == 4 || k == 7)
                                d.at(x, y) = s.at(s.w - 1 - y, s.h - 1 - x);
                            else
                                d.at(x, y) = s.at(y, x);
                        }
                }));
                break;
            }
            case kOpTurn:
            case kOpTurnF: {
                // Only a square icon turns.
                if (ic.width != ic.height) break;
                const float deg = f32At(p, 0);
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) { turnCell(s, d, deg); }));
                break;
            }
            case kOpShift: {
                static const int kDx[8] = {0, 0, 1, -1, 1, -1, 1, -1},
                                 kDy[8] = {1, -1, 0, 0, 1, 1, -1, -1};  // top-down rows
                if (p[0] > 7) return refuseShape("unsupported DDMI Shift");
                const int off = i16At(p, 1);
                const int dx = kDx[p[0]] * off, dy = kDy[p[0]] * off;
                const bool wrap = p[3] != 0;
                put(*t, mapCells(ic, ic.width, ic.height, [&](const Cell& s, Cell& d) {
                    for (int y = 0; y < s.h; y++)
                        for (int x = 0; x < s.w; x++) {
                            int nx = x + dx, ny = y + dy;
                            if (wrap) {
                                nx = ((nx % s.w) + s.w) % s.w;
                                ny = ((ny % s.h) + s.h) % s.h;
                            } else if (nx < 0 || ny < 0 || nx >= s.w || ny >= s.h)
                                continue;
                            d.at(nx, ny) = s.at(x, y);
                        }
                }));
                break;
            }
            case kOpScale: {
                const int nw = i16At(p, 0), nh = i16At(p, 2);
                if (nw == ic.width && nh == ic.height) break;
                if (nw <= 0 || nh <= 0)
                    return refuseShape("unsupported DDMI recipe shape: Scale to " + std::to_string(nw) + "x" +
                                       std::to_string(nh));
                put(*t, mapCells(ic, nw, nh, [&](const Cell& s, Cell& d) { scaleCell(s, d); }));
                break;
            }
            case kOpCrop: {
                // A half-open, top-down, 0-based rectangle; what lies outside the icon is transparent.
                const int x0 = i16At(p, 0), y0 = i16At(p, 2), x1 = i16At(p, 4), y1 = i16At(p, 6);
                const int nw = x1 - x0, nh = y1 - y0;
                if (nw <= 0 || nh <= 0) return refuseShape("unsupported DDMI recipe shape: an empty Crop");
                put(*t, mapCells(ic, nw, nh, [&](const Cell& s, Cell& d) {
                    for (int y = 0; y < nh; y++)
                        for (int x = 0; x < nw; x++) {
                            int sx = x + x0, sy = y + y0;
                            if (sx >= 0 && sy >= 0 && sx < s.w && sy < s.h) d.at(x, y) = s.at(sx, sy);
                        }
                }));
                break;
            }
            default: return refuseShape("unsupported DDMI op " + opName(op));
        }
    }
    // The icon drawn is the bottom of the stack: the first reference, with every op applied.
    if (st.size() != 1 || st[0].kind != Kind::Icon)
        return refuseShape("unsupported DDMI recipe shape: the program does not end with one icon on the stack");
    if (st[0].owned)
        res.owned = std::move(st[0].owned);
    else {
        for (auto& input : inputs)
            if (&input == st[0].icon) {
                res.owned = std::make_unique<Icon>(std::move(input));
                break;
            }
    }
    res.failureOffset = res.failureOperation = -1;
    done = true;
    return true;
}

}  // namespace fugue::assets
