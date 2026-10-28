#include "model/verbmenu.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#include "model/json.h"
#include "model/statpanel.h"
#include "net/textmacro.h"

namespace fugue::model {
namespace {
constexpr uint8_t kSrcKnown = kVerbSrcInView | kVerbSrcInOView | kVerbSrcRangeMod | kVerbSrcUsrContents | kVerbSrcInWorld | kVerbSrcUsrOnly;
constexpr uint8_t kHeadDefaultView = 0x7d;

constexpr uint8_t kArgView = 0x01, kArgOView = 0x02, kArgRange = 0x05, kArgORange = 0x06, kArgContents = 0x08, kArgWorld = 0x10, kArgRuntime = 0x40;
constexpr uint8_t kArgDefaultRange = 0x7d;
constexpr uint32_t kTypeMob = 0x0001, kTypeObj = 0x0002, kTypeTurf = 0x0020;

bool inRangeOf(const AtomTarget& t, int px, int py, bool havePos, int n) {
    if (!havePos || (t.x == 0 && t.y == 0)) return false;   // a HUD atom has no cell, so no range scope admits it
    return std::abs(t.x - px) <= n && std::abs(t.y - py) <= n;
}
}  // namespace

bool parseVerbDef(const std::string& json, VerbDef& e) {
    JVal v;
    try { v = parseJson(json); } catch (...) { return false; }
    if (v.kind != JVal::Obj) return false;
    e = VerbDef{};
    e.id = v["id"].u16();
    e.name = net::plainText(v["name"].bytesOrStr());
    e.desc = net::plainText(v["desc"].bytesOrStr());
    e.category = net::plainText(v["category"].bytesOrStr());
    e.head = uint8_t(v["head"].n);
    e.src = uint8_t(v["src"].n);
    e.flags = uint8_t(v["flags"].n);
    e.flagsExt = v["flagsExt"].u32();
    for (const JVal& a : v["args"].a) {
        VerbArgSpec s;
        s.typeMask = a["typeMask"].u32();
        s.filter = uint8_t(a["filter"].n);
        s.filterKind = uint8_t(a["filterKind"].n);
        e.args.push_back(s);
    }
    return true;
}

namespace {
// The `set src` clause alone (see verbmenu.h); verbAdmitsTarget adds the popup-menu flag gates.
bool srcClauseAdmits(const VerbDef& e, const AtomTarget& t, int px, int py, bool havePos, int view) {
    auto range = [&] { return e.head == kHeadDefaultView ? view : int(e.head); };
    if (e.src == kVerbSrcDefault) {
        switch (t.kind) {
            case kTargetMob: return false;                 // a /mob verb's default src IS usr: an identity clause, never a row
            case kTargetObj: return t.inContents;
            case kTargetTurf: return inRangeOf(t, px, py, havePos, 0);
        }
        return false;
    }
    if (e.src & ~kSrcKnown) return false;                  // unknown Src bit: the verb is hidden
    if (e.src & kVerbSrcInWorld) return true;
    if ((e.src & kVerbSrcUsrLoc) == kVerbSrcUsrLoc && !(e.src & kVerbSrcRangeMod)) {
        // `in usr.loc` / `= usr.loc` (Src 0x03): what stands on the player's own turf, and that turf. Not usr.contents.
        return !t.inContents && inRangeOf(t, px, py, havePos, 0);
    }
    if (e.src & (kVerbSrcInView | kVerbSrcInOView)) {
        // view / range (0x01 / 0x05) admit what the player carries (usr.contents is in view at distance 0). The o-forms
        // (0x02 / 0x06) never do. Neither excludes the player's own turf or what stands on it; oview excludes only the player.
        if (t.inContents) return !(e.src & kVerbSrcInOView);
        if ((e.src & kVerbSrcInOView) && t.isSelfMob) return false;   // oview / orange exclude usr itself
        return inRangeOf(t, px, py, havePos, range());
    }
    if (e.src & kVerbSrcUsrOnly) return false;             // identity clause
    if (e.src & kVerbSrcUsrContents) return t.inContents;
    return false;
}
}  // namespace

bool verbAdmitsTarget(const VerbDef& e, const AtomTarget& t, int px, int py, bool havePos, int view) {
    if (!t.targetable()) return false;
    uint32_t fl = e.flagWord();
    if ((fl & kVerbFlagNoPopup) || (fl & kVerbFlagHidden)) return false;
    return srcClauseAdmits(e, t, px, py, havePos, view);
}

namespace {
// The argument clause alone (see verbmenu.h); verbAdmitsAsArgument adds the popup-menu flag gates.
bool argClauseAdmits(const VerbArgSpec& a, const AtomTarget& t, int px, int py, bool havePos, int view) {
    uint32_t want = 0;
    switch (t.kind) {
        case kTargetMob: want = kTypeMob; break;
        case kTargetObj: want = kTypeObj; break;
        case kTargetTurf: want = kTypeTurf; break;
        default: return false;
    }
    if (!(a.typeMask & want)) return false;
    auto range = [&] { return a.filter == kArgDefaultRange ? view : int(a.filter); };
    switch (a.filterKind) {
        // view / range admit what the player carries (a held item has no cell, so the range test alone would refuse it).
        // The o-forms never do, and exclude usr itself, but keep usr's own turf and what stands on it.
        case kArgView: case kArgRange:
            if (t.inContents) return true;
            return inRangeOf(t, px, py, havePos, range());
        case kArgOView: case kArgORange:
            if (t.inContents || t.isSelfMob) return false;
            return inRangeOf(t, px, py, havePos, range());
        case kArgWorld: return true;
        case kArgContents: return t.inContents;
        // A runtime list (`in view(src)`, `in src.list`, `in global_list`) is not evaluated for the menu: every atom of its
        // type is offered. The pick then asks the server (c2s 0x0039, net::InputModel::atomArgument), which refuses a non-member.
        case kArgRuntime: return true;
    }
    return false;
}
}  // namespace

bool verbAdmitsAsArgument(const VerbDef& e, const AtomTarget& t, int px, int py, bool havePos, int view) {
    if (!t.targetable() || e.args.empty()) return false;
    uint32_t fl = e.flagWord();
    if ((fl & kVerbFlagNoPopup) || (fl & kVerbFlagHidden)) return false;
    return argClauseAdmits(e.args[0], t, px, py, havePos, view);
}

bool playerPos(const World& w, int& x, int& y) {
    if (w.haveSelf) {
        auto it = w.atoms.find(w.selfRef | kRegistryA);
        if (it != w.atoms.end() && it->second.hasPos) { x = it->second.x; y = it->second.y; return true; }
    }
    if (!w.haveEye) return false;
    x = w.eyeX; y = w.eyeY;
    return true;
}

int worldView(const World& w) { return w.bounds.view > 0 ? w.bounds.view : 5; }

namespace {

std::vector<uint16_t> verbIdsOf(const World& w, uint32_t appid) {
    std::vector<uint16_t> out;
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end()) return out;
    if (it->second.haveVerbs) return it->second.verbs;   // parsed once at ingest (World Appearance::verbs)
    try {
        JVal v = parseJson(it->second.fieldsJson);
        for (const JVal& x : v["verbs"].a) out.push_back(x.u16());
    } catch (...) {}
    return out;
}

// The verbs the player carries: the client appearance's verbs UNION the own mob's when knowable, else the own mob's alone.
std::vector<uint16_t> ownVerbIds(const World& w) {
    std::vector<uint16_t> own;
    // The own mob is keyed selfRef | kRegistryA, or before its 0x0007 the 0x0009 bind; the bare key is an obj ref.
    std::vector<uint32_t> mobApps;
    if (w.haveSelf) {
        if (auto m = w.atoms.find(w.selfRef | kRegistryA); m != w.atoms.end()) {
            if (w.appearances.count(m->second.appid)) mobApps.push_back(m->second.appid);
        } else if (auto rb = w.refBind.find(w.selfRef); rb != w.refBind.end() && w.appearances.count(rb->second)) {
            mobApps.push_back(rb->second);
        }
    }
    if (w.haveClientAppid && w.appearances.count(w.clientAppid) && !mobApps.empty()) {
        std::set<uint16_t> scope;
        for (uint16_t id : verbIdsOf(w, w.clientAppid)) scope.insert(id);
        for (uint32_t a : mobApps) for (uint16_t id : verbIdsOf(w, a)) scope.insert(id);
        if (!scope.empty()) return std::vector<uint16_t>(scope.begin(), scope.end());
    }
    if (!w.haveSelf) return own;
    auto m = w.atoms.find(w.selfRef | kRegistryA);
    if (m == w.atoms.end()) return own;
    return verbIdsOf(w, m->second.appid);
}

// The category a verb sorts under in the submenu: its own, or "Commands" when uncategorized. "Commands" is DM's
// client.default_verb_category default, which the 0x003b carries; a world that changes it is not modelled.
const std::string& menuCategory(const VerbDef& v) {
    static const std::string kDefault = "Commands";
    return v.category.empty() ? kDefault : v.category;
}

bool dict(const World& w, uint16_t id, VerbDef& out) {
    auto it = w.verbById.find(id);
    return it != w.verbById.end() && parseVerbDef(it->second, out);
}

// The default turf is not carried per cell: it is the unique appearance with layer == 2.0, the rule the cells section uses.
// A session keeps that set at ingest (World::layer2Appids); a hand-built World is scanned.
bool defaultTurfAppid(const World& w, uint32_t& out) {
    if (w.layer2Indexed) {
        if (w.layer2Appids.size() != 1) return false;
        out = *w.layer2Appids.begin();
        return true;
    }
    uint32_t turf = 0; int n = 0;
    for (const auto& [id, a] : w.appearances)
        if (a.layerBits == 0x40000000u) { turf = id; n++; }
    if (n != 1) return false;
    out = turf;
    return true;
}

// A client.screen atom's appearance: World::hudAppid (filled at ingest), else its 0x0076 record's own JSON.
bool hudAppidOf(const World& w, uint32_t ref, uint32_t& out) {
    if (auto it = w.hudAppid.find(ref); it != w.hudAppid.end()) { out = it->second; return true; }
    auto it = w.hud.find(ref);
    if (it == w.hud.end()) return false;
    try { out = parseJson(it->second)["appid"].u32(); return true; } catch (...) { return false; }
}

// The name an appearance shows (the "<Verb> word" listbox rows, the argument names a typed line matches).
std::string appearanceName(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end()) return std::string();
    try { return plainAtomName(parseJson(it->second.fieldsJson)["name"].bytesOrStr()); } catch (...) { return std::string(); }
}

}  // namespace

