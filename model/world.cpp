// Session::ingest: the world model's frame handlers.
//
// The wire is parsed by the proto/ decoders (proto/messages.h); this file holds the model rules, i.e. what a decoded
// record does to the world.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <type_traits>
#include <unordered_map>

#include "model/anomaly.h"
#include "model/gomath.h"
#include "model/htmltext.h"
#include "model/resync.h"
#include "model/world.h"
#include "net/textmacro.h"
#include "proto/g2_mapview.h"
#include "proto/msg.h"

namespace fugue::model {

namespace {

// ---- small string helpers

bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Window name of a browse() params string.
bool startsPair(std::string_view s) {
    for (char c : s) {
        if (c == '=') return true;
        if (c == ';' || c == '&') return false;
    }
    return false;
}

std::string trimAscii(std::string_view s) {
    size_t a = 0, b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; };
    while (a < b && sp(s[a])) a++;
    while (b > a && sp(s[b - 1])) b--;
    return std::string(s.substr(a, b - a));
}

std::string browserWindowName(const std::string& params) {
    std::string window;
    std::vector<std::string_view> parts;
    size_t start = 0;
    std::string_view s = params;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ';' || c == '&') {
        } else if (c == ',') {
            if (!startsPair(s.substr(i + 1))) continue;
        } else
            continue;
        parts.push_back(s.substr(start, i - start));
        start = i + 1;
    }
    parts.push_back(s.substr(start));
    for (auto part : parts) {
        std::string p = trimAscii(part);
        if (p.empty()) continue;
        size_t eq = p.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimAscii(std::string_view(p).substr(0, eq));
        for (auto& ch : k) {
            if (ch >= 'A' && ch <= 'Z')
                ch = char(ch - 'A' + 'a');
            else if (ch == '_')
                ch = '-';
        }
        if (k == "window") window = trimAscii(std::string_view(p).substr(eq + 1));
    }
    return window;
}

// Splits an output control into its window and page function.
bool splitBrowserOutput(const std::string& control, std::string& window, std::string& fn) {
    window.clear();
    fn.clear();
    size_t colon = control.find(':');
    std::string ctrl = colon == std::string::npos ? control : control.substr(0, colon);
    if (colon == std::string::npos) {
        if (!endsWith(ctrl, ".browser")) return false;
        window = ctrl.substr(0, ctrl.size() - 8);
        return !window.empty();
    }
    fn = control.substr(colon + 1);
    if (fn.empty() || fn == "output") {
        fn.clear();
        return false;
    }  // ":output" is chat (model/browsing.h)
    size_t dot = ctrl.rfind('.');
    window = dot == std::string::npos ? ctrl : ctrl.substr(0, dot);
    if (window.empty()) {
        fn.clear();
        return false;
    }
    return true;
}

// The window a browser control belongs to.
bool browserControlWindow(std::string control, std::string& window) {
    size_t colon = control.find(':');
    if (colon != std::string::npos) control.resize(colon);
    size_t dot = control.rfind('.');
    if (dot == std::string::npos || control.compare(dot + 1, std::string::npos, "browser") != 0) return false;
    window = control.substr(0, dot);
    return true;
}

// Text for a session-end reason. The reason codes are BYOND's; the wording is the model's own.
std::string sessionEndText(uint16_t reason, uint32_t version) {
    static const char* t[12] = {
        "the server closed the connection",
        "the connection died",
        "the server killed the connection",
        "another connection has been made using your login key -- this session has been terminated",
        "the server's DreamDaemon is too far out of date",
        "this client is too far out of date",
        "your BYOND key is corrupt -- to restore it, log in again while connected to the Internet",
        "your BYOND key has expired -- to renew it, log in again while connected to the Internet",
        "the connection timed out",
        "failed to get a security certificate from BYOND Central -- your account password may have changed, or the "
        "host has a problem",
        "access denied -- the host refused this login (a guest ban, a key ban, or a whitelist)",
        "the server killed the connection after a network read error"};
    if (reason >= 12) return "the server is closing the connection (reason " + std::to_string(reason) + ")";
    std::string line = t[reason];
    if ((reason == 4 || reason == 5) && version != 0) line += " -- version " + std::to_string(version) + " is required";
    return line;
}

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

int64_t tickNs(const World& w) {
    return w.tickNanos > 0 ? w.tickNanos : kDefaultTickNanos;
}  // tick duration, or the default when unset

// Glide size and long-glide flag, read off the appearance the atom holds (Pos is processed before the same frame's
// Appearance array, which re-times a glide that frame armed).
float glideSizeOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end() || !it->second.fields.hasGlideSize) return 0;
    float g = bitsToFloat(it->second.fields.glideSizeBits);
    return g > 0 ? g : 0;
}
bool longGlideOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    return it != w.appearances.end() && it->second.fields.longGlide();
}

}  // namespace

// The world's icon size.
int iconSizeOf(const World& w) {
    float s = bitsToFloat(w.stepSizeBits);
    return s > 0 ? int(s) : kDefaultIconSize;
}

static int g_modelMutation = 0;
void setModelMutation(int m) { g_modelMutation = m; }
// An atom whose animate_movement is NO_STEPS jumps from tile to tile; it never glides.
static bool noStepsOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    return g_modelMutation != 32 && it != w.appearances.end() && it->second.fields.noSteps();
}
static bool altRecordModel() { return g_modelMutation == 11; }
static bool g_goAutoGlide = false;
void setGoAutoGlide(bool on) { g_goAutoGlide = on; }

