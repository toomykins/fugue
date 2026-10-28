#include "skin/dmf.h"

#include <cctype>
#include <cstdlib>
#include <limits>

namespace fugue::skin {

std::string trimSpace(std::string_view s) {
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; };
    size_t a = 0, b = s.size();
    while (a < b && sp(s[a])) a++;
    while (b > a && sp(s[b - 1])) b--;
    return std::string(s.substr(a, b - a));
}

std::string lowerAscii(std::string_view s) {
    std::string o(s);
    for (char& c : o) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return o;
}

std::string upperAscii(std::string_view s) {
    std::string o(s);
    for (char& c : o) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return o;
}

bool parseInt64(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') { neg = s[0] == '-'; i = 1; }
    if (i >= s.size()) return false;
    // accumulate as negative so INT64_MIN is representable
    const int64_t lim = std::numeric_limits<int64_t>::min();
    int64_t v = 0;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        int d = s[i] - '0';
        if (v < (lim + d) / 10) return false;  // overflow
        v = v * 10 - d;
    }
    if (!neg) {
        if (v == lim) return false;
        v = -v;
    }
    out = v;
    return true;
}

namespace {

bool eqFoldNone(const std::string& v) { return lowerAscii(v) == "none"; }

// Decodes one value token (see dmf.h). Returns false on an unterminated quote.
bool scanValue(std::string_view s, std::string& out) {
    out.clear();
    if (s.empty()) return true;
    char q = s[0];
    if (q != '"' && q != '\'') { out = trimSpace(s); return true; }
    for (size_t i = 1; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\') {
            if (i + 1 >= s.size()) { out.push_back('\\'); break; }
            i++;
            switch (s[i]) {
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case '\'': out.push_back('\''); break;
            case 't': out.push_back('\t'); break;
            case 'n': out.push_back('\n'); break;
            default: out.push_back('\\'); out.push_back(s[i]);  // unknown escape: keep both
            }
            continue;
        }
        if (c == q) return true;
        out.push_back(c);
    }
    return false;
}

}  // namespace

const std::string* Elem::get(const std::string& key) const {
    auto it = idx.find(key);
    return it == idx.end() ? nullptr : &rows[it->second].value;
}

std::string Elem::str(const std::string& key, const std::string& def) const {
    const std::string* v = get(key);
    return v ? *v : def;
}

int64_t Elem::intv(const std::string& key, int64_t def) const {
    const std::string* v = get(key);
    if (!v || v->empty() || eqFoldNone(*v)) return def;
    int64_t n;
    if (!parseInt64(trimSpace(*v), n)) return def;
    return n;
}

double Elem::floatv(const std::string& key, double def) const {
    const std::string* v = get(key);
    if (!v || v->empty() || eqFoldNone(*v)) return def;
    std::string t = trimSpace(*v);
    char* end = nullptr;
    double d = std::strtod(t.c_str(), &end);
    return t.empty() || *end ? def : d;
}

bool Elem::boolv(const std::string& key, bool def) const {
    const std::string* v = get(key);
    if (!v) return def;
    std::string l = lowerAscii(trimSpace(*v));
    if (l == "true" || l == "1" || l == "yes" || l == "on") return true;
    if (l == "false" || l == "0" || l == "no" || l == "off") return false;
    return def;
}

bool Elem::point(const std::string& key, int64_t& x, int64_t& y) const {
    const std::string* v = get(key);
    if (!v || eqFoldNone(*v)) return false;
    size_t c = v->find(',');
    if (c == std::string::npos) return false;
    return parseInt64(trimSpace(v->substr(0, c)), x) && parseInt64(trimSpace(v->substr(c + 1)), y);
}

bool Elem::size(const std::string& key, int64_t& w, int64_t& h) const {
    const std::string* v = get(key);
    if (!v || eqFoldNone(*v)) return false;
    std::string l = lowerAscii(*v);
    size_t c = l.find('x');
    if (c == std::string::npos) return false;
    return parseInt64(trimSpace(l.substr(0, c)), w) && parseInt64(trimSpace(l.substr(c + 1)), h);
}

