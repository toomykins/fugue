#include "skin/usermacros.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "skin/dmf.h"

namespace fugue::skin {

namespace {
std::string quote(const std::string& s) {  // the inverse of dmf.cpp scanValue's escapes
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\t': o += "\\t"; break;
            case '\n': o += "\\n"; break;
            default: o.push_back(c);
        }
    }
    return o + "\"";
}
bool blank(const std::string& s) { return trimSpace(s).empty(); }
}  // namespace

MacroSet parseUserMacros(std::string_view text, std::string* err) {
    ParseResult r = parseDmf(text);
    if (!r.ok) {
        if (err) *err = "line " + std::to_string(r.errLine) + ": " + r.error;
        return {};
    }
    for (const Block& b : r.file.blocks)
        if (b.kind == "macro") return macroSetFromBlock(b);
    return {};
}

std::string formatUserMacros(const MacroSet& s) {
    std::vector<const MacroEntry*> es;
    for (const MacroEntry& e : s.entries) es.push_back(&e);
    std::stable_sort(es.begin(), es.end(), [](const MacroEntry* a, const MacroEntry* b) { return a->spec < b->spec; });
    std::string o = "macro \n";
    for (const MacroEntry* e : es) {
        o += "\telem " + (e->id.empty() ? std::string() : quote(e->id)) + "\n";
        o += "\t\tname = " + quote(e->spec) + "\n";
        o += "\t\tcommand = " + quote(e->command) + "\n";
    }
    return o + "\n";
}

bool editUserMacro(MacroSet& s, const std::string& oldSpec, const std::string& spec, const std::string& command,
                   const std::string& id) {
    MacroKey k;
    bool rep = false;
    bool add = !blank(spec) && !blank(command);
    if (!blank(spec) && !parseMacroName(trimSpace(spec), k, rep)) return false;
    auto drop = [&](const std::string& sp) {
        MacroKey dk;
        bool drep = false;
        if (blank(sp) || !parseMacroName(trimSpace(sp), dk, drep)) return;
        s.entries.erase(std::remove_if(s.entries.begin(), s.entries.end(),
                                       [&](const MacroEntry& e) { return e.key == dk && e.rep == drep; }),
                        s.entries.end());
    };
    drop(oldSpec);
    if (add) {
        drop(spec);  // re-binding an existing key replaces it
        MacroEntry e;
        e.id = trimSpace(id);
        e.spec = trimSpace(spec);
        e.command = trimSpace(command);
        e.key = k;
        e.rep = rep;
        s.entries.push_back(e);
    }
    s.reindex();
    return true;
}

template <class Pick>
static const MacroEntry* layered(const MacroSet* skin, const MacroSet& game, const MacroSet& global, Pick pick) {
    if (skin)
        if (const MacroEntry* e = pick(*skin)) return e->disabled ? nullptr : e;  // in use by the skin
    for (const MacroSet* s : {&game, &global})
        if (const MacroEntry* e = pick(*s); e && !e->disabled) return e;
    return nullptr;
}

const MacroEntry* lookupLayered(const MacroSet* skin, const MacroSet& game, const MacroSet& global, const MacroKey& k,
                                bool repeat) {
    return lookupHeld({skin, &game, &global}, k, repeat);
}

HeldMatch matchLayered(const MacroSet* skin, const MacroSet& game, const MacroSet& global, const MacroKey& k) {
    return matchHeld({skin, &game, &global}, k);
}

const MacroEntry* lookupLayeredAny(const MacroSet* skin, const MacroSet& game, const MacroSet& global,
                                   const MacroKey& k, bool repeat) {
    return layered(skin, game, global, [&](const MacroSet& s) { return s.findAny(k, repeat); });
}

std::string userMacrosPath(const std::string& stateDir, const std::string& worldKey) {
    std::filesystem::path p(stateDir);
    if (!worldKey.empty()) p = p / "macros" / worldKey;
    return (p / "macros.dmf").generic_string();
}

std::string worldKeyFor(const std::string& host, int port) {
    std::string k = host + "_" + std::to_string(port);
    for (char& c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
              c == '_'))
            c = '_';
    return k;
}

MacroSet loadUserMacros(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return parseUserMacros(ss.str());
}

bool saveUserMacros(const std::string& path, const MacroSet& s, std::string* err) {
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp;
            return false;
        }
        f << formatUserMacros(s);
        if (!f) {
            if (err) *err = "write failed: " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }
    return true;
}

}  // namespace fugue::skin
