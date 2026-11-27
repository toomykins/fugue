#include "proto/enc_c2s_0002_input.h"
#include <cctype>
#include <cstring>

namespace fugue::proto::enc {

AtomRef nullRef() { return AtomRef{RefNull, 0x0000ffff, 0, 0}; }
AtomRef objRef(uint32_t r) { return AtomRef{RefObj, r, 0, 0}; }
AtomRef mobRef(uint32_t r) { return AtomRef{RefMob, r, 0, 0}; }
AtomRef turfRef(int wx, int wy) {
    if (wx < 1) wx = 1;
    if (wy < 1) wy = 1;
    return AtomRef{RefTurf, 0, uint16_t(wx - 1), uint16_t(wy - 1)};
}

Bytes encodeAtomRef(const AtomRef& a) {
    Bytes b;
    b.push_back(a.kind);
    if (a.kind == RefTurf) {
        putLe16(b, a.x);
        putLe16(b, a.y);
    } else
        putLe32(b, a.ref);
    return b;
}

static int clampPx(int v, int tile) {
    if (tile <= 0) tile = 32;
    return v < 1 ? 1 : (v > tile ? tile : v);
}

// Order: icon-x, icon-y, vis-x, vis-y, the button (a hover has only "right=1" or "middle=1", and only while a verb menu
// closes with that button held: stock 516.1689, tests/data/hover-world), the modifiers, screen-loc.
static std::string mouseParams(const ClickCmd& c, bool withButton) {
    std::string btn = c.button.empty() ? "left" : c.button;
    // icon-x/icon-y are not clamped: they are in the named atom's icon frame, whatever the cursor's distance from it
    // (stock MouseParams::ParamString prints %hd). Over an overlay at -8,-8 the pair can be negative.
    std::string s = "icon-x=" + std::to_string(c.iconX) + ";icon-y=" + std::to_string(c.iconY) + ";";
    if (c.hasVis) s += "vis-x=" + std::to_string(c.visX) + ";vis-y=" + std::to_string(c.visY) + ";";
    if (withButton)
        s += btn + "=1;button=" + btn + ";";
    else if (c.button == "right" || c.button == "middle")
        s += c.button + "=1;";
    if (c.mods.ctrl) s += "ctrl=1;";
    if (c.mods.shift) s += "shift=1;";
    if (c.mods.alt) s += "alt=1;";
    int spx = c.screenPX ? c.screenPX : c.iconX, spy = c.screenPY ? c.screenPY : c.iconY;
    s += "screen-loc=" + (c.screenLocMap.empty() ? std::string() : mapIdParam(c.screenLocMap) + ":") +
         std::to_string(c.screenX) + ":" + std::to_string(clampPx(spx, c.screenTileSize)) + "," +
         std::to_string(c.screenY) + ":" + std::to_string(clampPx(spy, c.screenTileSize));
    return s;
}

std::string mapIdParam(const std::string& id) {
    static const char* hexd = "0123456789abcdef";
    std::string o;
    for (unsigned char ch : id) {
        if (std::isalnum(ch) || ch == '_' || ch == '.' || ch == '-')
            o += char(ch);
        else {
            o += '%';
            o += hexd[ch >> 4];
            o += hexd[ch & 15];
        }
    }
    return o;
}

std::string clickParams(const ClickCmd& c) { return mouseParams(c, true); }
std::string hoverParams(const ClickCmd& c) { return mouseParams(c, false); }

static void putStr(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

static Bytes mouseBody(uint8_t sub, const AtomRef& over, const AtomRef& loc, const std::string& locStr,
                       const std::string& control, const std::string& params) {
    Bytes b{sub, 0x00, 0xff, 0xff};
    auto o = encodeAtomRef(over);
    b.insert(b.end(), o.begin(), o.end());
    b.push_back(0x03);  // three values follow; loc is tag 0, an atom, or tag 2, a string (cf. encodeInfoClick)
    if (!locStr.empty()) {
        b.push_back(0x02);
        putStr(b, locStr);
    } else {
        b.push_back(0x00);
        auto l = encodeAtomRef(loc);
        b.insert(b.end(), l.begin(), l.end());
    }
    b.push_back(0x02);
    putStr(b, control.empty() ? "mapwindow.map" : control);
    b.push_back(0x02);
    putStr(b, params);
    return b;
}

Bytes mouseWireBody(Bytes body, bool wideId) {
    if (body.size() < 4 || body[0] < kMouseClick || body[0] > kMouseHover || body[1] != 0) return {};
    // The sentinel (0xffff), or sub-7's event ID, occupies the same negotiated
    // verb slot as sub-2. Stock 516.1688 zero-extends it, never sign-extends.
    if (wideId) body.insert(body.begin() + 4, {0, 0});
    return body;
}

Bytes encodeMouseCmd(const ClickCmd& c) { return mouseBody(c.sub, c.over, c.loc, c.locStr, c.control, clickParams(c)); }

Bytes encodeMouseNone(uint8_t sub, const std::string& locStr, const std::string& control, const std::string& params) {
    const Bytes n = encodeAtomRef(nullRef());
    Bytes b{sub, 0x00, 0xff, 0xff};
    b.insert(b.end(), n.begin(), n.end());
    b.push_back(0x03);
    for (const std::string* v : {&locStr, &control}) {
        if (v->empty()) {
            b.push_back(0x00);
            b.insert(b.end(), n.begin(), n.end());
        } else {
            b.push_back(0x02);
            putStr(b, *v);
        }
    }
    b.push_back(0x02);
    putStr(b, params);
    return b;
}

Bytes encodeHover(const AtomRef& over, const AtomRef& loc, const std::string& control, const std::string& params,
                  const std::string& locStr) {
    return mouseBody(kMouseHover, over, loc, locStr, control, params);
}

Bytes encodeHoverLeave() {
    Bytes b{kMouseHover, 0x00, 0xff, 0xff};
    auto n = encodeAtomRef(nullRef());
    b.insert(b.end(), n.begin(), n.end());
    b.push_back(0x03);
    for (int i = 0; i < 2; i++) {
        b.push_back(0x00);
        b.insert(b.end(), n.begin(), n.end());
    }  // loc and control: null atoms
    b.push_back(0x02);
    b.push_back(0x00);  // params ""
    return b;
}

ClickCmd turfClick(int wx, int wy, int sc, int sr, int ix, int iy, Mods m) {
    ClickCmd c;
    c.sub = kMouseClick;
    c.over = c.loc = turfRef(wx, wy);
    c.iconX = ix;
    c.iconY = iy;
    c.screenX = sc;
    c.screenY = sr;
    c.screenPX = ix;
    c.screenPY = iy;
    c.mods = m;
    return c;
}

Bytes encodeInfoClick(uint8_t sub, const AtomRef& over, const std::string& panel, const std::string& control,
                      const std::string& params) {
    Bytes b{sub, 0x00, 0xff, 0xff};
    auto o = encodeAtomRef(over);
    b.insert(b.end(), o.begin(), o.end());
    b.push_back(0x03);
    for (const std::string* s : {&panel, &control, &params}) {
        b.push_back(0x02);
        putStr(b, *s);
    }
    return b;
}

std::string infoClickParams(const std::string& button, const Mods& m, bool icon, int iconX, int iconY) {
    std::string btn = button.empty() ? "left" : button;
    std::string s = icon ? "icon-x=" + std::to_string(iconX) + ";icon-y=" + std::to_string(iconY) + ";" : std::string();
    s += btn + "=1;";
    if (m.ctrl) s += "ctrl=1;";
    if (m.shift) s += "shift=1;";
    if (m.alt) s += "alt=1;";
    return s + "button=" + btn;
}

std::string infoHoverParams(const Mods& m, bool icon, int iconX, int iconY) {
    std::string s = icon ? "icon-x=" + std::to_string(iconX) + ";icon-y=" + std::to_string(iconY) : std::string();
    for (auto [held, name] : {std::pair{m.ctrl, "ctrl=1"}, std::pair{m.shift, "shift=1"}, std::pair{m.alt, "alt=1"}})
        if (held) s += (s.empty() ? "" : ";") + std::string(name);
    return s;
}

MouseHooks allMouseHooks() { return MouseHooks{true, true, true, true, true, true}; }

std::vector<Bytes> pressFrames(ClickCmd c, const MouseHooks& h) {
    std::vector<Bytes> out;
    if (h.down) {
        c.sub = kMouseDown;
        out.push_back(encodeMouseCmd(c));
    }
    return out;
}
std::vector<Bytes> releaseFrames(ClickCmd c, const MouseHooks& h) {
    std::vector<Bytes> out;
    if (h.up) {
        c.sub = kMouseUp;
        out.push_back(encodeMouseCmd(c));
    }
    if (h.click) {
        c.sub = kMouseClick;
        out.push_back(encodeMouseCmd(c));
    }
    return out;
}
std::vector<Bytes> clickSequence(ClickCmd c, const MouseHooks& h) {
    auto a = pressFrames(c, h), b = releaseFrames(c, h);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes encodeDragCmd(const DragCmd& d) {
    Bytes b{kMouseExt, 0x00};
    putLe16(b, d.drop ? kMouseEventDrop : kMouseEventDrag);
    b.insert(b.end(), {0x00, 0xff, 0xff, 0x00, 0x00});
    b.push_back(7);  // value count
    const std::string none;
    for (auto [r, str] :
         {std::pair{&d.src, &none}, {&d.over, &none}, {&d.srcLoc, &d.srcLocStr}, {&d.overLoc, &d.overLocStr}}) {
        if (!str->empty() || (r == &d.overLoc && d.overLocBlank)) {
            b.push_back(0x02);
            putStr(b, *str);
            continue;
        }
        AtomRef a = *r;
        if (a.kind == RefNull && a.ref == 0) a.ref = 0x0000ffff;
        b.push_back(0x00);
        auto e = encodeAtomRef(a);
        b.insert(b.end(), e.begin(), e.end());
    }
    for (const std::string* s : {&d.srcControl, &d.overControl, &d.params}) {
        if (s == &d.overControl && d.overControlNull) {
            b.push_back(0x00);
            auto n = encodeAtomRef(nullRef());
            b.insert(b.end(), n.begin(), n.end());
            continue;
        }
        b.push_back(0x02);
        putStr(b, *s);
    }
    return b;
}

Bytes encodeWheelCmd(const WheelCmd& w) {
    Bytes b{kMouseExt, 0x00};
    putLe16(b, kMouseEventWheel);
    b.insert(b.end(), {0x00, 0xff, 0xff, 0x00, 0x00});
    b.push_back(6);  // value count
    AtomRef o = w.over;
    if (o.kind == RefNull && o.ref == 0) o.ref = 0x0000ffff;
    b.push_back(0x00);
    auto e = encodeAtomRef(o);
    b.insert(b.end(), e.begin(), e.end());
    for (float f : {w.dx, w.dy}) {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        b.push_back(0x01);
        putLe32(b, u);
    }
    for (const std::string* s : {&w.location, &w.control, &w.params}) {
        b.push_back(0x02);
        putStr(b, *s);
    }
    return b;
}

Bytes encodeMove(uint8_t dir) { return Bytes{0x00, 0x00, dir, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00}; }

Bytes encodeVerb(uint16_t id, uint8_t srcKind, uint32_t srcRef, const std::vector<VerbArg>& args, bool wideId) {
    Bytes b{0x02, 0x00};
    if (wideId)
        putLe32(b, id);
    else
        putLe16(b, id);
    b.push_back(srcKind);
    putLe32(b, srcRef);
    b.push_back(uint8_t(args.size()));
    for (auto& a : args) {
        b.push_back(a.tag);
        if (a.tag == 0x02)
            putStr(b, a.str);
        else if (a.tag == 0x01) {
            uint32_t u;
            std::memcpy(&u, &a.num, 4);
            putLe32(b, u);
        } else {
            b.push_back(a.kind);
            putLe32(b, a.ref);
        }
    }
    return b;
}

Bytes encodeMoveVerb(uint16_t id, uint32_t srcRef, int dir, bool down, bool twoArgs, bool wideId) {
    std::vector<VerbArg> a;
    VerbArg d;
    d.tag = 0x01;
    d.num = float(dir);
    a.push_back(d);
    if (twoArgs) {
        VerbArg s;
        s.tag = 0x01;
        s.num = down ? 1.f : 0.f;
        a.push_back(s);
    }
    // A /client/verb carries src-ref 0; `srcRef` is unused.
    (void)srcRef;
    return encodeVerb(id, kVerbSrcClient, 0, a, wideId);
}

Bytes encodeTopic(const std::string& href0) {
    std::string h = href0;
    if (h.rfind("byond://", 0) == 0) h = h.substr(8);
    if (h.empty() || h[0] != '?') h = "?" + h;
    Bytes b(h.begin(), h.end());
    b.push_back(0);
    return b;
}

}  // namespace fugue::proto::enc