std::vector<MenuVerb> atomMenu(const World& w, uint32_t appid, AtomTarget target) {
    std::vector<MenuVerb> out;
    if (!target.targetable()) return out;
    int px = 0, py = 0;
    bool havePos = playerPos(w, px, py);
    int view = worldView(w);
    if (target.kind == kTargetMob && w.haveSelf && target.ref == w.selfRef) target.isSelfMob = true;
    std::set<uint16_t> seen;
    for (uint16_t id : verbIdsOf(w, appid)) {
        if (seen.count(id)) continue;
        VerbDef e;
        if (!dict(w, id, e)) continue;   // not defined yet: the dictionary arrives just in time
        if (!verbAdmitsTarget(e, target, px, py, havePos, view)) continue;
        seen.insert(id);
        out.push_back({e, false});
    }
    for (uint16_t id : ownVerbIds(w)) {
        if (seen.count(id)) continue;
        VerbDef e;
        if (!dict(w, id, e)) continue;
        if (!verbAdmitsAsArgument(e, target, px, py, havePos, view)) continue;
        seen.insert(id);
        out.push_back({e, true});
    }
    // Submenu order: by CATEGORY, then by NAME, both compared as raw bytes (uppercase before lowercase, `-` before letters,
    // no digit runs). The atom's own verbs and the player's argument verbs are interleaved.
    std::stable_sort(out.begin(), out.end(), [](const MenuVerb& a, const MenuVerb& b) {
        const std::string& ca = menuCategory(a.verb);
        const std::string& cb = menuCategory(b.verb);
        if (ca != cb) return ca < cb;
        return a.verb.name < b.verb.name;
    });
    return out;
}

