#include "skin/macro.h"

#include <cctype>
#include <cstdlib>
#include <tuple>

#include "skin/layout.h"

namespace fugue::skin {

bool MacroKey::operator<(const MacroKey& o) const {
    return std::tie(key, ctrl, shift, alt, up) < std::tie(o.key, o.ctrl, o.shift, o.alt, o.up);
}

std::string MacroKey::toString() const {
    std::string s;
    if (ctrl) s += "CTRL+";
    if (shift) s += "SHIFT+";
    if (alt) s += "ALT+";
    s += key;
    if (up) s += "+UP";
    return s;
}

static std::string canonKeyName(const std::string& t) {
    if (t == "ENTER") return "RETURN";
    if (t == "ESC") return "ESCAPE";
    if (t == "BACKSPACE" || t == "BKSP") return "BACK";
    if (t == "INS") return "INSERT";
    if (t == "DEL") return "DELETE";
    return t;
}

bool parseMacroName(const std::string& spec, MacroKey& k, bool& rep) {
    k = MacroKey{};
    rep = false;
    std::string key;
    std::vector<std::string> mods;
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t pl = spec.find_first_of("+ ", pos);
        std::string tok = spec.substr(pos, (pl == std::string::npos ? spec.size() : pl) - pos);
        pos = pl == std::string::npos ? spec.size() + 1 : pl + 1;
        std::string t = upperAscii(trimSpace(tok));
        if (t.empty()) continue;
        if (t == "CTRL" || t == "CONTROL") {
            mods.push_back("CTRL");
            k.ctrl = true;
        } else if (t == "SHIFT") {
            mods.push_back("SHIFT");
            k.shift = true;
        } else if (t == "ALT") {
            mods.push_back("ALT");
            k.alt = true;
        } else if (t == "REP")
            rep = true;
        else if (t == "UP")
            k.up = true;
        else if (key.empty())
            key = canonKeyName(t);  // a second key token is dropped
    }
    if (key.empty()) {
        if (mods.empty()) {
            k = MacroKey{};
            return false;
        }
        key = mods.back();  // all-modifier spec: the LAST one is the key
        if (key == "CTRL")
            k.ctrl = false;
        else if (key == "SHIFT")
            k.shift = false;
        else if (key == "ALT")
            k.alt = false;
    }
    k.key = key;
    return true;
}

const MacroEntry* MacroSet::lookup(const MacroKey& k, bool repeat) const {
    const MacroEntry* e = find(k, repeat);
    return e && !e->disabled ? e : nullptr;  // a disabled binding still CLAIMS the key
}

const MacroEntry* MacroSet::find(const MacroKey& k, bool repeat) const {
    for (bool rep : {false, true}) {
        if (repeat && !rep) continue;
        auto it = byKey.find({k, rep});
        if (it != byKey.end()) return &entries[it->second];
    }
    return nullptr;
}

const MacroEntry* MacroSet::findAny(const MacroKey& k, bool repeat) const {
    if (repeat) return nullptr;
    MacroKey any = k;
    any.key = "ANY";
    if (k.up) {
        any.ctrl = any.shift = any.alt = false;
        return find(any, false);
    }
    for (const MacroKey& c : heldCombos(any))
        if (const MacroEntry* e = find(c, false)) return e;
    return nullptr;
}

std::vector<MacroKey> heldCombos(const MacroKey& k) {
    std::vector<MacroKey> out;
    const int held = (k.alt ? 4 : 0) | (k.ctrl ? 2 : 0) | (k.shift ? 1 : 0);
    for (int m : {7, 6, 5, 3, 4, 2, 1, 0}) {  // most modifiers first, then Alt (4) over Ctrl (2) over Shift (1)
        if ((m & held) != m) continue;
        MacroKey c = k;
        c.alt = m & 4;
        c.ctrl = m & 2;
        c.shift = m & 1;
        out.push_back(c);
    }
    return out;
}

// A combination's binding in the first of `layers` that binds it, or null when that one is disabled.
static const MacroEntry* layerBinding(std::initializer_list<const MacroSet*> layers, const MacroKey& c, bool rep) {
    for (const MacroSet* s : layers) {
        if (!s) continue;
        auto it = s->byKey.find({c, rep});
        if (it != s->byKey.end()) return s->entries[it->second].disabled ? nullptr : &s->entries[it->second];
    }
    return nullptr;
}

HeldMatch matchHeld(std::initializer_list<const MacroSet*> layers, const MacroKey& k) {
    for (const MacroKey& c : heldCombos(k)) {
        HeldMatch m{layerBinding(layers, c, false), layerBinding(layers, c, true)};
        if (m.press || m.rep) return m;
    }
    return {};
}

const MacroEntry* lookupHeld(std::initializer_list<const MacroSet*> layers, const MacroKey& k, bool repeat) {
    if (!repeat) {
        HeldMatch m = matchHeld(layers, k);
        return m.press ? m.press : m.rep;
    }
    for (const MacroKey& c : heldCombos(k))
        if (const MacroEntry* e = layerBinding(layers, c, true)) return e;
    return nullptr;
}