std::vector<AnimSeq> animSequences(const Atom::Chain& c) {
    std::vector<AnimSeq> out;
    if (c.rejected || c.steps.size() > 128 || c.tickNs <= 0 || c.tickNs > 10'000'000'000LL) return out;
    for (size_t i = 0; i < c.steps.size(); i++) {
        const Atom::AnimStep& st = c.steps[i];
        if (i == 0 || st.startNs != 0) {
            AnimSeq q;
            q.first = i;
            q.startNs = st.startNs;
            q.head = (st.flags & 0x40000000u) != 0;
            q.parallel = (st.flags & 0x4u) != 0;
            out.push_back(q);
        }
        AnimSeq& q = out.back();
        q.last = i;
        const double duration = double(bitsToFloat(st.timeBits)) * double(c.tickNs);
        if (!std::isfinite(duration) || duration < 0 || duration > double(INT64_MAX / 4) ||
            q.durNs > INT64_MAX / 4 - int64_t(duration) || st.startNs < 0 || st.startNs > INT64_MAX / 2)
            return {};
        q.durNs += int64_t(duration);
    }
    for (AnimSeq& q : out) {
        int32_t loop = c.steps[q.first].loop;
        if (loop > 0 && q.durNs > (INT64_MAX / 4) / loop) return {};
        q.endNs = loop < 1 ? -1 : q.startNs + int64_t(loop) * q.durNs;
    }
    return out;
}

// An Animate frame into a chain, as the stock handler builds it for every kind: Chain != 0 (or nothing still playing)
// replaces it, so a Count = 0 frame clears it; Chain = 0 appends. The first step starts at Delay units (deciseconds)
// from `now`, a step with a StepDelay (a parallel or a head) opens a sequence at StepDelay units from `now`, and any
// other step
// follows the one before it.
static void scheduleChain(Atom::Chain& ch, const proto::Animate& m, int64_t now, int64_t tick) {
    if (ch.rejected && m.chain == 0) return;
    bool playing = false;
    for (const AnimSeq& q : animSequences(ch)) playing = playing || q.endNs < 0 || now < q.endNs;
    if (m.chain != 0 || !playing) {
        ch = Atom::Chain{};
        ch.tickNs = tick;
    }
    auto reject = [&] {
        ch.steps.clear();
        ch.rejected = true;
    };
    if (m.steps.size() > 128 - std::min<size_t>(128, ch.steps.size()) || tick <= 0 || tick > 10'000'000'000LL ||
        now < 0 || now > INT64_MAX / 2) {
        reject();
        return;
    }
    auto validTime = [&](uint32_t bits, bool delay) {
        const double ticks = bitsToFloat(bits);
        return std::isfinite(ticks) && ticks >= (delay ? -1'000'000 : 0) && ticks <= 1'000'000 &&
               (!delay || static_cast<long double>(now) + ticks * ch.tickNs >= 0);
    };
    if (!validTime(m.delayBits, true)) {
        reject();
        return;
    }
    for (const proto::Animate::Step& step : m.steps)
        if (!validTime(step.timeBits, false) || (step.hasDelay && !validTime(step.delayBits, true))) {
            reject();
            return;
        }
    auto at = [&](uint32_t bits) { return now + int64_t(double(bitsToFloat(bits)) * double(ch.tickNs)); };
    for (const proto::Animate::Step& st : m.steps) {
        Atom::AnimStep s{st.from, st.to, st.timeBits, st.easing};
        s.flags = st.flags;
        s.loop = st.loop == 0 ? 1 : st.loop;
        if (st.hasExt) s.extBits = st.extBits;
        if (ch.steps.empty()) {
            s.startNs = at(m.delayBits);
            s.flags &= ~0x40000000u;
        } else if (st.hasDelay)
            s.startNs = at(st.delayBits);
        ch.steps.push_back(s);
    }
    if (!ch.steps.empty()) {
        ch.startNs = ch.steps[0].startNs;
        if (animSequences(ch).empty()) reject();
    }
}

// The eye scroll offset. X is negated, Y is not: the image y-flip already lives in placement, so flipping it here too
// would move the sprite a whole tile on a northward step.
void eyeScrollOffset(const World& w, int64_t nowNs, int& sx, int& sy) {
    sx = sy = 0;
    const GlideMotion& g = w.eyeGlide;
    if (!w.eyeGliding || nowNs == 0 || g.startNs == 0) return;
    double rem = glideLeft(g, nowNs);
    if (rem <= 0) return;
    int icon = iconSizeOf(w);
    sx = -int(std::lround(g.sx * icon * rem));
    sy = int(std::lround(g.sy * icon * rem));
}

// Glide duration: distance in tiles (Chebyshev with long glide, else Euclidean) over the glide size, times the tick.
int64_t glideDurationNs(int dx, int dy, float glidePx, int iconSize, int64_t tick, bool longGlide) {
    if (iconSize <= 0) iconSize = kDefaultIconSize;
    if (tick <= 0) tick = kDefaultTickNanos;
    if (glidePx <= 0) glidePx = kAutoGlidePx;
    double ax = std::fabs(double(dx)), ay = std::fabs(double(dy));
    double dist = (longGlide && g_modelMutation != 9) ? std::fmax(ax, ay) : gomath::hypot(ax, ay);
    double ticks = dist * double(iconSize) / double(glidePx);
    if (ticks <= 0) return 0;
    return int64_t(ticks * double(tick));
}

// The stock client moves a drawn atom's offset toward its tile by glide_size per tick along the offset's own direction
// (the view scrolls the same way), so a new target redirects what is left. While the offset is at least a tile plus one
// step on either axis the step is scaled by (tiles left on the longer axis + 1/16). Here that is in closed form over u
// = base tiles travelled: the distance D obeys dD/du = -(c*D + 1/16) while c*D > 1, where c*D is the longer axis in
// tiles, then falls linearly. An offset of at most one tile per axis (every fresh one-tile move) has no catch-up phase,
// and its duration is glideDurationNs's.
GlideMotion glideMotion(double sx, double sy, float glidePx, int iconSize, int64_t tick, bool longGlide,
                        int64_t startNs, bool catchUp) {
    if (iconSize <= 0) iconSize = kDefaultIconSize;
    if (tick <= 0) tick = kDefaultTickNanos;
    if (glidePx <= 0) glidePx = kAutoGlidePx;
    GlideMotion g;
    g.sx = sx;
    g.sy = sy;
    g.startNs = startNs;
    g.chebyshev = longGlide && g_modelMutation != 9;
    g.catchUp = catchUp;
    double ax = std::fabs(sx), ay = std::fabs(sy);
    double dist = g.chebyshev ? std::fmax(ax, ay) : gomath::hypot(ax, ay);
    if (dist <= 0) return g;
    g.rate = double(glidePx) / double(iconSize) / double(tick);
    double m = std::fmax(ax, ay), u = dist;
    if (catchUp && m > 1) {
        double c = m / dist;
        u = std::log((m + 1.0 / 16) / (1 + 1.0 / 16)) / c + 1 / c;
    }
    double ticks = u * double(iconSize) / double(glidePx);
    g.durNs = int64_t(ticks * double(tick));
    return g;
}

double glideLeft(const GlideMotion& g, int64_t nowNs) {
    if (g.durNs <= 0) return 0;
    int64_t el = nowNs - g.startNs;
    if (el <= 0) return 1;
    if (el >= g.durNs) return 0;
    double ax = std::fabs(g.sx), ay = std::fabs(g.sy), m = std::fmax(ax, ay);
    if (!g.catchUp || m <= 1) return 1 - double(el) / double(g.durNs);
    double dist = g.chebyshev ? m : gomath::hypot(ax, ay), c = m / dist;
    double u = double(el) * g.rate, u1 = std::log((m + 1.0 / 16) / (1 + 1.0 / 16)) / c;
    double d = u < u1 ? ((m + 1.0 / 16) * std::exp(-c * u) - 1.0 / 16) / c : 1 / c - (u - u1);
    return std::fmax(0, d / dist);
}

// The stock client's re-arm. Under TILE or PIXEL movement mode the move adds to the offset left, and an
// offset of two tiles on either axis is scaled back to two (the advancer). Under LEGACY mode an axis still moving the
// way it was drops what it had left, a turn that is not a reversal drops it all, and two tiles on either axis snaps.
bool armGlideOffset(const World& w, const GlideMotion* inFlight, int64_t nowNs, int mx, int my, uint8_t& lastDir,
                    double& sx, double& sy) {
    double rx = 0, ry = 0;
    if (inFlight && g_modelMutation != 30) {
        double left = glideLeft(*inFlight, nowNs);
        rx = inFlight->sx * left;
        ry = inFlight->sy * left;
    }
    uint8_t dir = uint8_t((mx < 0 ? 4 : mx > 0 ? 8 : 0) | (my < 0 ? 1 : my > 0 ? 2 : 0));
    bool legacy = w.movementMode == 0 && g_modelMutation != 31;
    if (legacy && (rx != 0 || ry != 0)) {
        if (rx * mx > 0) rx = 0;
        if (ry * my > 0) ry = 0;
        auto reverse = [](uint8_t d) { return uint8_t(((d & 3) ? (d & 3) ^ 3 : 0) | ((d & 12) ? (d & 12) ^ 12 : 0)); };
        if (dir != lastDir && dir != reverse(lastDir)) rx = ry = 0;
    }
    lastDir = dir;
    sx = rx + mx;
    sy = ry + my;
    double m = std::fmax(std::fabs(sx), std::fabs(sy));
    if (m >= kGlideMaxTiles && g_modelMutation != 31) {
        if (legacy) return false;
        sx *= kGlideMaxTiles / m;
        sy *= kGlideMaxTiles / m;
    }
    return true;
}

// LEGACY movement mode: the atom's rate is rewritten by every arm.
float autoGlideRate(const World& w, float prev, int64_t doneNs, const GlideMotion* inFlight, int64_t nowNs, int mx,
                    int my) {
    if (w.movementMode != 0) return kAutoGlidePx;
    float icon = float(iconSizeOf(w)), cap = icon * 0.5f;
    double rx = 0, ry = 0;
    if (inFlight) {
        double left = glideLeft(*inFlight, nowNs);
        rx = inFlight->sx * left;
        ry = inFlight->sy * left;
    }
    if (rx != 0 || ry != 0) {
        if (prev <= 0) prev = kAutoGlidePx;
        return (rx * mx > 0 || ry * my > 0) ? std::fmin(prev + 2, cap) : prev;
    }
    if (prev <= 0 || doneNs == 0) return kAutoGlidePx;
    double ticks = std::floor(double(nowNs - doneNs) / double(tickNs(w)));
    double f = double(icon) / prev + std::fmax(ticks, 0);
    double g = icon / (int(f) < 1 ? 1.0 : f);
    if (g < kAutoGlidePx) return kAutoGlidePx;
    if (g > cap) return cap;
    return (int(g) & 1) ? float(int(g) + 1) : float(g);
}

// ---- appearance anomaly instrument (model/anomaly.h); inert unless FUGUE_ANOMALY_LOG is set
namespace {
struct BindHist {
    uint32_t prev = 0, cur = 0;
    uint64_t curTick = 0;
    int64_t curNs = 0;
};
std::unordered_map<uint32_t, BindHist> g_bindHist;
std::unordered_map<uint32_t, uint64_t> g_bodyHash;     // appid -> hash of the last definition body
std::unordered_map<uint32_t, uint64_t> g_unheldBound;  // appid -> frame counter at the unheld bind
uint64_t g_anomSeq = 0;

// The fields in which two appearances differ, e.g. "icon_state,overlays".
std::string appDiff(const World& w, uint32_t a, uint32_t b) {
    auto ia = w.appearances.find(a), ib = w.appearances.find(b);
    if (ia == w.appearances.end() || ib == w.appearances.end()) return "absent";
    const proto::Appearance &x = ia->second.fields, &y = ib->second.fields;
    std::string o;
    auto add = [&](bool differs, const char* n) {
        if (!differs) return;
        if (!o.empty()) o += ',';
        o += n;
    };
    add(x.name != y.name, "name");
    add(x.suffix != y.suffix, "suffix");
    add(x.iconResID != y.iconResID, "icon");
    add(x.iconState != y.iconState, "icon_state");
    add(x.text != y.text, "text");
    add(x.maptext != y.maptext, "maptext");
    add(x.dir != y.dir, "dir");
    add(x.appearanceFlags != y.appearanceFlags, "flags");
    add(x.idSlots != y.idSlots, "idslots");
    add(x.verbs != y.verbs, "verbs");
    add(x.overlays != y.overlays, "overlays");
    add(x.underlays != y.underlays, "underlays");
    add(x.layerBits != y.layerBits, "layer");
    add(x.screenLoc != y.screenLoc, "screen_loc");
    add(x.hasColor != y.hasColor || x.color != y.color, "color");
    add(x.hasColorMatrix != y.hasColorMatrix || x.colorMatrixBits != y.colorMatrixBits, "colormatrix");
    add(x.hasPlane != y.hasPlane || x.plane != y.plane, "plane");
    add(x.hasPixelOffsets != y.hasPixelOffsets || x.pixelX != y.pixelX || x.pixelY != y.pixelY, "pixel");
    add(x.hasTransform != y.hasTransform || x.transformBits != y.transformBits, "transform");
    add(x.hasInvisibility != y.hasInvisibility || x.invisibility != y.invisibility, "invisibility");
    add(x.hasBlend != y.hasBlend || x.blend != y.blend, "blend");
    add(x.filters != y.filters, "filters");
    add(x.varsMask != y.varsMask, "varsmask");
    return o.empty() ? "none" : o;
}

std::string appLabel(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end()) return "#" + std::to_string(appid) + "<absent>";
    const proto::Appearance& f = it->second.fields;
    return "#" + std::to_string(appid) + "<" + f.name + "|icon" + std::to_string(f.iconResID) + ":" + f.iconState +
           "|dir" + std::to_string(f.dir) + "|ov" + std::to_string(f.overlays.size()) + ">";
}
uint64_t fnv(ByteSpan b) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < b.size(); i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}
}  // namespace