HitTarget targetFromHit(const World& w, uint8_t hitKind, uint32_t ref, int worldX, int worldY, bool screen) {
    HitTarget h;
    if (hitKind == 1 || hitKind == 2) {
        h.target.kind = hitKind == 1 ? kTargetObj : kTargetMob;
        h.target.ref = ref;
        h.target.x = worldX; h.target.y = worldY;
        // A screen atom is in usr.contents only when the 0x000a arrays say so: being on client.screen is not enough.
        // A map hit is on a turf, never carried.
        h.target.inContents = screen && w.usrContents.count(hitKind == 2 ? (ref | kRegistryA) : ref);
        bool hudFound = false;
        if (screen) hudFound = hudAppidOf(w, ref, h.appid);   // the HUD record carries the appearance; a map movable the atom table
        if (!hudFound) {
            auto at = w.atoms.find(hitKind == 2 ? (ref | kRegistryA) : ref);
            if (at != w.atoms.end()) h.appid = at->second.appid;   // appid 0 is a REAL appearance id, so presence is tracked apart from the value
        }
        return h;
    }
    if (screen || worldX <= 0 || worldY <= 0) return h;   // a HUD hit that is no atom: nothing to target
    h.target.kind = kTargetTurf;
    h.target.x = worldX; h.target.y = worldY;
    int z = 0;
    if (w.haveSelf) {
        auto s = w.atoms.find(w.selfRef | kRegistryA);
        if (s != w.atoms.end()) z = s->second.z;
    }
    auto t = w.tiles.find({uint16_t(worldX - 1), uint16_t(worldY - 1), uint16_t(z)});
    bool found = t != w.tiles.end() && w.appearances.count(t->second);
    if (found) h.appid = t->second;
    if (!found) defaultTurfAppid(w, h.appid);
    return h;
}

