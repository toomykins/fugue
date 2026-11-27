#include "model/statpanel.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>
#include "net/textmacro.h"

namespace fugue::model {

StatValue statValueOf(const proto::StatValue& v) {
    StatValue out;
    out.kind = v.kind;
    out.text = net::plainText(v.text);
    out.num = v.num;
    out.appid = v.appid;
    out.head = v.head;
    out.hasAppid = v.hasAppid;
    return out;
}

std::string plainAtomName(const std::string& s) { return net::plainText(s); }

// DreamSeeker prints a stat number as %g: 6 significant digits, exponent form past them (1.23457e+06, 1e-07).
static std::string trimNum(double f) {
    char b[64];
    auto r = std::to_chars(b, b + sizeof b, f, std::chars_format::general, 6);  // %.6g, locale-independent
    std::string s(b, r.ptr);
    if (s == "-0") s = "0";
    return s;
}

std::string statText(const World& w, const World::StatLine& l) {
    StatValue v = statValueOf(l.value);
    if (v.isAtom()) {
        auto it = w.appearances.find(v.appid);
        return it == w.appearances.end() ? "" : plainAtomName(it->second.fields.name);
    }
    if (v.kind == 0x06) return v.text;
    if (v.kind == 0x2a) return trimNum(v.num);
    return "";
}

namespace {
std::vector<uint32_t> verbIdsOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    return it == w.appearances.end() ? std::vector<uint32_t>{} : it->second.fields.verbs;
}

bool inPanel(const VerbDef& e) {
    if (e.name.empty()) return false;
    uint32_t fl = e.flagWord();
    return !(fl & kVerbFlagHidden) && !(fl & kVerbFlagNullCategory);
}
std::string categoryLabel(const VerbDef& e) {
    if (!inPanel(e)) return "";
    return e.category.empty() ? std::string(kDefaultVerbCategory) : e.category;
}
}  // namespace

bool verbScope(const World& w, std::vector<uint32_t>& ids) {
    ids.clear();
    if (!w.haveClientAppid || !w.appearances.count(w.clientAppid)) return false;
    if (!w.haveSelf) return false;
    // The own mob is keyed selfRef | kRegistryA once the WorldState announces it; before that its appearance is its
    // MobBind. The bare key is the obj ref space: an obj that shares the mob's number is not the mob.
    uint32_t mobApp = 0;
    bool known = false;
    if (auto m = w.atoms.find(w.selfRef | kRegistryA); m != w.atoms.end()) {
        mobApp = m->second.appid;
        known = true;
    } else if (auto rb = w.refBind.find(w.selfRef); rb != w.refBind.end()) {
        mobApp = rb->second;
        known = true;
    }
    if (!known || !w.appearances.count(mobApp)) return false;
    std::set<uint32_t> s;
    for (uint32_t id : verbIdsOf(w, w.clientAppid)) s.insert(id);
    for (uint32_t id : verbIdsOf(w, mobApp)) s.insert(id);
    for (uint32_t id : srcAdmittedVerbIds(w)) s.insert(id);
    if (s.empty()) return false;  // no verbs is indistinguishable from a server that sends no verb sections
    ids.assign(s.begin(), s.end());
    return true;
}

VerbPanel verbPanel(const World& w) {
    std::vector<uint32_t> scope;
    bool scoped = verbScope(w, scope);
    std::set<uint32_t> in(scope.begin(), scope.end());
    VerbPanel p;
    for (const auto& [id, verb] : w.verbById) {
        if (scoped && !in.count(id)) continue;
        VerbDef e = verbDefOf(verb);
        std::string l = categoryLabel(e);
        if (!l.empty()) p.byCategory[l].push_back(std::move(e));
    }
    for (auto& [label, verbs] : p.byCategory) {
        std::sort(verbs.begin(), verbs.end(),
                  [](const VerbDef& a, const VerbDef& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
        p.categories.push_back(label);  // byte order, not case-folded
    }
    return p;
}

std::vector<std::string> verbCategories(const World& w) { return verbPanel(w).categories; }

std::vector<VerbDef> verbsInCategory(const World& w, const std::string& label) {
    VerbPanel p = verbPanel(w);
    auto it = p.byCategory.find(label);
    return it == p.byCategory.end() ? std::vector<VerbDef>{} : std::move(it->second);
}

std::vector<std::string> statTabs(const World& w, const VerbPanel& p) {
    std::vector<std::string> t;
    for (const std::string& n : w.stat.tabs) t.push_back(net::plainText(n));
    t.insert(t.end(), p.categories.begin(), p.categories.end());
    return t;
}
std::vector<std::string> statTabs(const World& w) { return statTabs(w, verbPanel(w)); }

bool verbTabCategory(const World& w, const VerbPanel& p, int index, std::string& label) {
    int n = int(w.stat.tabs.size());
    if (index < n) return false;
    if (index - n >= int(p.categories.size())) return false;
    label = p.categories[size_t(index - n)];
    return true;
}
bool verbTabCategory(const World& w, int index, std::string& label) {
    if (index < int(w.stat.tabs.size())) return false;  // a Stat() tab needs no scope
    return verbTabCategory(w, verbPanel(w), index, label);
}

std::string statTabWireName(const World& w, int index) {
    if (index >= 0 && index < int(w.stat.tabs.size())) return w.stat.tabs[size_t(index)];
    std::string l;
    if (index >= 0 && verbTabCategory(w, index, l)) return kVerbTabWireName;
    return "";
}

bool statRowRef(const World& w, const StatValue& v, proto::enc::AtomRef& out) {
    if (!v.hasAppid) return false;
    switch (v.kind) {
        case 0x01:
            // Stock DreamSeeker names a turf row by its id, [07][head u32], not by the map's [03][x][y]. Bounds are required
            // so a row is clickable only once the world is known.
            if (w.bounds.maxX <= 0 || w.bounds.maxY <= 0) return false;
            out = proto::enc::AtomRef{proto::enc::RefTurfId, v.head, 0, 0};
            return true;
        case 0x02: out = proto::enc::objRef(v.head); return true;
        case 0x03: out = proto::enc::mobRef(v.head); return true;
        default: return false;
    }
}

Bytes statRowClickBody(const World& w, const proto::enc::AtomRef& ref, const StatRowClick& c) {
    // Stock DreamSeeker 516 sends Click(location, control, params) with location = the tab the row is on, control =
    // "infowindow.info" and params "left=1;button=left", for an obj, a turf or a mob alike. The map click's shape
    // (mapwindow.map, a screen-loc, the atom's turf as loc) is not used here.
    return proto::enc::encodeInfoClick(c.sub, ref, statTabWireName(w, w.stat.selected), c.control,
                                       proto::enc::infoClickParams(c.button, c.mods, c.icon, c.iconX, c.iconY));
}

Bytes encodeStatTabBody(uint16_t index, uint8_t token, const std::string& name) {
    Bytes b{uint8_t(index), uint8_t(index >> 8), token};
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(0);
    return b;
}

bool selectStatTab(World& w, int index, StatTabSelect& out) {
    std::string name = statTabWireName(w, index);
    if (name.empty()) return false;
    auto& st = w.stat;
    st.token++;
    if (st.token == 0) st.token = 1;  // 0 is the server's "unsolicited/reset" value and must never be ours
    st.owns = true;
    st.selected = index;
    st.lines.clear();
    out.index = uint16_t(index);
    out.token = st.token;
    out.name = std::move(name);
    return true;
}

}  // namespace fugue::model