// A new mob record is marked for an immediate C2S::ResyncMob and flagged as awaiting the server's MobBind, which the
// resync sweep skips until it arrives.
static void markNewMob(World& w, uint32_t key) {
    w.resync.newMobs.insert(key & ~kRegistryA);
    w.atoms[key].pollAnswerDue = true;
}

// Loop order: New, Pos, Bind, Dir, Appearance, Pixel, Bounds, Step, VisContents, Particles. A1 and the off-view
// sentinel retract the atom's position; the high bit namespaces the registry.
// Record identity: the stock client keys every record by {id, Key}. A record for a held slot with a different Key tears
// the old atom down and builds a new one; a zero Key on either side is not compared. The teardown also arms the resync
// sweep. Without it, a reused slot would keep the dead atom's bounds, step and vis_contents. Every new record is marked
// for the immediate poll: a mob keeps the mark, an obj loses it to its own appearance binding. fresh = false is the
// resurrection guard (the record must already be held); true creates an absent id. id carries the registry bit.
Atom* Session::record(uint32_t id, uint16_t key, bool fresh) {
    World& w = w_;
    auto created = [&] {
        if (id & kRegistryA)
            markNewMob(w, id);
        else
            w.resync.newObjs.insert(id);
    };
    auto bindImages = [&](Atom& a) {
        w.vis.observed(id, a.renderIdentity);
        w.bindPendingImages(id, a.renderIdentity);
    };
    auto it = w.atoms.find(id);
    if (it == w.atoms.end()) {
        if (!fresh) return nullptr;
        Atom& a = w.atoms[id];
        a.key = key;
        a.haveKey = true;
        a.renderIdentity = ++w.nextAtomIdentity;
        a.touchTick = w.resync.tick;
        created();
        bindImages(a);
        return &a;
    }
    Atom& a = it->second;
    a.touchTick = w.resync.tick;
    if (w.resync.freed.erase(id)) {  // freed by the GC: rebuilt here, with no teardown
        a.bindOther = a.backlink = 0xffff;
        a.renderIdentity = ++w.nextAtomIdentity;
        w.vis.erase(id);
        a.iconStartNs = 0;
        w.flicks.erase(id);
        a.key = key;
        a.haveKey = true;
        created();
        bindImages(a);
        return &a;
    }
    // Rendering is stricter than the legacy zero-key teardown rule. A local
    // token detects observed slot lifetimes; it cannot prove server identity.
    const bool identityChanged = !a.renderIdentity || !a.haveKey || a.key != key;
    if (a.haveKey && a.key != key && a.key != 0 && key != 0 && g_modelMutation != 10 && !altRecordModel()) {
        if (anomaly::on()) {
            anomaly::note("slot-rebind", std::string(id & kRegistryA ? "mob:" : "obj:") +
                                             std::to_string(id & ~kRegistryA) + " key " + std::to_string(a.key) + "->" +
                                             std::to_string(key) + " was " + appLabel(w, a.appid));
            g_bindHist.erase(id);
        }
        a = Atom{};
        w.visContents.erase(id);
        w.visTurfs.erase(id);
        w.particles.erase(id);
        w.flicks.erase(id);
        w.resync.teardowns++;
        created();
    }
    if (identityChanged) {
        a.renderIdentity = ++w.nextAtomIdentity;
        w.vis.erase(id);
    }
    bindImages(a);
    a.key = key;
    a.haveKey = true;
    return &a;
}

// Stock DreamSeeker plays a non-looping icon state from its first frame when an atom, turf, image, screen object or stat
// row first shows it and again whenever the icon or icon_state changes; any other appearance change (dir, name, a moved
// screen_loc, an added overlay) keeps the running sequence, as do leaving and re-entering the view or the screen.
void Session::stampIcon(int64_t& startNs, uint32_t from, uint32_t to) const {
    bool changed = startNs == 0;
    if (!changed && from != to) {
        auto f = w_.appearances.find(from), t = w_.appearances.find(to);
        changed = f == w_.appearances.end() || t == w_.appearances.end() ||
                  f->second.fields.iconResID != t->second.fields.iconResID ||
                  f->second.fields.iconState != t->second.fields.iconState;
    }
    if (changed) startNs = ingestNs_;
}

void Session::bindAppearance(Atom& a, uint32_t appid, uint32_t id) {
    if (anomaly::on() && a.appid != appid) {
        const char* kind = id & kRegistryA ? "mob:" : "obj:";
        std::string who = std::string(kind) + std::to_string(id & ~kRegistryA) + " key " + std::to_string(a.key);
        if (appid && !w_.appearances.count(appid)) {
            anomaly::note("bind-unheld", who + " -> " + appLabel(w_, appid) + " was " + appLabel(w_, a.appid));
            g_unheldBound.emplace(appid, ++g_anomSeq);
        }
        BindHist& h = g_bindHist[id];
        if (h.cur != appid) {
            if (h.prev == appid && h.cur != 0 && ingestNs_ - h.curNs < 2000000000ll) {
                std::string kindOf = "same-icon";
                auto p = w_.appearances.find(appid), c = w_.appearances.find(h.cur);
                if (p != w_.appearances.end() && c != w_.appearances.end() &&
                    (p->second.fields.iconResID != c->second.fields.iconResID ||
                     p->second.fields.name != c->second.fields.name))
                    kindOf = "diff-icon";
                anomaly::note("flipflop", who + " " + kindOf + " [" + appDiff(w_, appid, h.cur) + "] " +
                                              appLabel(w_, appid) + " -> " + appLabel(w_, h.cur) + " -> back after " +
                                              std::to_string((ingestNs_ - h.curNs) / 1000000) + "ms");
            }
            h.prev = h.cur;
            h.cur = appid;
            h.curNs = ingestNs_;
        }
    }
    stampIcon(a.iconStartNs, a.appid, appid);
    a.appid = appid;
}

void Session::setTile(const std::array<uint16_t, 3>& key, uint32_t appid) {
    const auto previous = w_.tiles.find(key);
    if (previous == w_.tiles.end() || previous->second != appid) w_.tileRevision[key] = ++w_.nextTileRevision;
    w_.tiles[key] = appid;
    stampIcon(w_.tileIconStart[key], appid);
}