namespace {
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((b - 1 - a) / b); }
// The tiles an atom at (x, y) covers: its cell, widened where bound + step leave it.
struct Footprint { int x0, y0, x1, y1; };
Footprint footprintOf(const Atom* a, int x, int y, int tile) {
    Footprint f{x, y, x, y};
    if (!a) return f;
    auto axis = [&](int c, int off, int size, int& lo, int& hi) {
        if (off >= 0 && off + size <= tile) return;
        lo = c + floorDiv(off, tile);
        hi = c + floorDiv(off + size - 1, tile);
    };
    axis(x, (a->hasBounds ? a->boundX : 0) + (a->hasStep ? a->stepX : 0), a->hasBounds ? a->boundW : tile, f.x0, f.x1);
    axis(y, (a->hasBounds ? a->boundY : 0) + (a->hasStep ? a->stepY : 0), a->hasBounds ? a->boundH : tile, f.y0, f.y1);
    return f;
}
const std::vector<uint32_t>* cellList(const std::map<std::array<int, 3>, std::vector<uint32_t>>& cells, const std::array<int, 3>& c) {
    auto it = cells.find(c);
    return it == cells.end() ? nullptr : &it->second;
}
// The Chebyshev gap between two footprints, kept to a byte as stock keeps it.
int gapOf(const Footprint& a, const Footprint& u) {
    auto axis = [](int a0, int a1, int u0, int u1) {
        int d = u0 - a1;
        if (d < 0) d = a0 - u1;
        return d < 0 ? 0 : d;
    };
    return int8_t(uint8_t(std::max(axis(a.x0, a.x1, u.x0, u.x1), axis(a.y0, a.y1, u.y0, u.y1))));
}
}  // namespace

VerbSrcRank srcRank(const World& w, VerbSrc src) {
    VerbSrcRank r;
    const int tile = iconSizeOf(w);
    const Atom* self = nullptr;
    if (w.haveSelf)
        if (auto s = w.atoms.find(w.selfRef | kRegistryA); s != w.atoms.end()) self = &s->second;
    std::optional<Footprint> usr;
    if (w.haveEye) usr = footprintOf(self, w.eyeX, w.eyeY, tile);
    const bool mob = src.kind == kTargetMob, obj = src.kind == kTargetObj;
    if (mob || obj) {
        uint32_t key = mob ? (src.ref | kRegistryA) : src.ref;
        int objs = 0, at = -1, before = 0;
        for (uint32_t k : w.usrContentsOrder) {
            bool m = k & kRegistryA;
            if (k == key) at = before;
            if (m == mob) before++;
            if (!m) objs++;
        }
        bool carried = at >= 0;
        if (carried) r.carried = mob ? objs + at : at;
        auto a = w.atoms.find(key);
        if (mob && w.haveSelf && src.ref == w.selfRef) r.dist = -1;
        else if (a != w.atoms.end() && a->second.hasPos && usr) r.dist = gapOf(footprintOf(&a->second, a->second.x, a->second.y, tile), *usr);
        else r.dist = carried ? -1 : 127;
        if (a != w.atoms.end() && a->second.hasPos) {
            // Its place in its cell's list: the cell's mobs, then its objs (World::cellMobs / cellObjs).
            std::array<int, 3> cell{a->second.x, a->second.y, a->second.z};
            const std::vector<uint32_t>* mobs = cellList(w.cellMobs, cell);
            const std::vector<uint32_t>* l = mob ? mobs : cellList(w.cellObjs, cell);
            if (l)
                if (auto at = std::find(l->rbegin(), l->rend(), src.ref); at != l->rend())
                    r.cell = int(l->rend() - at - 1) + (mob || !mobs ? 0 : int(mobs->size()));
        }
    } else if (src.kind == kTargetTurf) {
        int x = int(src.ref & 0xffff) + 1, y = int(src.ref >> 16) + 1;
        r.dist = usr ? gapOf({x, y, x, y}, *usr) : 127;
        std::array<int, 3> cell{x, y, self ? self->z : 0};
        const std::vector<uint32_t>* mobs = cellList(w.cellMobs, cell);
        const std::vector<uint32_t>* objs = cellList(w.cellObjs, cell);
        r.cell = int((mobs ? mobs->size() : 0) + (objs ? objs->size() : 0));   // after the cell's mobs and objs
    } else if (src.kind == 0x08) r.dist = -1;
    else if (src.kind == 0x04) r.dist = 0;   // usr's own area
    return r;
}

