#include "skin/winset.h"

#include "skin/dmf.h"

#include <algorithm>
#include "net/textmacro.h"

namespace fugue::skin {

std::vector<std::string> splitOutsideQuotes(std::string_view s, bool escapes) {
    std::vector<std::string> out;
    bool inQ = false;
    long start = -1;
    for (size_t i = 0; i < s.size(); i++) {
        char r = s[i];
        if (escapes && inQ && r == '\\') {
            i++;
        } else if (r == '"') {
            inQ = !inQ;
            if (start < 0) start = long(i);
        } else if (!inQ && (r == ';' || r == '&' || r == ' ' || r == '\t' || r == '\n')) {
            if (start >= 0) { out.emplace_back(s.substr(size_t(start), i - size_t(start))); start = -1; }
        } else if (start < 0) {
            start = long(i);
        }
    }
    if (start >= 0) out.emplace_back(s.substr(size_t(start)));
    return out;
}

namespace {
std::string urlDecode(const std::string& v) {
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '+') { out.push_back(' '); continue; }
        if (v[i] == '%' && i + 2 < v.size() && hex(v[i + 1]) >= 0 && hex(v[i + 2]) >= 0) {
            out.push_back(char(hex(v[i + 1]) * 16 + hex(v[i + 2])));
            i += 2;
            continue;
        }
        out.push_back(v[i]);
    }
    return out;
}

std::string unescapeQuoted(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] != '\\' || i + 1 == v.size()) { out.push_back(v[i]); continue; }
        char c = v[++i];
        if (c == 'n') out += "\r\n";
        else if (c == 't') out.push_back('\t');
        else out.push_back(c);
    }
    return out;
}
}  // namespace

namespace {
struct Tok { std::string text; size_t b = 0, e = 0; bool quoted = false; };

// DreamSeeker's winset tokenizer (docs/protocol/GAME-CLIENT-SERVER-516.md, "Winset params"). White space and '=' are
// interchangeable separators, and so, when `wide` (a world's winset), are '?' ':' '!'; a token is a quoted run ('...' or
// "...", only at its start) or a run of anything else. ';' and '&' end a pair. Inside a pair the tokens alternate key,
// value; a lone last token is a key with an empty value.
std::vector<std::pair<Tok, Tok>> stockPairs(std::string_view s, bool escapes, bool wide) {
    std::vector<std::pair<Tok, Tok>> out;
    std::vector<Tok> seg;
    auto flush = [&] {
        for (size_t k = 0; k < seg.size(); k += 2) out.emplace_back(seg[k], k + 1 < seg.size() ? seg[k + 1] : Tok{"", seg[k].e, seg[k].e, false});
        seg.clear();
    };
    auto sep = [&](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '=' || (wide && (c == '?' || c == ':' || c == '!')); };
    auto end = [](char c) { return c == ';' || c == '&'; };
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if (end(c)) { flush(); i++; continue; }
        if (sep(c)) { i++; continue; }
        Tok t;
        t.b = i;
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && s[j] != c) j += (escapes && s[j] == '\\') ? 2 : 1;
            j = std::min(j, n);
            t.text = std::string(s.substr(i + 1, j - i - 1));
            t.quoted = true;
            i = std::min(j + 1, n);
            t.e = i;
            while (i < n && !sep(s[i]) && !end(s[i])) i++;   // what trails a closing quote is dropped
        } else {
            while (i < n && !sep(s[i]) && !end(s[i])) i++;
            t.text = std::string(s.substr(t.b, i - t.b));
            t.e = i;
        }
        seg.push_back(std::move(t));
    }
    flush();
    return out;
}
}  // namespace