// The Windows virtual-key code of a canonical key name. The modifier keys, then Any, sort after every other key.
static int keyCode(const std::string& k) {
    static const std::map<std::string, int> named = {
        {"BACK", 0x08},      {"TAB", 0x09},       {"CENTER", 0x0c},   {"RETURN", 0x0d},    {"PAUSE", 0x13},
        {"CAPSLOCK", 0x14},  {"ESCAPE", 0x1b},    {"SPACE", 0x20},    {"NORTHEAST", 0x21}, {"SOUTHEAST", 0x22},
        {"SOUTHWEST", 0x23}, {"NORTHWEST", 0x24}, {"WEST", 0x25},     {"NORTH", 0x26},     {"EAST", 0x27},
        {"SOUTH", 0x28},     {"INSERT", 0x2d},    {"DELETE", 0x2e},   {"APPS", 0x5d},      {"MULTIPLY", 0x6a},
        {"ADD", 0x6b},       {"SEPARATOR", 0x6c}, {"SUBTRACT", 0x6d}, {"DECIMAL", 0x6e},   {"DIVIDE", 0x6f},
        {"NUMLOCK", 0x90},   {"SCROLL", 0x91},    {";", 0xba},        {"=", 0xbb},         {",", 0xbc},
        {"-", 0xbd},         {".", 0xbe},         {"/", 0xbf},        {"`", 0xc0},         {"[", 0xdb},
        {"\\", 0xdc},        {"]", 0xdd},         {"'", 0xde},        {"ALT", 0x100},      {"CTRL", 0x100},
        {"SHIFT", 0x100},    {"ANY", 0x101}};
    if (auto it = named.find(k); it != named.end()) return it->second;
    if (k.size() == 1 && std::isalnum((unsigned char)k[0])) return (unsigned char)k[0];
    if (k.size() == 7 && k.rfind("NUMPAD", 0) == 0 && std::isdigit((unsigned char)k[6])) return 0x60 + (k[6] - '0');
    if (k.size() >= 2 && k[0] == 'F' && std::isdigit((unsigned char)k[1])) return 0x6f + std::atoi(k.c_str() + 1);
    return 0xff;
}

static int listOrder(const MacroEntry& e) {
    const MacroKey& k = e.key;
    bool alt = k.alt || k.key == "ALT", ctrl = k.ctrl || k.key == "CTRL", shift = k.shift || k.key == "SHIFT";
    return keyCode(k.key) << 5 | (alt ? 1 : 0) | (ctrl ? 2 : 0) | (shift ? 4 : 0) | (k.up ? 8 : 0) | (e.rep ? 16 : 0);
}

bool listedBefore(const MacroEntry& a, const MacroEntry& b) { return listOrder(a) < listOrder(b); }

std::string macroSpecName(const MacroKey& k, bool rep) {
    std::string s =
        std::string(k.ctrl ? "CTRL+" : "") + (k.shift ? "SHIFT+" : "") + (k.alt ? "ALT+" : "") + anyKeyName(k.key);
    return s + (rep ? "+REP" : "") + (k.up ? "+UP" : "");
}

void MacroSet::reindex() {
    byKey.clear();
    for (size_t i = 0; i < entries.size(); i++)
        if (!entries[i].key.key.empty()) byKey[{entries[i].key, entries[i].rep}] = i;
}

std::string anyKeyName(const std::string& key) {
    std::string s = key;
    for (size_t i = 1; i < s.size(); i++) s[i] = char(std::tolower((unsigned char)s[i]));
    return s;
}

MacroSet macroSetFromBlock(const Block& b) {
    MacroSet set;
    set.name = b.name;
    for (const Elem& e : b.elems) {
        std::string spec = e.str("name", e.name);
        MacroKey k;
        bool rep;
        if (!parseMacroName(spec, k, rep)) continue;
        MacroEntry ent;
        ent.id = e.name;
        ent.spec = spec;
        ent.command = e.str("command", "");
        ent.key = k;
        ent.rep = rep;
        ent.disabled = e.boolv("is-disabled", false);
        auto lk = std::make_pair(k, rep);
        auto prev = set.byKey.find(lk);
        if (prev != set.byKey.end()) {
            set.entries[prev->second] = ent;
            continue;
        }  // later wins, in place
        set.byKey[lk] = set.entries.size();
        set.entries.push_back(ent);
    }
    return set;
}

MacroTable resolveMacros(const File& f) {
    MacroTable T;
    for (const Block& b : f.blocks) {
        if (b.kind != "macro") continue;
        T.sets[b.name] = macroSetFromBlock(b);
    }
    if (const Block* mw = findMainWindow(f))
        if (const Elem* m = mw->main()) T.active = m->str("macro", "");
    if (!T.active.empty() && !T.sets.count(T.active)) T.active.clear();
    if (T.active.empty() && T.sets.size() == 1) T.active = T.sets.begin()->first;
    return T;
}

}  // namespace fugue::skin