std::vector<uint16_t> srcAdmittedVerbIds(const World& w) {
    std::set<uint16_t> out;
    int px = 0, py = 0;
    bool havePos = playerPos(w, px, py);
    int view = worldView(w);
    // Each verb is parsed once per call, and each appearance's verb list resolved once: thousands of atoms share a few appearances.
    std::unordered_map<uint16_t, std::optional<VerbDef>> defs;
    std::unordered_map<uint32_t, std::vector<const VerbDef*>> carried;
    auto carries = [&](uint32_t appid) -> const std::vector<const VerbDef*>& {
        auto [it, fresh] = carried.try_emplace(appid);
        if (!fresh) return it->second;
        for (uint16_t id : verbIdsOf(w, appid)) {
            auto [d, parsed] = defs.try_emplace(id);
            if (parsed) { VerbDef e; if (dict(w, id, e)) d->second = std::move(e); }
            if (d->second) it->second.push_back(&*d->second);
        }
        return it->second;
    };
    // Popup flags are ignored here: the panel gates on hidden itself, and a no-popup verb is still listed.
    auto take = [&](uint32_t appid, const AtomTarget& t) {
        for (const VerbDef* e : carries(appid))
            if (!out.count(e->id) && srcClauseAdmits(*e, t, px, py, havePos, view)) out.insert(e->id);
    };
    for (const auto& [key, a] : w.atoms) {
        if (!a.hasPos) continue;
        bool mob = key & kRegistryA;
        uint32_t ref = key & ~kRegistryA;
        if (mob && w.haveSelf && ref == w.selfRef) continue;   // the own mob's section is the caller's
        if (carries(a.appid).empty()) continue;
        AtomTarget t;
        t.kind = mob ? kTargetMob : kTargetObj;
        t.ref = ref; t.x = a.x; t.y = a.y;
        take(a.appid, t);
    }
    for (const auto& [key, appid] : w.usrContents) {
        AtomTarget t;
        t.kind = (key & kRegistryA) ? kTargetMob : kTargetObj;
        t.ref = key & ~kRegistryA; t.inContents = true;
        take(appid, t);
    }
    for (const auto& [ref, json] : w.hud) {
        uint32_t appid = 0;
        if (!hudAppidOf(w, ref, appid)) continue;
        AtomTarget t;
        t.kind = kTargetObj; t.ref = ref;
        take(appid, t);
    }
    if (!havePos) return {out.begin(), out.end()};
    int z = 0;
    if (w.haveSelf) {
        auto s = w.atoms.find(w.selfRef | kRegistryA);
        if (s != w.atoms.end()) z = s->second.z;
    }
    // Turfs within view: a turf verb with no clause admits the player's own cell only. An unlisted cell is the default turf.
    uint32_t defaultTurf = 0;
    int haveDefault = -1;   // looked up on the first unlisted cell only
    for (int y = std::max(1, py - view); y <= py + view; y++)
        for (int x = std::max(1, px - view); x <= px + view; x++) {
            uint32_t appid = 0;
            auto t = w.tiles.find({uint16_t(x - 1), uint16_t(y - 1), uint16_t(z)});
            if (t != w.tiles.end() && w.appearances.count(t->second)) appid = t->second;
            else {
                if (haveDefault < 0) haveDefault = defaultTurfAppid(w, defaultTurf) ? 1 : 0;
                if (!haveDefault) continue;
                appid = defaultTurf;
            }
            if (carries(appid).empty()) continue;
            AtomTarget at;
            at.kind = kTargetTurf; at.x = x; at.y = y;
            take(appid, at);
        }
    // The player's own area, for a verb with no clause (an area verb with any other clause is not modelled).
    auto ac = w.areaCells.find({uint16_t(px - 1), uint16_t(py - 1), uint16_t(z)});
    if (ac != w.areaCells.end())
        if (auto b = w.turfBinds.find(ac->second); b != w.turfBinds.end())
            for (const VerbDef* e : carries(b->second))
                if (e->src == kVerbSrcDefault) out.insert(e->id);
    return {out.begin(), out.end()};
}