std::vector<WinsetPair> splitWinset(std::string_view id, std::string_view props, bool urlEncoded) {
    std::vector<WinsetPair> out;
    for (const auto& [k, v] : stockPairs(props, urlEncoded, urlEncoded)) {
        WinsetPair p;
        p.raw = std::string(props.substr(k.b, v.e - k.b));
        if (urlEncoded)
            p.value = net::plainText(v.quoted ? unescapeQuoted(v.text) : urlDecode(v.text));
        else
            p.value = v.text;
        p.target = std::string(id);
        p.key = k.text;
        if (p.target.empty()) {
            size_t dot = k.text.rfind('.');
            if (dot == std::string::npos) { out.push_back(p); continue; }
            p.target = k.text.substr(0, dot);
            p.key = k.text.substr(dot + 1);
        }
        p.key = lowerAscii(trimSpace(p.key));
        p.ok = !p.key.empty();
        out.push_back(p);
    }
    return out;
}

bool winsetBody(std::string_view cmd, std::string& body) {
    std::string t = trimSpace(cmd);
    if (t.rfind(".winset", 0) != 0) return false;
    std::string rest = trimSpace(t.substr(7));
    if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"') return false;
    body = rest.substr(1, rest.size() - 2);
    return true;
}

// The on-show body is re-split by splitWinset, so `key = value` is rejoined to `key=value` first.
static std::string joinSpacedEquals(std::string_view s) {
    std::string t;
    bool inQ = false;
    for (size_t i = 0; i < s.size(); i++) {
        char r = s[i];
        if (r == '"') inQ = !inQ;
        if (!inQ && (r == ' ' || r == '\t')) {
            size_t j = i;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
            if ((!t.empty() && t.back() == '=') || (j < s.size() && s[j] == '=')) { i = j - 1; continue; }
        }
        t.push_back(r);
    }
    return t;
}

bool onShowParams(std::string_view cmd, std::string& params) {
    std::string body;
    if (!winsetBody(cmd, body)) return false;
    params.clear();
    bool first = true;
    for (const std::string& tok : splitOutsideQuotes(joinSpacedEquals(body))) {
        if (tok.find('?') != std::string::npos) continue;
        if (!first) params.push_back(';');
        params += tok;
        first = false;
    }
    return true;
}

namespace {
WinsetPair makePair(const std::string& raw, const std::string& lhs, const std::string& value) {
    WinsetPair p;
    p.raw = raw;
    p.value = value;
    size_t dot = lhs.rfind('.');
    if (dot == std::string::npos) return p;
    p.target = lhs.substr(0, dot);
    p.key = lowerAscii(trimSpace(lhs.substr(dot + 1)));
    p.ok = !p.key.empty() && !p.target.empty();
    return p;
}
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}  // namespace

bool hasConditional(std::string_view body) {
    bool inQ = false;
    for (size_t i = 0; i < body.size(); i++) {
        if (inQ && body[i] == '\\') { i++; continue; }
        if (body[i] == '"') inQ = !inQ;
        else if (!inQ && body[i] == '?') return true;
    }
    return false;
}

