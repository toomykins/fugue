#include "pager/listing.h"
#include <cstring>
#include "pager/text.h"

namespace fugue::pager {

// ---- HTML entity unescaping (subset of the named table) ----
namespace {
struct Ent {
    const char* name;
    char32_t r;
    bool legacy;
};  // legacy: also accepted without the ';'
const Ent kEnts[] = {
    {"amp", '&', true},       {"lt", '<', true},        {"gt", '>', true},         {"quot", '"', true},
    {"nbsp", 0xA0, true},     {"copy", 0xA9, true},     {"reg", 0xAE, true},       {"apos", '\'', false},
    {"mdash", 0x2014, false}, {"ndash", 0x2013, false}, {"hellip", 0x2026, false}, {"laquo", 0xAB, true},
    {"raquo", 0xBB, true},    {"middot", 0xB7, true},   {"bull", 0x2022, false},   {"trade", 0x2122, false},
    {"lsquo", 0x2018, false}, {"rsquo", 0x2019, false}, {"ldquo", 0x201C, false},  {"rdquo", 0x201D, false},
    {"times", 0xD7, true},    {"eacute", 0xE9, true},   {"egrave", 0xE8, true},    {"auml", 0xE4, true},
    {"ouml", 0xF6, true},     {"uuml", 0xFC, true},     {"szlig", 0xDF, true},     {"deg", 0xB0, true},
    {"euro", 0x20AC, false},  {"larr", 0x2190, false},  {"rarr", 0x2192, false},
};
const char32_t k1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                            0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                            0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
bool isAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
}  // namespace

std::string htmlUnescape(std::string_view s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            o.push_back(s[i++]);
            continue;
        }
        size_t j = i + 1;
        if (j >= s.size()) {
            o.push_back('&');
            i++;
            continue;
        }
        if (s[j] == '#') {
            j++;
            bool hex = false;
            if (j < s.size() && (s[j] == 'x' || s[j] == 'X')) {
                hex = true;
                j++;
            }
            size_t d0 = j;
            unsigned long long v = 0;
            bool over = false;
            while (j < s.size()) {
                char c = s[j];
                int dv = (c >= '0' && c <= '9')        ? c - '0'
                         : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                       : -1;
                if (dv < 0) break;
                if (!over) {
                    v = v * (hex ? 16 : 10) + (unsigned)dv;
                    if (v > 0x10FFFF) over = true;
                }
                j++;
            }
            if (j == d0) {
                // No digits. "&#x;" is an entity (U+FFFD, 4 bytes consumed); "&#;" and "&#xZ" stay literal.
                if (hex && j < s.size() && s[j] == ';') {
                    appendRune(o, 0xFFFD);
                    i = j + 1;
                    continue;
                }
                o.push_back('&');
                i++;
                continue;
            }
            if (j < s.size() && s[j] == ';') j++;
            char32_t r;
            if (over)
                r = 0xFFFD;
            else if (v >= 0x80 && v <= 0x9F)
                r = k1252[v - 0x80];
            else if (v == 0 || (v >= 0xD800 && v <= 0xDFFF))
                r = 0xFFFD;
            else
                r = char32_t(v);
            appendRune(o, r);
            i = j;
            continue;
        }
        size_t n0 = j;
        while (j < s.size() && isAlnum(s[j])) j++;
        std::string_view name = s.substr(n0, j - n0);
        bool semi = j < s.size() && s[j] == ';';
        const Ent* hit = nullptr;
        size_t used = 0;
        if (semi) {
            for (auto& e : kEnts)
                if (name == e.name) {
                    hit = &e;
                    used = name.size() + 1;
                }
        }
        if (!hit) {  // longest legacy prefix without ';'
            for (auto& e : kEnts)
                if (e.legacy && name.substr(0, strlen(e.name)) == e.name && strlen(e.name) > used) {
                    hit = &e;
                    used = strlen(e.name);
                }
        }
        if (hit) {
            appendRune(o, hit->r);
            i = n0 + used;
        } else {
            o.push_back('&');
            i++;
        }
    }
    return o;
}