std::vector<VerbSrcCandidate> verbSrcCandidates(const World& w, uint16_t id, bool everyAtom) {
    std::vector<VerbSrcCandidate> out;
    // Does an appearance carry the verb? Memoised per call, since many atoms share an appearance.
    std::unordered_map<uint32_t, bool> memo;
    auto carries = [&](uint32_t appid) {
        auto m = memo.find(appid);
        if (m != memo.end()) return m->second;
        bool yes = false;
        for (uint16_t v : verbIdsOf(w, appid)) if (v == id) { yes = true; break; }
        memo[appid] = yes;
        return yes;
    };
    // A /client verb: the client appearance's own section; nothing else can carry its id.
    if (w.haveClientAppid && carries(w.clientAppid)) { out.push_back({{0x08, 0}, "client", srcRank(w, {0x08, 0})}); return out; }
    VerbDef e;
    bool haveDef = dict(w, id, e);
    int px = 0, py = 0;
    bool havePos = playerPos(w, px, py);
    int view = worldView(w);
    struct Cand { uint8_t kind; uint32_t ref; uint32_t appid; };
    std::vector<Cand> mobs, objs, turfs, areas;
    auto admits = [&](const AtomTarget& t) { return haveDef && srcClauseAdmits(e, t, px, py, havePos, view); };
    if (w.haveSelf) {
        // The own mob is keyed selfRef | kRegistryA once the 0x0007 announces it. Before that, its appearance is the 0x0009
        // bind for selfRef. The bare key is the obj ref space, never the mob.
        uint32_t appid = 0;
        bool known = false;
        auto m = w.atoms.find(w.selfRef | kRegistryA);
        if (m != w.atoms.end()) { appid = m->second.appid; known = true; }
        else if (auto rb = w.refBind.find(w.selfRef); rb != w.refBind.end()) { appid = rb->second; known = true; }
        if (known && carries(appid)) {
            // A default / `src = usr` clause names the own mob; any other clause must admit it as an atom at the
            // player's own cell (an oview() excludes it).
            AtomTarget t;
            t.kind = kTargetMob; t.ref = w.selfRef; t.x = px; t.y = py; t.isSelfMob = true;
            if (!haveDef || e.src == kVerbSrcDefault || (e.src & kVerbSrcUsrOnly) || admits(t)) mobs.push_back({kTargetMob, w.selfRef, appid});
        }
    }
    if (haveDef) {
        // Map movables: only those with a cell. An obj retracted by an A1 or the off-view sentinel keeps its record but is no src.
        for (const auto& [key, a] : w.atoms) {
            if (!a.hasPos) continue;
            bool mob = key & kRegistryA;
            uint32_t ref = key & ~kRegistryA;
            if (mob && w.haveSelf && ref == w.selfRef) continue;
            if (!carries(a.appid)) continue;
            AtomTarget t;
            t.kind = mob ? kTargetMob : kTargetObj;
            t.ref = ref; t.x = a.x; t.y = a.y;
            if (admits(t)) (mob ? mobs : objs).push_back({t.kind, ref, a.appid});
        }
        // usr.contents (World::usrContents, the 0x000a arrays): the only atoms `set src in usr` admits. The appearance is
        // the pair's own, not the atom table's: the bare key is shared with objs, so a contents mob must not be looked up there.
        for (const auto& [key, appid] : w.usrContents) {
            bool mob = key & kRegistryA;
            uint32_t ref = key & ~kRegistryA;
            if (!carries(appid)) continue;
            AtomTarget t;
            t.kind = mob ? kTargetMob : kTargetObj;
            t.ref = ref; t.inContents = true;
            if (admits(t)) (mob ? mobs : objs).push_back({t.kind, ref, appid});
        }
        // client.screen objs are listed here, not via usrContents. A carried one was already listed there; `seen` drops the repeat.
        for (const auto& [ref, json] : w.hud) {
            uint32_t appid = 0;
            if (!hudAppidOf(w, ref, appid) || !carries(appid)) continue;
            AtomTarget t;
            t.kind = kTargetObj; t.ref = ref;
            if (admits(t)) objs.push_back({kTargetObj, ref, appid});
        }
        if (havePos) {
            int z = 0;
            if (w.haveSelf) {
                auto s = w.atoms.find(w.selfRef | kRegistryA);
                if (s != w.atoms.end()) z = s->second.z;
            }
            // Turfs in view: a turf verb's src is the cell, wire (x-1) | (y-1) << 16. Scanned y-major, x ascending: ascending turf id.
            uint32_t defaultTurf = 0;
            int haveDefault = -1;   // looked up on the first unlisted cell only
            for (int y = py - view; y <= py + view; y++)
                for (int x = px - view; x <= px + view; x++) {
                    if (x < 1 || y < 1) continue;
                    uint32_t appid = 0;
                    auto t = w.tiles.find({uint16_t(x - 1), uint16_t(y - 1), uint16_t(z)});
                    if (t != w.tiles.end() && w.appearances.count(t->second)) appid = t->second;
                    else {
                        if (haveDefault < 0) haveDefault = defaultTurfAppid(w, defaultTurf) ? 1 : 0;
                        if (!haveDefault) continue;
                        appid = defaultTurf;   // an unlisted cell: targetFromHit's default-turf rule
                    }
                    if (!carries(appid)) continue;
                    AtomTarget at;
                    at.kind = kTargetTurf; at.x = x; at.y = y;
                    if (admits(at)) turfs.push_back({kTargetTurf, uint32_t(x - 1) | uint32_t(y - 1) << 16, appid});
                }
            // The player's own area (an area verb's default src): [04][the 0x0020 index of the player's cell]. That index is the
            // area's ordinal (\ref [0x040000NN]), not a position in the bind list. An area verb with any other clause is not modelled.
            auto ac = w.areaCells.find({uint16_t(px - 1), uint16_t(py - 1), uint16_t(z)});
            if (ac != w.areaCells.end() && e.src == kVerbSrcDefault) {
                auto b = w.turfBinds.find(ac->second);
                if (b != w.turfBinds.end() && carries(b->second)) areas.push_back({0x04, ac->second, b->second});
            }
        }
    }
    // Order: mobs before objs, each by ascending ref, carried and floor objs interleaved, and the own mob in ref order.
    // Turfs and areas follow.
    auto byRef = [](const Cand& a, const Cand& b) { return a.ref < b.ref; };
    std::stable_sort(mobs.begin(), mobs.end(), byRef);
    std::stable_sort(objs.begin(), objs.end(), byRef);
    std::set<std::string> names;
    std::set<std::pair<uint8_t, uint32_t>> seen;   // one atom listed twice (a carried obj also on client.screen) is one candidate
    for (const auto* list : {&mobs, &objs, &turfs, &areas})
        for (const Cand& c : *list) {
            if (!seen.insert({c.kind, c.ref}).second) continue;
            // One entry per NAME, the first one kept. Mobs are listed first, so a mob and an obj of one name collapse to the mob.
            std::string n = appearanceName(w, c.appid);
            if (!names.insert(n).second && !everyAtom) continue;
            out.push_back({{c.kind, c.ref}, n, srcRank(w, {c.kind, c.ref})});
        }
    return out;
}

