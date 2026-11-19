#include "model/htmltext.h"

#include <cstdint>
#include <map>

namespace fugue::model {

namespace {

int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void putRune(std::string& out, uint32_t r) {
    if (r < 0x80)
        out += char(r);
    else if (r < 0x800) {
        out += char(0xC0 | (r >> 6));
        out += char(0x80 | (r & 0x3F));
    } else if (r < 0x10000) {
        out += char(0xE0 | (r >> 12));
        out += char(0x80 | ((r >> 6) & 0x3F));
        out += char(0x80 | (r & 0x3F));
    } else {
        out += char(0xF0 | (r >> 18));
        out += char(0x80 | ((r >> 12) & 0x3F));
        out += char(0x80 | ((r >> 6) & 0x3F));
        out += char(0x80 | (r & 0x3F));
    }
}

// Named references. Keys with a trailing ';' are the normal form; keys without are the legacy
// (semicolon-optional) names HTML5 keeps for the Latin-1 set and amp/lt/gt/quot.
const std::map<std::string, uint32_t>& entities() {
    static const std::map<std::string, uint32_t> m = [] {
        std::map<std::string, uint32_t> e;
        static const char* latin1[96] = {
            "nbsp",   "iexcl",  "cent",   "pound",  "curren", "yen",    "brvbar", "sect",   "uml",    "copy",
            "ordf",   "laquo",  "not",    "shy",    "reg",    "macr",   "deg",    "plusmn", "sup2",   "sup3",
            "acute",  "micro",  "para",   "middot", "cedil",  "sup1",   "ordm",   "raquo",  "frac14", "frac12",
            "frac34", "iquest", "Agrave", "Aacute", "Acirc",  "Atilde", "Auml",   "Aring",  "AElig",  "Ccedil",
            "Egrave", "Eacute", "Ecirc",  "Euml",   "Igrave", "Iacute", "Icirc",  "Iuml",   "ETH",    "Ntilde",
            "Ograve", "Oacute", "Ocirc",  "Otilde", "Ouml",   "times",  "Oslash", "Ugrave", "Uacute", "Ucirc",
            "Uuml",   "Yacute", "THORN",  "szlig",  "agrave", "aacute", "acirc",  "atilde", "auml",   "aring",
            "aelig",  "ccedil", "egrave", "eacute", "ecirc",  "euml",   "igrave", "iacute", "icirc",  "iuml",
            "eth",    "ntilde", "ograve", "oacute", "ocirc",  "otilde", "ouml",   "divide", "oslash", "ugrave",
            "uacute", "ucirc",  "uuml",   "yacute", "thorn",  "yuml"};
        for (int i = 0; i < 96; i++) {
            e[std::string(latin1[i]) + ";"] = 160 + i;
            e[latin1[i]] = 160 + i;
        }
        for (auto [n, c] : {std::pair<const char*, uint32_t>{"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}}) {
            e[std::string(n) + ";"] = c;
            e[n] = c;
        }
        for (auto [n, c] : {std::pair<const char*, uint32_t>{"apos", 39},
                            {"hellip", 0x2026},
                            {"mdash", 0x2014},
                            {"ndash", 0x2013},
                            {"lsquo", 0x2018},
                            {"rsquo", 0x2019},
                            {"ldquo", 0x201C},
                            {"rdquo", 0x201D},
                            {"bull", 0x2022},
                            {"trade", 0x2122},
                            {"euro", 0x20AC},
                            {"larr", 0x2190},
                            {"uarr", 0x2191},
                            {"rarr", 0x2192},
                            {"darr", 0x2193},
                            {"hearts", 0x2665},
                            {"num", '#'},
                            {"excl", '!'},
                            {"lpar", '('},
                            {"rpar", ')'},
                            {"comma", ','},
                            {"period", '.'},
                            {"colon", ':'},
                            {"semi", ';'},
                            {"equals", '='},
                            {"quest", '?'},
                            {"commat", '@'},
                            {"lsqb", '['},
                            {"rsqb", ']'},
                            {"lbrace", '{'},
                            {"rbrace", '}'},
                            {"sol", '/'},
                            {"bsol", '\\'},
                            {"plus", '+'},
                            {"percnt", '%'},
                            {"dollar", '$'},
                            {"ast", '*'},
                            {"Tab", 9},
                            {"NewLine", 10},
                            {"lowbar", '_'},
                            {"grave", '`'},
                            {"verbar", '|'},
                            {"hyphen", 0x2010},
                            {"dagger", 0x2020}})
            e[std::string(n) + ";"] = c;
        return e;
    }();
    return m;
}

constexpr uint32_t kWin1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                   0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                   0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

// Ported from Go's html package (BSD-3-Clause, Copyright 2009 The Go Authors; see THIRD-PARTY-NOTICES.md).
// Appends to out, returns bytes consumed.
size_t unescapeEntity(std::string& out, std::string_view s) {
    size_t i = 1;
    if (s.size() <= 1) {
        out += '&';
        return 1;
    }
    if (s[i] == '#') {
        if (s.size() <= 3) {
            out += '&';
            return 1;
        }
        i++;
        char c = s[i];
        bool hex = false;
        if (c == 'x' || c == 'X') {
            hex = true;
            i++;
        }
        int64_t x = 0;
        while (i < s.size()) {
            c = s[i];
            i++;
            if (hex) {
                int h = hexv(c);
                if (h >= 0) {
                    x = 16 * x + h;
                    if (x > 0x7fffffff) x &= 0x7fffffff;
                    continue;
                }
            } else if (c >= '0' && c <= '9') {
                x = 10 * x + (c - '0');
                if (x > 0x7fffffff) x &= 0x7fffffff;
                continue;
            }
            if (c != ';') i--;
            break;
        }
        if (i <= 3) {
            out += '&';
            return 1;
        }
        if (x >= 0x80 && x <= 0x9F)
            x = kWin1252[x - 0x80];
        else if (x == 0 || (x >= 0xD800 && x <= 0xDFFF) || x > 0x10FFFF)
            x = 0xFFFD;
        putRune(out, uint32_t(x));
        return i;
    }
    while (i < s.size()) {
        char c = s[i];
        i++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        if (c != ';') i--;
        break;
    }
    std::string name(s.substr(1, i - 1));
    const auto& ents = entities();
    if (!name.empty()) {
        auto it = ents.find(name);
        if (it != ents.end()) {
            putRune(out, it->second);
            return i;
        }
        size_t maxLen = name.size() - 1;
        if (maxLen > 6) maxLen = 6;
        for (size_t j = maxLen; j > 1; j--) {
            auto p = ents.find(name.substr(0, j));
            if (p != ents.end()) {
                putRune(out, p->second);
                return j + 1;
            }
        }
    }
    out.append(s.substr(0, i));
    return i;
}

}  // namespace

std::string decodeChatHTML(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        char c = raw[i];
        if (c == '%') {
            if (i + 2 >= raw.size() || hexv(raw[i + 1]) < 0 || hexv(raw[i + 2]) < 0) return std::string(raw);
            out += char(hexv(raw[i + 1]) * 16 + hexv(raw[i + 2]));
            i += 2;
        } else if (c == '+')
            out += ' ';
        else
            out += c;
    }
    return out;
}

std::string stripChatMarkup(std::string_view s) {
    std::string stripped;
    stripped.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '<') {
            size_t k = s.find('>', i + 1);
            if (k != std::string_view::npos && k > i + 1) {
                i = k + 1;
                continue;
            }
        }
        stripped += s[i++];
    }
    if (stripped.find('&') == std::string::npos) return stripped;
    std::string out;
    out.reserve(stripped.size());
    size_t p = 0;
    while (p < stripped.size()) {
        size_t a = stripped.find('&', p);
        if (a == std::string::npos) {
            out.append(stripped, p, std::string::npos);
            break;
        }
        out.append(stripped, p, a - p);
        p = a + unescapeEntity(out, std::string_view(stripped).substr(a));
    }
    return out;
}

std::string htmlEscape(std::string_view s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '\'': o += "&#39;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&#34;"; break;
            default: o += c;
        }
    }
    return o;
}

}  // namespace fugue::model