// ---- DM literals ----
std::string dmString(std::string_view s, std::string_view* rest) {
    if (s.empty() || s[0] != '"') {
        if (rest) *rest = s;
        return "";
    }
    std::string b;
    for (size_t i = 1; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') {
            if (rest) *rest = s.substr(i + 1);
            return b;
        }
        if (c == '\\') {
            if (i + 1 < s.size()) {
                i++;
                b.push_back(s[i] == 'n' ? '\n' : s[i]);
            }
        } else
            b.push_back(c);
    }
    if (rest) *rest = std::string_view();
    return b;
}

std::vector<std::string> dmStringList(std::string_view in) {
    std::vector<std::string> out;
    if (in.substr(0, 5) != "list(") return out;
    std::string s(in.substr(5));
    while (!s.empty() && s[0] == '"') {
        std::string_view rest;
        out.push_back(dmString(s, &rest));
        std::string t = trimSpace(rest);
        if (!t.empty() && t[0] == ',') t.erase(0, 1);
        s = trimSpace(t);
    }
    return out;
}

// ---- URL host only ----
namespace {
bool hexv(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int hexn(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
// for ASCII c: true => invalid in a host
bool hostEscape(unsigned char c) {
    if (isAlnum(char(c))) return false;
    if (strchr("-_.~", c) && c) return false;
    if (strchr("!$&'()*+,;=:[]<>\"", c) && c) return false;
    return true;
}
}  // namespace

bool urlHost(std::string_view raw, std::string* host) {
    host->clear();
    for (unsigned char c : raw)
        if (c < 0x20 || c == 0x7f) return false;
    std::string_view s = raw;
    if (auto h = s.find('#'); h != std::string_view::npos) s = s.substr(0, h);
    if (s.empty()) return true;
    std::string_view rest = s;
    bool hasScheme = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (isAlnum(c) && !(c >= '0' && c <= '9')) continue;
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            if (i == 0) break;
            continue;
        }
        if (c == ':') {
            if (i == 0) return false;  // missing protocol scheme
            hasScheme = true;
            rest = s.substr(i + 1);
        }
        break;
    }
    bool hasQuery = false;
    if (auto q = rest.find('?'); q != std::string_view::npos) {
        rest = rest.substr(0, q);
        hasQuery = true;
    }
    (void)hasQuery;
    if (rest.empty() || rest[0] != '/') {
        if (hasScheme) return true;  // opaque
        // no scheme, no leading slash: first segment must not contain a colon
        std::string_view seg = rest.substr(0, rest.find('/'));
        if (seg.find(':') != std::string_view::npos) return false;
    }
    bool authority = rest.substr(0, 2) == "//" && (hasScheme || rest.substr(0, 3) != "///");
    if (!authority) return true;
    rest.remove_prefix(2);
    std::string_view auth = rest.substr(0, rest.find('/'));
    size_t at = auth.rfind('@');
    std::string_view hp = auth;
    if (at != std::string_view::npos) {
        // userinfo: ASCII letters and digits, plus - . _ : ~ ! $ & ' ( ) * + , ; = % @
        for (unsigned char c : auth.substr(0, at))
            if (!(isAlnum(char(c)) || (c && strchr("-._:~!$&'()*+,;=%@", c)))) return false;
        hp = auth.substr(at + 1);
    }
    {
        std::string_view h = hp;
        size_t colon = h.rfind(':');
        size_t rb = h.rfind(']');
        if (h.empty() || h[0] != '[') {
            // port = after the last colon
            if (colon != std::string_view::npos) {
                std::string_view port = h.substr(colon);
                if (port.size() > 1)
                    for (size_t i = 1; i < port.size(); i++)
                        if (port[i] < '0' || port[i] > '9') return false;
                h = h.substr(0, colon);
            }
        } else {
            if (rb == std::string_view::npos) return false;  // missing ']' in host
            std::string_view port = h.substr(rb + 1);
            if (!port.empty()) {
                if (port[0] != ':') return false;
                for (size_t i = 1; i < port.size(); i++)
                    if (port[i] < '0' || port[i] > '9') return false;
            }
        }
    }
    // The host is hp with percent escapes decoded; validate characters.
    std::string h;
    for (size_t i = 0; i < hp.size(); i++) {
        unsigned char c = hp[i];
        if (c == '%') {
            if (i + 2 >= hp.size() || !hexv(hp[i + 1]) || !hexv(hp[i + 2])) return false;
            unsigned char d = (unsigned char)(hexn(hp[i + 1]) << 4 | hexn(hp[i + 2]));
            if (hexn(hp[i + 1]) < 8 && hp.substr(i, 3) != "%25")
                return false;  // %-escapes below 0x80 are invalid in a host (except %25)
            h.push_back(char(d));
            i += 2;
            continue;
        }
        if (c < 0x80 && hostEscape(c)) return false;
        h.push_back(char(c));
    }
    *host = h;
    return true;
}