namespace {
// `as anything` (no list) and `as text|anything` are typed as an ATOM name: mob, obj or turf in the argument's scope
// (default view). An area is not offered.
VerbArgSpec argAsAtoms(VerbArgSpec a) {
    constexpr uint32_t kText = 0x04, kAnything = 0x1000;
    if (a.typeMask & kAnything) a.typeMask = (a.typeMask & ~(kAnything | kText)) | kTypeMob | kTypeObj | kTypeTurf;
    return a;
}
}  // namespace

bool argResolvesLocally(const VerbArgSpec& a0) {
    // An atom argument (some mob / obj / turf bit, nothing that is not an atom or null) under a clause DreamSeeker
    // evaluates itself. `in world` and runtime lists go to the server instead (c2s 0x002d, 0x0039).
    constexpr uint32_t kAtoms = kTypeMob | kTypeObj | kTypeTurf, kNull = 0x80;
    VerbArgSpec a = argAsAtoms(a0);
    if (!(a.typeMask & kAtoms) || (a.typeMask & ~(kAtoms | kNull))) return false;
    switch (a.filterKind) {
        case kArgView: case kArgOView: case kArgRange: case kArgORange: case kArgContents: return true;
    }
    return false;
}

std::vector<VerbSrcCandidate> verbArgCandidates(const World& w, const VerbArgSpec& a0) {
    std::vector<VerbSrcCandidate> out;
    if (!argResolvesLocally(a0)) return out;
    VerbArgSpec a = argAsAtoms(a0);
    int px = 0, py = 0;
    bool havePos = playerPos(w, px, py);
    int view = worldView(w);
    struct Cand { uint8_t kind; uint32_t ref; uint32_t appid; };
    std::vector<Cand> mobs, objs, turfs;
    auto admits = [&](const AtomTarget& t) { return argClauseAdmits(a, t, px, py, havePos, view); };
    std::set<uint32_t> carried;   // atoms keys listed from usr.contents: their map record (if any) is not listed again
    for (const auto& [key, appid] : w.usrContents) {
        AtomTarget t;
        t.kind = (key & kRegistryA) ? kTargetMob : kTargetObj;
        t.ref = key & ~kRegistryA; t.inContents = true;
        if (admits(t)) (t.kind == kTargetMob ? mobs : objs).push_back({t.kind, t.ref, appid});
        carried.insert(key);
    }
    if (w.haveSelf) {
        uint32_t appid = 0;
        bool known = false;
        if (auto m = w.atoms.find(w.selfRef | kRegistryA); m != w.atoms.end()) { appid = m->second.appid; known = true; }
        else if (auto rb = w.refBind.find(w.selfRef); rb != w.refBind.end()) { appid = rb->second; known = true; }
        AtomTarget t;
        t.kind = kTargetMob; t.ref = w.selfRef; t.x = px; t.y = py; t.isSelfMob = true;
        if (known && havePos && admits(t)) mobs.push_back({kTargetMob, w.selfRef, appid});
    }
    for (const auto& [key, at] : w.atoms) {
        if (!at.hasPos || carried.count(key)) continue;
        bool mob = key & kRegistryA;
        uint32_t ref = key & ~kRegistryA;
        if (mob && w.haveSelf && ref == w.selfRef) continue;
        AtomTarget t;
        t.kind = mob ? kTargetMob : kTargetObj;
        t.ref = ref; t.x = at.x; t.y = at.y;
        if (admits(t)) (mob ? mobs : objs).push_back({t.kind, ref, at.appid});
    }
    if (havePos && (a.typeMask & kTypeTurf)) {
        int z = 0;
        if (w.haveSelf)
            if (auto s = w.atoms.find(w.selfRef | kRegistryA); s != w.atoms.end()) z = s->second.z;
        int r = std::min(a.filter == kArgDefaultRange ? view : int(a.filter), view);   // DreamSeeker knows no cell past its view
        uint32_t defaultTurf = 0;
        int haveDefault = -1;
        // usr's own turf first, then the rest by ascending turf id. A typed name shared by several floors resolves to the cell under usr.
        std::vector<std::pair<int, int>> cells{{px, py}};
        for (int y = py - r; y <= py + r; y++)
            for (int x = px - r; x <= px + r; x++)
                if (x != px || y != py) cells.push_back({x, y});
        for (const auto& [x, y] : cells) {
            if (x < 1 || y < 1) continue;
            uint32_t appid = 0;
            auto t = w.tiles.find({uint16_t(x - 1), uint16_t(y - 1), uint16_t(z)});
            if (t != w.tiles.end() && w.appearances.count(t->second)) appid = t->second;
            else {
                if (haveDefault < 0) haveDefault = defaultTurfAppid(w, defaultTurf) ? 1 : 0;
                if (!haveDefault) continue;
                appid = defaultTurf;
            }
            AtomTarget at;
            at.kind = kTargetTurf; at.x = x; at.y = y;
            if (admits(at)) turfs.push_back({kTargetTurf, uint32_t(x - 1) | uint32_t(y - 1) << 16, appid});
        }
    }
    // Order: mobs before objs and turfs last, objs by ascending ref with carried ones interleaved.
    auto byRef = [](const Cand& x, const Cand& y) { return x.ref < y.ref; };
    std::stable_sort(mobs.begin(), mobs.end(), byRef);
    std::stable_sort(objs.begin(), objs.end(), byRef);
    for (const auto* list : {&mobs, &objs, &turfs})
        for (const Cand& c : *list) out.push_back({{c.kind, c.ref}, appearanceName(w, c.appid), srcRank(w, {c.kind, c.ref})});
    return out;
}