void Session::applyObjectArrays(const proto::ObjectArrays& m, uint32_t reg) {
    World& w = w_;
    auto rec = [&](const proto::RecordRef& r, bool fresh) { return record(reg | r.id, r.key, fresh); };
    for (const proto::RecordRef& r : m.created) {
        Atom* e = rec(r, true);  // an A1 on a held atom (same Key) retracts its location, keeps slot + appearance
        e->hasPos = false;
        e->bindOther = 0xffff;  // and its Bind
        e->x = e->y = e->z = 0;
        e->gliding = false;
    }
    for (const auto& p : m.pos) {
        if (p.x == 0xffff && p.y == 0xffff) {  // OffView
            Atom* e = rec(p.rec, true);
            e->hasPos = false;
            e->x = e->y = e->z = 0;
            e->gliding = false;
            continue;
        }
        Atom& e = *rec(p.rec, true);
        int fx = e.x, fy = e.y, fz = e.z;
        bool hadPos = e.hasPos;
        e.x = p.x + 1;
        e.y = p.y + 1;
        e.z = p.z;
        e.hasPos = true;
        // Arming a glide: presence only (the interpolation itself is render state).
        // A glide is armed for a one-tile (|dx|,|dy| <= GlideThresholdTiles = 1) same-z move of an atom
        // that already had a position; everything else (first sighting, z change, no move, a jump)
        // snaps, which clears it. Nothing else clears it but a retraction (A1 / off-view) or removal.
        // It starts from where the sprite is drawn (armGlideOffset).
        int dx = e.x - fx, dy = e.y - fy;
        bool glide = !(!hadPos || e.z != fz || (dx == 0 && dy == 0) || std::abs(dx) > kGlideThresholdTiles ||
                       std::abs(dy) > kGlideThresholdTiles || noStepsOf(w, e.appid));
        double sx = 0, sy = 0;
        if (glide) glide = armGlideOffset(w, e.gliding ? &e.glide : nullptr, ingestNs_, -dx, -dy, e.glideDir, sx, sy);
        if (glide) {
            float gs = glideSizeOf(w, e.appid);
            if (gs > 0)
                e.autoGlidePx = gs;
            else
                e.autoGlidePx = g_goAutoGlide ? float(iconSizeOf(w))
                                              : autoGlideRate(w, e.autoGlidePx, e.autoDoneNs,
                                                              e.gliding ? &e.glide : nullptr, ingestNs_, -dx, -dy);
            e.glide = glideMotion(sx, sy, e.autoGlidePx, iconSizeOf(w), tickNs(w), longGlideOf(w, e.appid), ingestNs_,
                                  g_modelMutation != 31);
            e.autoDoneNs = e.glide.startNs + e.glide.durNs;
        }
        e.gliding = glide;
    }
    for (const auto& b : m.bind) {
        if (Atom* e = rec(b.rec, false)) e->bindOther = b.other;  // read only by the resync sweep
    }
    for (const auto& d : m.dir) {
        Atom* e = rec(d.rec, false);
        if (!e) continue;  // resurrection guard
        e->dir = d.dir;
    }
    for (const auto& a : m.appearance) {
        Atom* e = rec(a.rec, false);
        if (!e) continue;
        bindAppearance(*e, a.appearance, reg | a.rec.id);
        // The stock client reads the glide size off the atom's current appearance as the glide runs, so a glide this
        // frame armed takes the rate of the appearance the same frame binds.
        if (e->gliding && e->glide.startNs == ingestNs_) {
            float gs = glideSizeOf(w, e->appid);
            if (gs > 0) e->autoGlidePx = gs;
            e->glide = glideMotion(e->glide.sx, e->glide.sy, gs > 0 ? gs : e->autoGlidePx, iconSizeOf(w), tickNs(w),
                                   longGlideOf(w, e->appid), ingestNs_, e->glide.catchUp);
            e->autoDoneNs = e->glide.startNs + e->glide.durNs;
        }
        // The obj binder clears the poll mark; the mob binder does not. So an obj that arrives with its appearance is
        // never polled, and one that arrives without is polled at once.
        if (reg != kRegistryA) w.resync.newObjs.erase(reg | a.rec.id);
    }
    for (const auto& p : m.pixel) {
        Atom* e = rec(p.rec, false);
        if (!e) continue;
        e->pixelX = p.x;
        e->pixelY = p.y;
        e->pixelW = p.w;
        e->pixelZ = p.z;
    }
    for (const auto& b : m.bounds) {
        Atom* e = rec(b.rec, false);
        if (!e) continue;
        e->boundX = b.x;
        e->boundY = b.y;
        e->boundW = b.w;
        e->boundH = b.h;
        e->hasBounds = true;
    }
    for (const auto& s : m.step) {
        Atom* e = rec(s.rec, false);
        if (!e) continue;
        e->stepX = s.x;
        e->stepY = s.y;
        e->stepSizeBits = s.sizeBits;
        e->hasStep = true;
    }
    for (const auto& vc : m.visContents) {
        // Validate/create the held slot's new identity BEFORE changing any table.
        // Missing holders do not get orphan memberships; a changed key may tear
        // down the old slot, which formerly erased the just-written turf list.
        Atom* holder = rec(vc.rec, false);
        if (!holder) continue;
        const uint32_t pid = reg | vc.rec.id;
        std::vector<VisReference> ordered;
        for (const proto::VisItem& item : vc.items) ordered.push_back({item.type, item.value});
        const bool accepted = w.vis.replace(pid, holder->renderIdentity, ordered, [&](uint32_t child) {
            const auto found = w.atoms.find(child);
            return found == w.atoms.end() || w.resync.freed.count(child) ? uint64_t(0) : found->second.renderIdentity;
        });
        if (!accepted) {
            w.visContents.erase(pid);
            w.visTurfs.erase(pid);
            holder->visCount = 0;
            continue;
        }
        std::vector<uint32_t> ids;
        std::vector<uint32_t> turfs;
        for (const proto::VisItem& it : vc.items)
            // An item's Type is the child's kind, not the frame's: 2 = obj, 3 = mob. Any other Type is unmodelled and skipped.
            if (it.type == 2)
                ids.push_back(it.value);
            else if (it.type == 3)
                ids.push_back(kRegistryA | it.value);
            else if (it.type == 1)
                turfs.push_back(it.value);  // a turf: World::visTurfs
        if (turfs.empty())
            w.visTurfs.erase(pid);
        else
            w.visTurfs[pid] = std::move(turfs);
        holder->visCount = int(ids.size());
        if (ids.empty()) {
            w.visContents.erase(pid);
            continue;
        }
        w.visContents[pid] = std::move(ids);
    }
    // The particle set: particleID 0xffff detaches it. Another particleID is another /particles
    // datum, which clears the live particles; the same one only updates the definition, and they carry on.
    for (const auto& pr : m.particles) {
        uint32_t key = reg | pr.rec.id;
        rec(pr.rec, false);
        if (pr.particleID == 0xffff) {
            w.particles.erase(key);
            continue;
        }
        ParticleSet& ps = w.particles[key];
        if (ps.pid != pr.particleID) ps.live.clear();
        ps.pid = pr.particleID;
        if (pr.def) applyParticleBlock(ps, *pr.def);  // a block that does not parse leaves the definition as it was
    }
}

// Adds a chat line: both rings grow together, an all-empty line is dropped, the oldest
// lines fall off past the cap.
void Session::addChat(const std::string& plain, const std::string& markup) {
    World& w = w_;
    if (plain.empty() && markup.empty()) return;
    w.chat.push_back(net::plainText(plain));
    w.chatHtml.push_back(net::plainText(markup));
    w.chatTotal++;
    if (w.chat.size() > kChatCap) {
        size_t over = w.chat.size() - kChatCap;
        w.chat.erase(w.chat.begin(), w.chat.begin() + long(over));
        w.chatHtml.erase(w.chatHtml.begin(), w.chatHtml.begin() + long(over));
    }
}