std::vector<WinsetClause> parseClientWinset(std::string_view s) {
    std::vector<WinsetClause> out(1);   // out.back() is the clause being filled
    bool inElse = false;
    auto branch = [&]() -> std::vector<WinsetPair>& { return inElse ? out.back().otherwise : out.back().then; };
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (isSpace(c)) { i++; continue; }
        if (c == ';') {   // the conditional is over: what follows is unconditional again
            if (out.back().conditional || !out.back().then.empty()) out.emplace_back();
            inElse = false;
            i++;
            continue;
        }
        if (c == '?') {   // the pair just read is the condition of a NEW clause
            std::vector<WinsetPair>& b = branch();
            if (b.empty()) { i++; continue; }   // a stray '?': nothing to test
            WinsetPair cond = b.back();
            b.pop_back();
            size_t eq = cond.raw.find('=');
            bool neg = eq != std::string::npos && eq > 0 && cond.raw[eq - 1] == '!';   // keys never hold '!'
            if (!out.back().conditional && out.back().then.empty()) out.pop_back();
            out.emplace_back();
            out.back().conditional = true;
            out.back().cond = cond;
            out.back().negate = neg;
            inElse = false;
            i++;
            continue;
        }
        if (c == ':' && out.back().conditional && !inElse) { inElse = true; i++; continue; }
        if (c == '"') {   // a quoted run with no key (such as the `""` between two pairs): skip it
            for (i++; i < n && s[i] != '"'; i++) if (s[i] == '\\') i++;
            i++;
            continue;
        }
        // A pair: key up to '=' (or "!="), then a quoted or bare value.
        size_t start = i;
        std::string lhs;
        while (i < n && s[i] != '=' && !isSpace(s[i]) && s[i] != ';' && s[i] != '?') {
            if (s[i] == '!' && i + 1 < n && s[i + 1] == '=') break;
            lhs.push_back(s[i++]);
        }
        if (i >= n || (s[i] != '=' && s[i] != '!')) {   // no '=': not a pair
            WinsetPair bad;
            bad.raw = std::string(s.substr(start, i - start));
            if (!bad.raw.empty()) branch().push_back(bad);
            if (i == start) i++;
            continue;
        }
        i += s[i] == '!' ? 2 : 1;
        std::string val;
        if (i < n && s[i] == '"') {
            for (i++; i < n && s[i] != '"'; i++) {
                if (s[i] == '\\' && i + 1 < n) i++;
                val.push_back(s[i]);
            }
            if (i < n) i++;   // the closing quote ends the pair, whatever follows it
        } else {
            bool thenBranch = out.back().conditional && !inElse;
            while (i < n && !isSpace(s[i]) && s[i] != ';' && s[i] != '?' && s[i] != '"' && !(thenBranch && s[i] == ':')) val.push_back(s[i++]);
        }
        branch().push_back(makePair(std::string(s.substr(start, i - start)), lhs, val));
    }
    if (out.size() > 1 && !out.back().conditional && out.back().then.empty()) out.pop_back();
    return out;
}

std::vector<std::string> wingetParamNames(std::string_view params) {
    std::vector<std::string> out;
    size_t i = 0, n = params.size();
    while (i <= n) {
        size_t j = i;
        while (j < n && params[j] != ';' && params[j] != '&') j++;
        std::string_view item = params.substr(i, j - i);
        size_t a = 0;
        while (a < item.size() && isSpace(item[a])) a++;
        size_t b = a;
        while (b < item.size() && !isSpace(item[b]) && item[b] != '=') b++;
        if (b > a) out.push_back(lowerAscii(item.substr(a, b - a)));
        i = j + 1;
    }
    return out;
}

std::string wingetEncode(std::string_view v) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : v) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') out.push_back(char(c));
        else if (c == ' ') out.push_back('+');
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

std::string wingetAnswer(std::string_view params, const std::function<bool(const std::string&, std::string&)>& lookup) {
    std::vector<std::string> names = wingetParamNames(params);
    if (names.size() == 1) { std::string v; return lookup(names[0], v) ? v : std::string(); }
    std::string out;
    for (const std::string& n : names) {
        std::string v;
        if (!lookup(n, v)) v.clear();
        if (!out.empty()) out.push_back(';');
        out += n + "=" + wingetEncode(v);
    }
    return out;
}

std::string expandSkinParams(std::string_view cmd, int w, int h) {
    static const std::string needle = "[[inner-size]]";
    std::string repl = std::to_string(w) + " " + std::to_string(h);
    std::string out;
    size_t i = 0;
    for (;;) {
        size_t j = cmd.find(needle, i);
        if (j == std::string_view::npos) { out.append(cmd.substr(i)); return out; }
        out.append(cmd.substr(i, j - i));
        out += repl;
        i = j + needle.size();
    }
}

bool parsePair(std::string_view v, std::string_view seps, long long& a, long long& b) {
    std::string s;
    for (size_t i = 0; i < v.size();) {
        if (v[i] == '%' && i + 2 < v.size() && v[i + 1] == '2' && (v[i + 2] == 'c' || v[i + 2] == 'C')) { s.push_back(','); i += 3; }
        else s.push_back(v[i++]);
    }
    size_t i = s.find_first_of(seps);
    if (i == std::string::npos) return false;
    int64_t x, y;
    if (!parseInt64(trimSpace(s.substr(0, i)), x)) return false;
    if (!parseInt64(trimSpace(s.substr(i + 1)), y)) return false;
    a = x; b = y;
    return true;
}

}  // namespace fugue::skin