bool Elem::color(const std::string& key, uint8_t rgba[4]) const {
    const std::string* v = get(key);
    if (!v || v->empty() || eqFoldNone(*v)) return false;
    std::string s = trimSpace(*v);
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8) return false;
    uint32_t n = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        n = n * 16 + uint32_t(d);
    }
    uint8_t a = 255;
    if (s.size() == 8) { a = uint8_t(n); n >>= 8; }
    rgba[0] = uint8_t(n >> 16); rgba[1] = uint8_t(n >> 8); rgba[2] = uint8_t(n); rgba[3] = a;
    return true;
}

std::string Elem::type() const { return upperAscii(trimSpace(str("type", ""))); }

const Elem* Block::elem(const std::string& n) const {
    for (const Elem& e : elems) if (e.name == n) return &e;
    return nullptr;
}

const Elem* Block::main() const {
    for (const Elem& e : elems) if (e.type() == "MAIN") return &e;
    return nullptr;
}

std::vector<const Elem*> Block::controls() const {
    std::vector<const Elem*> out;
    for (const Elem& e : elems) if (e.type() != "MAIN") out.push_back(&e);
    return out;
}

const Block* File::window(const std::string& n) const {
    const Block* r = nullptr;
    for (const Block& b : blocks) if (b.kind == "window" && b.name == n) r = &b;
    return r;
}

const Block* File::macro(const std::string& n) const {
    const Block* r = nullptr;
    for (const Block& b : blocks) if (b.kind == "macro" && b.name == n) r = &b;
    return r;
}

ParseResult parseDmf(std::string_view src) {
    ParseResult R;
    File& f = R.file;
    Block* cur = nullptr;
    Elem* elem = nullptr;
    auto fail = [&](int line, const std::string& msg) {
        R.ok = false; R.errLine = line; R.error = msg;
        return R;
    };
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= src.size()) {
        size_t nl = src.find('\n', pos);
        std::string_view rawv = src.substr(pos, (nl == std::string_view::npos ? src.size() : nl) - pos);
        pos = nl == std::string_view::npos ? src.size() + 1 : nl + 1;
        lineNo++;
        while (!rawv.empty() && rawv.back() == '\r') rawv.remove_suffix(1);
        std::string line(rawv);
        std::string trimmed = trimSpace(line);
        if (trimmed.empty()) continue;
        bool indented = line[0] == ' ' || line[0] == '\t';

        if (!indented) {
            size_t sp = trimmed.find(' ');
            std::string kind = lowerAscii(trimmed.substr(0, sp));
            std::string rest = sp == std::string::npos ? "" : trimmed.substr(sp + 1);
            if (kind != "window" && kind != "menu" && kind != "macro") return fail(lineNo, "unknown datum kind");
            std::string name;
            if (!scanValue(trimSpace(rest), name)) return fail(lineNo, "unterminated quoted value");
            f.blocks.push_back(Block{});
            cur = &f.blocks.back();
            cur->kind = kind; cur->name = name; cur->line = lineNo;
            elem = nullptr;
            continue;
        }
        if (!cur) return fail(lineNo, "row before any header");

        if (trimmed == "elem" || trimmed.rfind("elem ", 0) == 0 || trimmed.rfind("elem\t", 0) == 0) {
            std::string rest = trimSpace(trimmed.substr(4));
            std::string name;
            if (!rest.empty() && !scanValue(rest, name)) return fail(lineNo, "unterminated quoted value");
            cur->elems.push_back(Elem{});
            elem = &cur->elems.back();
            elem->name = name; elem->line = lineNo;
            continue;
        }
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) return fail(lineNo, "neither an elem nor a key = value row");
        if (!elem) return fail(lineNo, "key outside any elem");
        std::string dec;
        if (!scanValue(trimSpace(trimmed.substr(eq + 1)), dec)) return fail(lineNo, "unterminated quoted value");
        std::string k = lowerAscii(trimSpace(trimmed.substr(0, eq)));
        elem->idx[k] = int(elem->rows.size());
        elem->rows.push_back(KV{k, dec, lineNo});
    }
    R.ok = true;
    return R;
}

}  // namespace fugue::skin