// ---- LiveWorld ----
std::vector<std::string> LiveWorld::lines() const {
    // line break = (?i)<br\s*/?>|\n ; tag = <[^>]*>
    std::vector<std::string> parts;
    std::string cur;
    auto isRe2Space = [](char c) { return c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' '; };
    for (size_t i = 0; i < status.size();) {
        if (status[i] == '\n') {
            parts.push_back(cur);
            cur.clear();
            i++;
            continue;
        }
        if (status[i] == '<' && i + 2 < status.size() + 0 && (status[i + 1] | 0x20) == 'b' &&
            (status[i + 2] | 0x20) == 'r') {
            size_t j = i + 3;
            while (j < status.size() && isRe2Space(status[j])) j++;
            if (j < status.size() && status[j] == '/') j++;
            if (j < status.size() && status[j] == '>') {
                parts.push_back(cur);
                cur.clear();
                i = j + 1;
                continue;
            }
        }
        cur.push_back(status[i++]);
    }
    parts.push_back(cur);
    std::vector<std::string> out;
    for (auto& l : parts) {
        std::string t;  // strip the tags
        for (size_t i = 0; i < l.size();) {
            if (l[i] == '<') {
                size_t e = l.find('>', i + 1);
                if (e != std::string::npos) {
                    i = e + 1;
                    continue;
                }
            }
            t.push_back(l[i++]);
        }
        std::string u = htmlUnescape(t), joined;
        for (auto& f : fields(u)) {
            if (!joined.empty()) joined.push_back(' ');
            joined += f;
        }
        if (!joined.empty()) out.push_back(joined);
    }
    return out;
}

std::string LiveWorld::title() const {
    auto l = lines();
    return l.empty() ? url : l[0];
}

std::optional<std::vector<LiveWorld>> parseListing(std::string_view rawIn, std::string* err) {
    std::string text = latin1ToUtf8(rawIn);
    std::vector<LiveWorld> worlds;
    int cur = -1;
    bool sawSection = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != '\t' && line[0] != ' ') {
            sawSection = true;
            cur = -1;
            if (line.compare(0, 6, "world/") == 0) {
                worlds.emplace_back();
                cur = int(worlds.size()) - 1;
            }
            continue;
        }
        if (cur < 0) continue;
        std::string t = trimSpace(line);
        size_t eq = t.find(" = ");
        if (eq == std::string::npos) continue;
        std::string key = t.substr(0, eq), val = t.substr(eq + 3);
        LiveWorld& w = worlds[cur];
        if (key == "url") {
            w.url = dmString(val, nullptr);
            std::string host;
            if (urlHost(w.url, &host) && host.compare(0, 12, "BYOND.world.") == 0) w.id = host.substr(12);
        } else if (key == "server_version") {
            long long v;
            goAtoi(val, &v);
            w.serverVersion = v;
        } else if (key == "status")
            w.status = dmString(val, nullptr);
        else if (key == "host")
            w.host = dmString(val, nullptr);
        else if (key == "players")
            w.players = dmStringList(val);
    }
    if (!sawSection) {
        if (err) *err = "hub listing: not in the hub's text format (" + std::to_string(rawIn.size()) + " bytes)";
        return std::nullopt;
    }
    return worlds;
}

}  // namespace fugue::pager