uint32_t dmRefOf(const World& w, const AtomTarget& t) {
    // DM \ref numbers: obj 0x02000000 | ref, mob 0x03000000 | ref, turf 0x01000000 | (x-1) + (y-1) * maxx, plus maxx * maxy
    // per z level.
    switch (t.kind) {
        case kTargetObj: return 0x02000000u | t.ref;
        case kTargetMob: return 0x03000000u | t.ref;
        case kTargetTurf: {
            int z = 0;
            if (w.haveSelf)
                if (auto s = w.atoms.find(w.selfRef | kRegistryA); s != w.atoms.end()) z = s->second.z;
            uint32_t mx = uint32_t(std::max(0, w.bounds.maxX)), my = uint32_t(std::max(0, w.bounds.maxY));
            return 0x01000000u | (uint32_t(t.x - 1) + uint32_t(t.y - 1) * mx + uint32_t(z) * mx * my);
        }
    }
    return 0;
}

bool ownVerbSrc(const World& w, uint16_t id, VerbSrc& out) {
    auto carries = [&](uint32_t appid) {
        for (uint16_t v : verbIdsOf(w, appid)) if (v == id) return true;
        return false;
    };
    if (w.haveClientAppid && carries(w.clientAppid)) { out = {0x08, 0}; return true; }
    if (!w.haveSelf) return false;
    auto m = w.atoms.find(w.selfRef | kRegistryA);
    auto rb = w.refBind.find(w.selfRef);
    uint32_t appid = 0;
    if (m != w.atoms.end()) appid = m->second.appid;
    else if (rb != w.refBind.end()) appid = rb->second;
    else return false;
    if (!carries(appid)) return false;
    out = {kTargetMob, w.selfRef};
    return true;
}

}  // namespace fugue::model