void Session::ingest(uint16_t typ, ByteSpan body) {
    World& w = w_;
    if (!w.iconClockNs) w.iconClockNs = ingestNs_;
    if (anomaly::on()) anomaly::frame(typ, w.resync.tick, ingestNs_);
    // Runs one decoder: a malformed body is counted in ingestErrors() and yields nothing.
    auto decoded = [&](auto&& decode) -> std::optional<std::invoke_result_t<decltype(decode)>> {
        try {
            return decode();
        } catch (const std::exception&) {
            decodeErrors_++;
            return std::nullopt;
        }
    };
    switch (typ) {
        case S2C::MapGrid: {
            // The single-frame grid is stored (the Cells() fallback), then the refresh machine is driven. A malformed
            // grid changes nothing and is not counted.
            proto::MapGrid m;
            try {
                m = proto::decodeMapGrid(body);
            } catch (const std::exception&) { return; }
            w.grid = {true, m.x0, m.y0, m.z, std::move(m.cells)};
            w.resync.relinkObjs = w.resync.relinkMobs = true;  // the window reset arms both relinks
            w.resync.haveOrigin = true;
            w.resync.originX = w.grid.x0;
            w.resync.originY = w.grid.y0;
            try {
                proto::MapRefresh r = proto::decodeMapRefresh(st_, S2C::MapGrid, body);
                w.viewZ = r.z;
                for (const auto& c : r.cells) setTile({c.x, c.y, r.z}, c.appid);
            } catch (const std::exception&) { w.mapViewErrs++; }
            return;
        }
        case S2C::MapRefreshFull:
        case S2C::MapRefresh: {
            auto r = decoded([&] { return proto::decodeMapRefresh(st_, typ, body); });
            if (!r) {
                w.mapViewErrs++;
                return;
            }
            // The stock handler arms both relinks only when the window origin moves on either axis. MapGrid always arms
            // them.
            if (!w.resync.haveOrigin || r->originX != w.resync.originX || r->originY != w.resync.originY)
                w.resync.relinkObjs = w.resync.relinkMobs = true;
            w.resync.haveOrigin = true;
            w.resync.originX = r->originX;
            w.resync.originY = r->originY;
            w.viewZ = r->z;
            for (const auto& c : r->cells) setTile({c.x, c.y, r->z}, c.appid);
            return;
        }
        case S2C::VisTurfs: {
            // The remote turfs a vis_contents names (vg's camera console): each turf's look goes into `tiles` and its
            // turf-registry index into `areaCells`, at the cell World::visTurfs indexes. A malformed body, or one before
            // the Bounds, changes nothing.
            proto::VisTurfs m;
            try {
                m = proto::decodeVisTurfs(body);
            } catch (const std::exception&) { return; }
            int mx = w.bounds.maxX, my = w.bounds.maxY;
            if (mx <= 0 || my <= 0) return;
            for (size_t i = 0; i < m.cells.size(); i++) {
                uint32_t c = m.cells[i];
                std::array<uint16_t, 3> key{uint16_t(c % uint32_t(mx)), uint16_t(c / uint32_t(mx) % uint32_t(my)),
                                            uint16_t(c / (uint32_t(mx) * uint32_t(my)))};
                setTile(key, m.looks[i]);
                w.areaCells[key] = m.areas[i];
            }
            for (const auto& b : m.binds) w.turfBinds[b.index] = b.appid;
            return;
        }
        case S2C::AreaGrid: {
            auto m = decoded([&] { return proto::decodeAreaGrid(st_, body); });
            if (!m) {
                w.mapViewErrs++;
                return;
            }
            for (const auto& b : m->binds) w.turfBinds[b.index] = b.appid;
            // The grid is per-cell AREA indices (World::areaCells), on the z of the refresh that armed it.
            uint16_t z = st_.get<proto::g2::MapView>().z;
            for (const auto& c : m->cells) w.areaCells[{c.x, c.y, z}] = c.index;
            return;
        }
        case S2C::VisContents: {
            // vis_contents refresh. Tag 1 = a turf: its appearance and its vis_contents, item Type 2 obj, 3 mob
            // (any other Type is skipped). appid 0xffff with no items empties the vis_contents and leaves the turf's appearance
            // as it was; otherwise both are replaced. Tags 2/3 (an obj's / mob's vis_contents) and 13 are not modelled here.
            auto m = decoded([&] { return proto::decodeVisContents(body); });
            if (!m) {
                w.cellRefreshErrs++;
                return;
            }
            for (const auto& r : m->records) {
                if (r.tag != 1) continue;
                std::array<uint16_t, 3> key{r.x, r.y, r.z};
                if (r.appid == 0xffff && r.contents.empty()) {
                    w.turfVis.erase(key);
                    w.vis.erase(VisContents::turf(key[0], key[1], key[2]));
                    if (g_modelMutation == 12) {
                        w.tiles.erase(key);
                        w.tileIconStart.erase(key);
                    }  // test-only mutation: the tile is cleared too
                    continue;
                }
                setTile(key, r.appid);
                std::vector<VisReference> ordered;
                for (const proto::VisItem& item : r.contents) ordered.push_back({item.type, item.value});
                if (!w.vis.replace(VisContents::turf(key[0], key[1], key[2]), 0, ordered, [&](uint32_t child) {
                        const auto found = w.atoms.find(child);
                        return found == w.atoms.end() || w.resync.freed.count(child) ? uint64_t(0)
                                                                                     : found->second.renderIdentity;
                    })) {
                    w.turfVis.erase(key);
                    continue;
                }
                std::vector<uint32_t> ids;
                for (const proto::VisItem& it : r.contents)
                    if (it.type == 2)
                        ids.push_back(it.value);
                    else if (it.type == 3)
                        ids.push_back(kRegistryA | it.value);
                if (ids.empty())
                    w.turfVis.erase(key);
                else
                    w.turfVis[key] = std::move(ids);
            }
            return;
        }
        case S2C::Timing: {
            // Each accepted in [10,1000] ms.
            proto::Timing m = proto::decodeTiming(body);
            if (m.tickMs && *m.tickMs >= 10 && *m.tickMs <= 1000) w.tickNanos = int64_t(*m.tickMs) * 1'000'000;
            if (m.frameMs && *m.frameMs >= 10 && *m.frameMs <= 1000) w.frameNanos = int64_t(*m.frameMs) * 1'000'000;
            return;
        }
        case S2C::Bounds: {
            auto m = decoded([&] { return proto::decodeBounds(st_, body); });  // also feeds st_.mapView
            if (!m) return;
            // u16[4..5] are the eye-centred extent that admits light sources, smaller than the buffered map window
            // (decodeBounds refuses a body under 14 bytes).
            w.bounds = {true,
                        m->maxX,
                        m->maxY,
                        m->view,
                        m->winW,
                        m->winH,
                        m->viewW,
                        m->viewH,
                        int(le16(&body[8])),
                        int(le16(&body[10]))};
            return;
        }
        case S2C::FlickTarget: {
            // DD sends it before a flick. The stock client reads it as a record check and appearance bind for an obj
            // (kind 2) or a mob (kind 3) only; the flick itself starts with the FlickState, and a repeated same-state
            // flick need not change this appearance at all.
            auto m = decoded([&] { return proto::decodeFlickTarget(body); });
            if (!m) return;
            const uint8_t kind = m->loc.kind;
            if ((kind != 2 && kind != 3) || (m->loc.ref & kRegistryA)) return;
            const uint32_t id = m->loc.ref | (kind == 3 ? kRegistryA : 0);
            // An obj's record is made here; a mob's is only checked, so a mob made this tick does not flick.
            Atom* a = record(id, m->loc.aux, kind == 2);
            if (a && m->hasAppid) {
                bindAppearance(*a, m->appid, id);
                if (kind == 2) w.resync.newObjs.erase(id);
            }
            return;
        }
        case S2C::FlickState: {  // flick() payload half
            auto m = decoded([&] { return proto::decodeFlickState(body); });
            if (!m) return;
            auto start = [&](World::Flick& f) {
                f.iconState = m->iconState;
                f.iconResIndex = m->iconResIndex;
                f.haveRes = true;
                f.startNs = ingestNs_;
            };
            if (m->loc.kind == 1) {  // a turf the client holds, by its index in the world
                const uint32_t mx = w.bounds.maxX, my = w.bounds.maxY;
                if (!mx || !my) return;
                const std::array<uint16_t, 3> cell{uint16_t(m->loc.ref % mx), uint16_t(m->loc.ref / mx % my),
                                                   uint16_t(m->loc.ref / mx / my)};
                if (!w.tiles.count(cell) || (!w.turfFlicks.count(cell) && w.turfFlicks.size() >= 65536)) return;
                start(w.turfFlicks[cell]);
                return;
            }
            const uint32_t id = m->loc.ref | (m->loc.kind == 0 ? kRegistryA : 0);
            const auto atom = w.atoms.find(id);
            const uint16_t key = m->loc.aux;
            // The record is checked and bound by the FlickTarget that goes with it. Stock holds the flick of an absent or
            // recycled record for a few ticks; here it is dropped.
            if (atom == w.atoms.end() || w.resync.freed.count(id) ||
                (atom->second.haveKey && atom->second.key && key && atom->second.key != key))
                return;
            if (!w.flicks.count(id) && w.flicks.size() >= 65536) return;
            World::Flick& f = w.flicks[id];
            start(f);
            f.key = atom->second.key;
            f.haveKey = atom->second.haveKey;
            return;
        }
        case S2C::Appearance: {
            w.resync.appearances++;  // the stock GC collects past 500 of these
            // A malformed one is counted in ingestErrors() and is absent from the snapshot.
            auto m = decoded([&] { return proto::decodeAppearance(st_, body); });
            if (!m) return;
            uint32_t appid = m->appid;
            if (anomaly::on()) {
                uint64_t h = fnv(body);
                auto held = w.appearances.find(appid);
                if (held != w.appearances.end()) {
                    auto old = g_bodyHash.find(appid);
                    bool differs = old == g_bodyHash.end() || old->second != h;
                    anomaly::note(differs ? "redef-diff" : "redef-same",
                                  appLabel(w, appid) + " -> " + (differs ? "new content " : "") + m->name + "|icon" +
                                      std::to_string(m->iconResID) + ":" + m->iconState);
                }
                g_bodyHash[appid] = h;
                if (auto ub = g_unheldBound.find(appid); ub != g_unheldBound.end()) {
                    anomaly::note("late-def", "#" + std::to_string(appid) + " defined " +
                                                  std::to_string(g_anomSeq - ub->second) + " unheld-binds after");
                    g_unheldBound.erase(ub);
                }
            }
            if (m->layerBits == 0x40000000u)
                w.layer2Appids.insert(appid);
            else
                w.layer2Appids.erase(appid);  // World::layer2Appids
            w.layer2Indexed = true;
            w.appearances[appid] = {std::move(*m), w.resync.tick};
            return;
        }
        case S2C::MobBind:
        case S2C::ObjBind: {
            auto m =
                decoded([&] { return typ == S2C::MobBind ? proto::decodeMobBind(body) : proto::decodeObjBind(body); });
            if (!m) return;
            uint32_t ref = m->ref, appid = m->appid;
            w.refBind[ref] = appid;  // keyed by ref alone, not by table
            // Each message names ONE table. The S2C::MobBind binds the MOB table (the one C2S::ResyncMob polls) and
            // clears the record's resync marks. ObjBind binds the OBJ table (the one C2S::ResyncObj polls). Both create
            // an absent record, as DreamDaemon answers a poll for an id outside the client's window with its binding.
            // The same binding clears the new record's poll mark.
            if (altRecordModel()) {  // test-only hook (setModelMutation)
                if (w.haveSelf && ref == w.selfRef) {
                    auto it = w.atoms.find(ref | kRegistryA);
                    if (it != w.atoms.end()) it->second.appid = appid;
                    return;
                }
                if (w.atoms.count(ref) && w.atoms.count(ref | kRegistryA)) return;
                for (uint32_t key : {ref, ref | kRegistryA}) {
                    auto it = w.atoms.find(key);
                    if (it != w.atoms.end()) it->second.appid = appid;
                }
                return;
            }
            uint32_t id = typ == S2C::MobBind ? (ref | kRegistryA) : ref;
            if (w.resync.freed.erase(id)) {  // rebuilt, bound: no mark
                w.atoms[id].bindOther = w.atoms[id].backlink = 0xffff;
                w.atoms[id].renderIdentity = ++w.nextAtomIdentity;
                w.vis.erase(id);
            }
            Atom& a = w.atoms[id];
            if (!a.renderIdentity) a.renderIdentity = ++w.nextAtomIdentity;
            w.vis.observed(id, a.renderIdentity);
            a.touchTick = w.resync.tick;  // a lookup marks the record for the GC (FUGUE_RESYNC_GC) for this tick
            if (anomaly::on() && a.appid != 0 && a.appid != appid)
                anomaly::note(typ == S2C::MobBind ? "mobbind-change" : "objbind-change",
                              std::string(typ == S2C::MobBind ? "mob:" : "obj:") + std::to_string(ref) + " key " +
                                  std::to_string(a.key) + " " + appLabel(w, a.appid) + " -> " + appLabel(w, appid) +
                                  " [" + appDiff(w, a.appid, appid) +
                                  "] self=" + (w.haveSelf && ref == w.selfRef ? "1" : "0"));
            bindAppearance(a, appid, id);
            a.pollAnswerDue = false;
            (typ == S2C::MobBind ? w.resync.newMobs : w.resync.newObjs).erase(ref);
            return;
        }
        case S2C::MobGone:
        case S2C::ObjGone: {
            // The server's "gone" answer to a resync poll is MobGone for a mob, ObjGone for an obj. The stock client
            // frees each held record it names, which also arms the sweep. Freeing keeps a dead atom's record, and its
            // vis_contents, from lingering into a reused slot.
            uint32_t reg = typ == S2C::MobGone ? kRegistryA : 0u;
            for (uint32_t id : proto::decodeGone(body).ids) {
                uint32_t key = reg | id;
                w.vis.deleted(key);
                if (auto pending = w.pendingImageTargets.find(key); pending != w.pendingImageTargets.end()) {
                    for (uint32_t image : pending->second) w.imageInstances.at(image).targetIdentity = UINT64_MAX;
                    w.pendingImageTargets.erase(pending);
                }
                if (w.resync.freed.erase(key)) {
                    w.atoms.erase(key);
                    w.flicks.erase(key);
                    continue;
                }  // the GC freed it: not held, nothing to tear down
                if (!w.atoms.erase(key)) continue;  // stock: a record it does not hold is skipped and arms nothing
                g_bindHist.erase(key);
                (reg ? w.resync.newMobs : w.resync.newObjs).erase(key & ~kRegistryA);
                w.visContents.erase(key);
                w.visTurfs.erase(key);
                w.particles.erase(key);
                w.flicks.erase(key);
                w.resync.teardowns++;
            }
            (reg ? relinkMobs : relinkObjs)(w);  // the handler relinks at once
            return;
        }
        case S2C::ContentsDelta: {
            auto m = decoded([&] { return proto::decodeContentsDelta(body); });
            if (!m) return;
            // The in-view delta, except that every array's meaning is usr.contents (below). Array 1 is an obj leaving it, so it
            // creates no dead atom. Array 3 is a mob leaving it, so it erases nothing from the atom table. Array 4 is a mob
            // entering it, so its appid goes on the mob's key, not the bare obj key. The cell-less record that array 2 leaves
            // in the atom table is kept: every verb path ignores a record with no cell, and a drop
            // re-sends the obj in full, with its array-1 before the InView that places it again.
            for (const auto& p : m->bindA) bindAppearance(w.atoms[p.id], p.appearance, p.id);
            for (const auto& p : m->bindB)
                if (auto it = w.atoms.find(p.id | kRegistryA); it != w.atoms.end())
                    bindAppearance(it->second, p.appearance, p.id | kRegistryA);
            // usr.contents (World::usrContents): array 1 = an obj leaving it, 2 = an obj entering it, 3 = a mob leaving it,
            // 4 = a mob entering it.
            auto leave = [&](uint32_t key) {
                w.usrContents.erase(key);
                w.usrContentsOrder.erase(std::remove(w.usrContentsOrder.begin(), w.usrContentsOrder.end(), key),
                                         w.usrContentsOrder.end());
            };
            auto enter = [&](uint32_t key, uint32_t appid) {
                if (!w.usrContents.count(key)) w.usrContentsOrder.push_back(key);
                w.usrContents[key] = appid;
            };
            for (uint32_t id : m->addA) leave(id);
            for (const auto& p : m->bindA) enter(p.id, p.appearance);
            for (uint32_t id : m->removeB) leave(id | kRegistryA);
            for (const auto& p : m->bindB) enter(p.id | kRegistryA, p.appearance);
            return;
        }
        case S2C::InView: {
            auto m = decoded([&] { return proto::decodeInView(body); });
            if (!m) return;
            applyObjectArrays(m->objects, 0);
            resyncAfterArrays(w, m->objects, false);  // the parser's tail relinks
            return;
        }
        case S2C::ClientDir:
            if (!body.empty()) w.clientDir = uint8_t(body[0]);
            return;
        case S2C::MouseSubs:
            if (auto m = decoded([&] { return proto::decodeMouseSubs(body); }))
                w.movementMode = uint8_t((m->extra >> 2) & 3);
            return;
        case S2C::WorldState: {
            auto m = decoded([&] { return proto::decodeWorldState(body); });
            if (!m) return;
            if (m->eye) {
                w.eyeGlideSizeBits = m->eye->glideSizeBits;
                w.eyeLongGlide = ((m->eye->flags32 >> 21) & 1) != 0;  // AppearanceFlagsShift=21, LONG_GLIDE=1<<0
                if (bitsToFloat(m->eye->stepSizeBits) > 0) w.stepSizeBits = m->eye->stepSizeBits;
            }
            const auto& vis = m->vision;
            if (vis && (vis->sub & 0x01)) {
                w.seeInDark = vis->seeInDark;
                w.haveSeeInDark = true;
            }
            if (vis && (vis->sub & 0x02)) {
                w.seeInvisible = vis->seeInvisible;
                w.haveSeeInvisible = true;
            }
            if (m->hasSight) {
                w.sight = w.narrowSight = m->sight;
                w.haveSight = w.haveNarrowSight = true;
            }
            if (vis && (vis->sub & 0x04)) {
                w.sight = w.wideSight = vis->sightWide;
                w.haveSight = w.haveWideSight = true;
            }
            // client.pixel_x/y (Sub&0x10), and all four of x/y/w/z (Sub&0x20); the server sends both together.
            if (vis && (vis->sub & 0x10)) {
                w.viewPixelX = int16_t(vis->viewOrigin[0]);
                w.viewPixelY = int16_t(vis->viewOrigin[1]);
            }
            if (vis && (vis->sub & 0x20)) {
                w.viewPixelX = int16_t(vis->screenOffset[0]);
                w.viewPixelY = int16_t(vis->screenOffset[1]);
                w.viewPixelW = int16_t(vis->screenOffset[2]);
                w.viewPixelZ = int16_t(vis->screenOffset[3]);
            }
            applyObjectArrays(m->objects, kRegistryA);
            resyncAfterArrays(w, m->objects, true);  // the parser's tail relinks
            int oldEyeX = w.eyeX, oldEyeY = w.eyeY;
            bool hadEye = w.haveEye;
            if (m->hasX) {
                w.trkX = m->eyeX;
                w.seenX = true;
            }
            if (m->hasY) {
                w.trkY = m->eyeY;
                w.seenY = true;
            }
            if (w.seenX && w.seenY) {
                w.eyeX = w.trkX + 1;
                w.eyeY = w.trkY + 1;
                w.haveEye = true;
            }
            // Arms the eye glide: ONE vector for the whole (possibly diagonal) move, re-armed like an atom's
            // (armGlideOffset). A move further than the threshold on either axis snaps the view, as it snaps the player's
            // sprite.
            if (hadEye) {
                int dx = w.eyeX - oldEyeX, dy = w.eyeY - oldEyeY;
                if (dx != 0 || dy != 0) {
                    double sx = 0, sy = 0;
                    bool hadGlide = w.eyeGliding;
                    GlideMotion prevGlide = w.eyeGlide;
                    w.eyeGliding = std::abs(dx) <= kGlideThresholdTiles && std::abs(dy) <= kGlideThresholdTiles &&
                                   armGlideOffset(w, w.eyeGliding ? &w.eyeGlide : nullptr, ingestNs_, -dx, -dy,
                                                  w.eyeGlideDir, sx, sy);
                    if (w.eyeGliding) {
                        float gs = bitsToFloat(w.eyeGlideSizeBits);
                        if (gs > 0)
                            w.eyeAutoPx = gs;
                        else
                            w.eyeAutoPx = g_goAutoGlide
                                              ? float(iconSizeOf(w))
                                              : autoGlideRate(w, w.eyeAutoPx, w.eyeAutoDoneNs,
                                                              hadGlide ? &prevGlide : nullptr, ingestNs_, -dx, -dy);
                        w.eyeGlide =
                            glideMotion(sx, sy, w.eyeAutoPx, iconSizeOf(w), tickNs(w), w.eyeLongGlide, ingestNs_, true);
                        w.eyeAutoDoneNs = w.eyeGlide.startNs + w.eyeGlide.durNs;
                    }
                    // The player's own mob stays where the view puts it: a glide this frame armed on the eye's tile
                    // runs the view's motion.
                    auto self = w.atoms.find(w.selfRef | kRegistryA);
                    if (w.haveSelf && self != w.atoms.end() && self->second.gliding &&
                        self->second.glide.startNs == ingestNs_ && self->second.x == w.eyeX && self->second.y == w.eyeY)
                        self->second.glide = w.eyeGliding ? w.eyeGlide : GlideMotion{};
                }
            }
            return;
        }
        case S2C::Animate: {
            // Only the settled appid matters: an atom held with no appearance adopts the last step's `to`.
            auto m = decoded([&] { return proto::decodeAnimate(body); });
            if (!m) return;
            if (m->kind == 5) {  // animate(client)
                scheduleChain(w.clientChain, *m, ingestNs_, kAnimUnitNanos);
                return;
            }
            // An animate() names its target by {ref, Key} like an InView record does, so it runs the same Key check and
            // get-or-create: a Key change is a teardown, and an absent record is built and marked.
            // This runs before the step check: the closing frame of a tween has no steps and still carries the Key.
            if ((m->kind == 2 || m->kind == 3) && !altRecordModel())
                record(m->ref | (m->kind == 3 ? kRegistryA : 0u), m->ver, true);
            const bool steps = !m->steps.empty();
            // An obj animate with steps binds the obj's appearance, which clears the poll mark like an A5 does.
            if (steps && m->kind == 2 && !altRecordModel()) w.resync.newObjs.erase(m->ref);
            if (m->kind == 13) {
                // Only an image we hold. A chain is never expired by time, so its snapshot effect is set membership.
                // An image's animation is sent just before the image when a client is first shown one that is already
                // animating; stock runs it (516.1689, stock-parity-world shared-render-target). Otherwise only an image we hold.
                uint32_t img = m->ref;
                if (!w.images.count(img)) {
                    if (!steps || w.earlyImageAnims.size() >= 65536) return;
                    w.earlyImageAnims.insert(img);
                }
                if (steps) w.imageAnims.insert(img);
                if (steps || w.imageChains.count(img)) scheduleChain(w.imageChains[img], *m, ingestNs_, kAnimUnitNanos);
                return;
            }
            if (m->kind == 1) {  // a turf the client holds, by its index in the world (as in FlickState)
                const uint32_t mx = w.bounds.maxX, my = w.bounds.maxY;
                if (!mx || !my) return;
                const std::array<uint16_t, 3> cell{uint16_t(m->ref % mx), uint16_t(m->ref / mx % my), uint16_t(m->ref / mx / my)};
                if (!w.tiles.count(cell) || (!w.turfChains.count(cell) && (!steps || w.turfChains.size() >= 65536))) return;
                scheduleChain(w.turfChains[cell], *m, ingestNs_, kAnimUnitNanos);
                return;
            }
            if (m->kind != 2 && m->kind != 3) return;
            uint32_t key = m->ref | (m->kind == 3 ? kRegistryA : 0u);
            auto it = w.atoms.find(key);
            if (it == w.atoms.end()) return;
            if (steps) {
                uint32_t to = m->steps.back().to;
                if (to == 0) return;
                if (it->second.appid == 0) bindAppearance(it->second, to, key);
                it->second.animating = true;  // Set while an animate() chain is held (never cleared by elapsed time)
            }
            scheduleChain(it->second.chain, *m, ingestNs_, kAnimUnitNanos);
            return;
        }
        case S2C::SelfRef: {
            auto m = decoded([&] { return proto::decodeSelfRef(body); });
            if (!m) return;
            // A new own mob starts usr.contents empty. This is order-dependent: the server sends this SelfRef before
            // the new mob's array-2 pairs, so the clear never removes them.
            if (w.haveSelf && w.selfRef != m->value) {
                w.usrContents.clear();
                w.usrContentsOrder.clear();
            }
            w.selfRef = m->value;
            w.haveSelf = true;
            // The stock SelfRef handler builds the mob record when it is absent, and a new mob record is marked for the
            // resync poll. So the client's first word about its own mob is C2S::ResyncMob [it]. A held record is not
            // marked.
            if (!altRecordModel() &&
                (!w.atoms.count(w.selfRef | kRegistryA) || w.resync.freed.erase(w.selfRef | kRegistryA))) {
                Atom& a = w.atoms[w.selfRef | kRegistryA];
                a.bindOther = a.backlink = 0xffff;
                markNewMob(w, w.selfRef | kRegistryA);
            }
            return;
        }
        case S2C::WorldName: w.name = proto::decodeText(body).text; return;
        case S2C::ClientAppearance: {
            auto m = decoded([&] { return proto::decodeClientAppearance(body); });
            if (!m) return;
            w.clientAppid = m->appid;
            w.haveClientAppid = true;
            return;
        }
        case S2C::HostMessage: {
            // Trailing CR/LF trimmed; blank ignored.
            std::string msg = proto::decodeText(body).text;
            while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();
            if (trimAscii(msg).empty()) return;
            w.hostMessage = msg;
            // Each non-blank trimmed line goes to the chat ring.
            size_t ls = 0;
            while (ls <= msg.size()) {
                size_t le = msg.find('\n', ls);
                if (le == std::string::npos) le = msg.size();
                std::string ln = trimAscii(std::string_view(msg).substr(ls, le - ls));
                if (!ln.empty()) {
                    std::string line = "*** " + ln + " ***";
                    addChat(line, htmlEscape(line));
                }
                ls = le + 1;
            }
            return;
        }
        case S2C::SessionEnd: {
            auto m = decoded([&] { return proto::decodeSessionEnd(body); });
            if (!m) return;
            w.sessionEnd = {true, m->reason, m->requiredVersion, sessionEndText(m->reason, m->requiredVersion)};
            {  // Shown in chat as a "*** ... ***" line.
                std::string line = "*** " + w.sessionEnd.text + " ***";
                addChat(line, htmlEscape(line));
            }
            return;
        }
        case S2C::Browse: {
            // Keyed by the window= param. Close removes the window and its place in the arrival order; a re-browse
            // of an open window keeps its place.
            auto m = decoded([&] { return proto::decodeBrowse(body); });
            if (!m) return;
            std::string params = net::plainText(m->params);  // as parseBrowseFrame: stock drops the markers
            std::string name = browserWindowName(params);
            if (m->close) {
                w.browsers.erase(name);
                for (size_t i = 0; i < w.browserOrder.size(); i++)
                    if (w.browserOrder[i] == name) {
                        w.browserOrder.erase(w.browserOrder.begin() + long(i));
                        break;
                    }
            } else {
                if (!w.browsers.count(name)) w.browserOrder.push_back(name);
                w.browsers[name] = {params, net::plainText(m->html)};
            }
            return;
        }
        case S2C::Output: {
            // A page-JS call, a whole-page replace of a live window, or chat.
            auto m = decoded([&] { return proto::decodeOutput(body); });
            if (!m) return;
            const std::string& control = m->control;
            std::string win, fn;
            if (splitBrowserOutput(control, win, fn) && !fn.empty()) {  // page JS call: counted, not chat
                w.browserCalls[win]++;
                return;
            }
            if (browserControlWindow(control, win)) {
                auto b = w.browsers.find(win);
                if (b != w.browsers.end()) {
                    b->second.html = decodeChatHTML(m->value);
                    return;
                }  // page replacement
            }
            if (!control.empty() && control.find(':') == std::string::npos && textTarget_ && !textTarget_(control))
                return;
            if (!control.empty()) {
                std::string sw, sfn;
                splitBrowserOutput(control, sw, sfn);
                w.chatControl = !sw.empty() ? sw : control;
            }
            std::string markup = decodeChatHTML(m->value);
            addChat(stripChatMarkup(markup), markup);
            return;
        }
        case S2C::Text: {
            // No registry entry: one trailing newline stripped, then the markup is url-decoded and added to chat.
            std::string t = proto::decodeText(body).text;
            if (!t.empty() && t.back() == '\n') t.pop_back();
            std::string markup = decodeChatHTML(t);
            addChat(stripChatMarkup(markup), markup);
            return;
        }
        case S2C::ResourceList: {
            // Appends: the ordinal space runs across frames.
            auto m = decoded([&] { return proto::decodeResourceList(st_, body); });
            if (!m) return;
            for (const auto& e : m->entries) w.resources.emplace_back(e.id, e.type);
            return;
        }
        case S2C::Verb: {
            // De-dup by id, keep first-arrival order, last record wins.
            auto m = decoded([&] { return proto::decodeVerb(st_, body); });
            if (!m) return;
            uint32_t id = m->id;
            if (!w.verbById.count(id)) w.verbOrder.push_back(id);
            w.verbById[id] = std::move(*m);
            return;
        }
        case S2C::ImageCreate:
        case S2C::ImageDelete:
        case S2C::ImageModify: {
            // ImageDelete drops imageID and every moreIDs entry; ImageCreate / ImageModify store the record under
            // imageID. imageAnims mirrors the image animation keys.
            auto m = decoded([&] {
                return typ == S2C::ImageCreate   ? proto::decodeImageCreate(body)
                       : typ == S2C::ImageDelete ? proto::decodeImageDelete(body)
                                                 : proto::decodeImageModify(body);
            });
            if (!m) return;
            uint32_t id = m->imageID;
            auto unpend = [&](uint32_t i) {
                const auto image = w.imageInstances.find(i);
                if (image == w.imageInstances.end()) return;
                auto pending = w.pendingImageTargets.find(image->second.target);
                if (pending == w.pendingImageTargets.end()) return;
                pending->second.erase(i);
                if (pending->second.empty()) w.pendingImageTargets.erase(pending);
            };
            if (typ == S2C::ImageDelete) {
                auto drop = [&](uint32_t i) {
                    unpend(i);
                    w.vis.erase(VisContents::image(i));
                    w.images.erase(i);
                    w.imageInstances.erase(i);
                    w.imageAnims.erase(i);
                    w.earlyImageAnims.erase(i);
                    w.imageChains.erase(i);
                    w.imageIconStart.erase(i);
                };
                drop(id);
                for (uint32_t more : m->moreIDs) drop(more);
            } else {
                if (!w.images.count(id) && w.images.size() >= 65536) return;
                auto& image = w.imageInstances[id];
                const uint8_t kind = m->locKind;
                const uint32_t ref = m->locRef;
                const uint32_t target = kind == 7 ? m->cell : ref | (kind == 2 ? kRegistryA : 0u);
                const bool fresh = typ == S2C::ImageCreate || !image.identity;
                const bool moved = image.locKind != kind || image.target != target;
                if (fresh || moved) image.identity = ++w.nextImageIdentity;
                if (fresh || moved) {
                    unpend(id);
                    image.targetIdentity = 0;
                    image.targetHud = false;
                }
                image.appid = m->appid;
                image.locKind = kind;
                image.target = target;
                if ((fresh || moved) && kind == 7) {
                    image.turfWidth = image.turfHeight = 0;
                    const uint64_t area = uint64_t(w.bounds.maxX) * w.bounds.maxY;
                    // Recorded ImageCreate turf cells are zero-based row-major world indices.
                    if (w.bounds.maxX > 0 && w.bounds.maxX <= UINT16_MAX && w.bounds.maxY > 0 &&
                        w.bounds.maxY <= UINT16_MAX && area && target / area <= UINT16_MAX) {
                        image.turfCell = {uint16_t(target % w.bounds.maxX),
                                          uint16_t((target / w.bounds.maxX) % w.bounds.maxY), uint16_t(target / area)};
                        image.turfWidth = uint16_t(w.bounds.maxX);
                        image.turfHeight = uint16_t(w.bounds.maxY);
                    }
                }
                image.hasVis = !m->visContents.empty();
                if (fresh || moved) w.vis.erase(VisContents::image(id));
                std::vector<VisReference> tail;
                for (const proto::VisItem& child : m->visContents) tail.push_back({child.type, child.value});
                w.vis.replace(VisContents::image(id), image.identity, tail, [&](uint32_t child) {
                    const auto a = w.atoms.find(child);
                    return a != w.atoms.end() && a->second.haveKey && !w.resync.freed.count(child)
                               ? a->second.renderIdentity
                               : uint64_t(0);
                });
                // Bind once, including normal image-before-target delivery. A same-
                // location modify never rebinds a pinned image to a recycled slot.
                if (!image.targetIdentity && (kind == 1 || kind == 2) && !(ref & kRegistryA)) {
                    const auto atom = w.atoms.find(target);
                    if (atom != w.atoms.end() && atom->second.haveKey && !w.resync.freed.count(target) &&
                        atom->second.renderIdentity)
                        image.targetIdentity = atom->second.renderIdentity;
                    else if (kind == 1 && w.hudIdentity.count(target) &&
                             w.hudObjectCurrent(target, w.hudIdentity.at(target).key)) {
                        image.targetIdentity = w.hudIdentity.at(target).membership;
                        image.targetHud = true;
                    } else
                        w.pendingImageTargets[target].insert(id);
                }
                if (typ == S2C::ImageCreate && !w.earlyImageAnims.erase(id)) {
                    w.imageAnims.erase(id);
                    w.imageChains.erase(id);
                    w.imageIconStart.erase(id);
                }  // a recycled id is a new image
                stampIcon(w.imageIconStart[id], m->appid);
                w.images[id] = std::move(*m);
            }
            return;
        }
        case S2C::Hud: {
            // op 1 = remove, and its index is a LIST POSITION into hudOrder
            // (an out-of-range position is ignored); any other op adds/updates by ref (= index), appending
            // to hudOrder on first sight.
            auto m = decoded([&] { return proto::decodeHud(body); });
            if (!m) return;
            for (const auto& s : m->records) {
                uint32_t index = s.index;
                if (s.op == 1) {
                    if (index >= w.hudOrder.size()) continue;
                    w.hud.erase(w.hudOrder[index]);
                    w.hudAppid.erase(w.hudOrder[index]);
                    w.hudOrder.erase(w.hudOrder.begin() + long(index));
                    continue;
                }
                World::HudIdentity identity{s.kind, s.ver};
                const auto previous = w.hudIdentity.find(index);
                if (previous == w.hudIdentity.end() && w.hudIdentity.size() >= 65536) continue;
                if (previous != w.hudIdentity.end() &&
                    (previous->second.kind != identity.kind || previous->second.key != identity.key))
                    w.hudIconStart.erase(index);
                identity.membership = previous != w.hudIdentity.end() && w.hud.count(index) &&
                                              previous->second.kind == identity.kind &&
                                              previous->second.key == identity.key
                                          ? previous->second.membership
                                          : ++w.nextHudMembership;
                w.hudIdentity[index] = identity;
                if (!w.hud.count(index)) w.hudOrder.push_back(index);
                w.hud[index] = s;
                w.hudAppid[index] = s.appid;
                if (identity.kind == 2) w.bindPendingImages(index, identity.membership, true);
                stampIcon(w.hudIconStart[index], s.appid);
            }
            return;
        }
        case S2C::StatPanel: {
            // A frame the decoder rejects counts as Frames+1 Errors+1. The token gate and the client-side verb-tab guard need a
            // token this client owns, which only the tab select creates.
            auto m = decoded([&] { return proto::decodeStatPanel(body); });
            auto& st = w.stat;
            st.frames++;
            if (!m) {
                st.errors++;
                return;
            }
            // Token gate: token 0 = unsolicited, and releases ownership; otherwise a frame is accepted while no token is owned
            // or it matches ours.
            if (m->token == 0)
                st.owns = false;
            else if (st.owns && m->token != st.token) {
                st.stale++;
                return;
            }
            if (m->hasTabs) st.tabs = m->tabs;
            // While a client-side verb-category tab is up, the server's echoed index must not move the selection, unless the
            // same frame carries the tab list.
            bool onClientTab = st.owns && !m->hasTabs && st.selected >= int(st.tabs.size());
            if (m->hasSelected && !onClientTab) {
                if (m->hasTabs || int(m->selected) != st.selected) st.lines.clear();
                st.selected = int(m->selected);
            }
            for (auto& r : m->rows) {
                World::StatLine l{r.ordinal, net::plainText(r.name), std::move(r.value), {}};
                size_t i = l.ordinal;
                bool same = i < st.lines.size() && st.lines[i].name == l.name;
                const bool unchanged = same && st.lines[i].value == l.value;
                l.identity = unchanged ? st.lines[i].identity : ++st.nextIdentity;
                if (same) {
                    const proto::StatValue& previous = st.lines[i].value;
                    if (previous.hasAppid && previous.head == l.value.head && previous.kind == l.value.kind &&
                        previous.rowWord == l.value.rowWord)
                        l.icon = st.lines[i].icon;
                }
                if (l.value.hasAppid) stampIcon(l.icon, l.value.appid);
                if (same)
                    st.lines[i] = std::move(l);
                else if (st.lines.size() >= kMaxStatLines)
                    continue;
                else if (i < st.lines.size())
                    st.lines.insert(st.lines.begin() + long(i), std::move(l));
                else
                    st.lines.push_back(std::move(l));
            }
            for (uint16_t i : m->clears)
                if (i < st.lines.size()) st.lines.erase(st.lines.begin() + long(i));
            return;
        }
        case S2C::SkinText: {
            // Only a NON-empty dmf replaces the skin and bumps the seq.
            auto m = decoded([&] { return proto::decodeSkinText(body); });
            if (!m) return;
            if (!m->dmf.empty()) {
                w.skinDmf = std::move(m->dmf);
                w.skinSeq++;
            }
            return;
        }
        case S2C::SkinResource:
            if (auto m = decoded([&] { return proto::decodeSkinResource(body); })) w.skinResID = m->resID;
            return;
        case S2C::Winset:
            // Props are kept raw; last write per control wins.
            if (auto m = decoded([&] { return proto::decodeWinset(body); })) w.controls[m->control] = m->props;
            return;
        default: return;  // every other type is not a modelled section
    }
}

}  // namespace fugue::model

#include "model/sha256.h"
namespace fugue::model {
void Session::noteResource(uint32_t id, ByteSpan data, const std::string& name) {
    World::RecvRes r;
    r.len = data.size();
    r.sha = sha256Hex(data.data(), data.size());
    w_.received[id] = std::move(r);
    if (!name.empty()) w_.resNames[id] = name;
}
}  // namespace fugue::model
