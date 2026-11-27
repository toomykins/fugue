// Rules follow stock DreamSeeker 516; dsmaptext.h says which behaviour they reproduce.
#include "render/dsmaptext.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <vector>

namespace fugue::render {
namespace {

using std::string;
using std::string_view;

struct GdiStrike {
    int pt, h, ascent, il, first, defaultChar;
    uint8_t adv[224];  // firstChar (32) .. 255, cp1252
    int dpi = 96;      // the resolution its file was digitised for
};
#include "render/gdifont_tables.inc"
#include "render/gdifont_windows.inc"

enum class Bitmap { None, SmallFonts, MsSansSerif, Courier };

std::vector<const GdiStrike*> strikesOf(Bitmap b, GdiProfile p) {
    switch (b) {
        case Bitmap::SmallFonts:
            // Windows' files have more strikes than Wine's (stock 516.1689 on Windows 11 draws 18pt from the 18pt strike),
            // and it registers a second set of files, made for 120 dpi, whose strikes stock draws too (16pt from that
            // set's 12pt strike). The largest comes last. The strikes both have differ in
            // bytes 0x7f to 0x9f, where Windows' mostly have no glyph.
            if (p == GdiProfile::Windows)
                return {&kWinSmallFonts_2, &kWinSmallFonts_3, &kWinSmallFonts_4, &kWinSmallFonts_5, &kWinSmallFonts_6,
                        &kWinSmallFonts_7, &kWin120SmallFonts_6, &kWin120SmallFonts_7};
            return {&kFaceSmallFonts_7};
        case Bitmap::MsSansSerif:
            if (p == GdiProfile::Windows)
                return {&kWinMsSansSerif_8, &kWinMsSansSerif_10, &kWinMsSansSerif_12, &kWinMsSansSerif_14,
                        &kWin120MsSansSerif_12, &kWin120MsSansSerif_14, &kWinMsSansSerif_18, &kWin120MsSansSerif_18,
                        &kWinMsSansSerif_24, &kWin120MsSansSerif_24};
            return {&kFaceMsSansSerif_8, &kFaceMsSansSerif_10, &kFaceMsSansSerif_12};
        case Bitmap::Courier:
            if (p == GdiProfile::Windows) return {&kFaceCourier_10, &kWinCourier_12, &kWinCourier_15, &kWin120Courier_15};
            return {&kFaceCourier_10};
        default: return {};
    }
}

// ---- text helpers ------------------------------------------------------------------------------------------------

string lower(string_view s) {
    string o(s);
    for (char& c : o)
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
    return o;
}
bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
string_view trim(string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) a++;
    while (b > a && isWs(s[b - 1])) b--;
    return s.substr(a, b - a);
}
int roundHalfUp(double x) { return int(std::floor(x + 0.5)); }

// UTF-8 (BYOND 516 strings) -> code points; an invalid byte is U+FFFD.
std::u32string decodeUtf8(string_view s) {
    std::u32string o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        char32_t cp = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
        bool ok = n > 0 && i + size_t(n) <= s.size();
        for (int k = 1; ok && k < n; k++) {
            unsigned char d = static_cast<unsigned char>(s[i + size_t(k)]);
            if ((d & 0xc0) != 0x80) ok = false;
            cp = (cp << 6) | (d & 0x3f);
        }
        if (!ok) {
            o.push_back(0xFFFD);
            i++;
            continue;
        }
        o.push_back(cp);
        i += size_t(n);
    }
    return o;
}

// Unicode -> the ANSI (cp1252) byte a charset-0 FNT face is indexed by; -1 = not in the code page (the default char).
// Stock DreamSeeker maps the cp1252 range to its bytes and every other code point to the default char.
int cp1252(char32_t cp) {
    if (cp < 0x80 || (cp >= 0xa0 && cp <= 0xff)) return int(cp);
    static const std::map<char32_t, int> t = {
        {0x20ac, 0x80}, {0x201a, 0x82}, {0x0192, 0x83}, {0x201e, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
        {0x02c6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c}, {0x017d, 0x8e}, {0x2018, 0x91},
        {0x2019, 0x92}, {0x201c, 0x93}, {0x201d, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02dc, 0x98},
        {0x2122, 0x99}, {0x0161, 0x9a}, {0x203a, 0x9b}, {0x0153, 0x9c}, {0x017e, 0x9e}, {0x0178, 0x9f}};
    auto it = t.find(cp);
    return it == t.end() ? -1 : it->second;
}

// Named entities DreamSeeker decodes: &lt; &gt; &amp; &quot; &nbsp; &copy; &eacute;, and numeric forms. An unknown name
// (&bogus;) is dropped, and "&amp" without its ';' stays literal. The table is the HTML 4 Latin-1 set plus common
// punctuation; anything else counts as unknown.
const std::map<string, char32_t>& namedEntities() {
    static const std::map<string, char32_t> m = [] {
        std::map<string, char32_t> e = {
            {"quot", 34},       {"amp", 38},       {"apos", 39},       {"lt", 60},         {"gt", 62},
            {"oelig", 0x153},   {"OElig", 0x152},  {"scaron", 0x161},  {"Scaron", 0x160},  {"Yuml", 0x178},
            {"fnof", 0x192},    {"circ", 0x2c6},   {"tilde", 0x2dc},   {"ensp", 0x2002},   {"emsp", 0x2003},
            {"thinsp", 0x2009}, {"ndash", 0x2013}, {"mdash", 0x2014},  {"lsquo", 0x2018},  {"rsquo", 0x2019},
            {"sbquo", 0x201a},  {"ldquo", 0x201c}, {"rdquo", 0x201d},  {"bdquo", 0x201e},  {"dagger", 0x2020},
            {"Dagger", 0x2021}, {"bull", 0x2022},  {"hellip", 0x2026}, {"permil", 0x2030}, {"lsaquo", 0x2039},
            {"rsaquo", 0x203a}, {"euro", 0x20ac},  {"trade", 0x2122}};
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
        for (int i = 0; i < 96; i++) e[latin1[i]] = char32_t(0xa0 + i);
        return e;
    }();
    return m;
}

void appendUtf8(string& o, char32_t r) {
    if (r > 0x10FFFF || (r >= 0xD800 && r < 0xE000)) r = 0xFFFD;
    if (r < 0x80)
        o.push_back(char(r));
    else if (r < 0x800) {
        o.push_back(char(0xC0 | (r >> 6)));
        o.push_back(char(0x80 | (r & 0x3F)));
    } else if (r < 0x10000) {
        o.push_back(char(0xE0 | (r >> 12)));
        o.push_back(char(0x80 | ((r >> 6) & 0x3F)));
        o.push_back(char(0x80 | (r & 0x3F)));
    } else {
        o.push_back(char(0xF0 | (r >> 18)));
        o.push_back(char(0x80 | ((r >> 12) & 0x3F)));
        o.push_back(char(0x80 | ((r >> 6) & 0x3F)));
        o.push_back(char(0x80 | (r & 0x3F)));
    }
}

bool isEntityName(string_view s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '#' && i == 0);
        if (!ok) return false;
    }
    return true;
}

string decodeEntities(string_view s) {
    string o;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            size_t j = s.find(';', i);
            if (j != string_view::npos && j > i + 1 && j - i <= 10 && isEntityName(s.substr(i + 1, j - i - 1))) {
                string_view name = s.substr(i + 1, j - i - 1);
                if (name[0] == '#') {
                    bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
                    string_view digits = name.substr(hex ? 2 : 1);
                    long v = 0;
                    bool good = !digits.empty();
                    for (char c : digits) {
                        int d = (c >= '0' && c <= '9')        ? c - '0'
                                : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                                : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                              : -1;
                        if (d < 0) {
                            good = false;
                            break;
                        }
                        v = v * (hex ? 16 : 10) + d;
                        if (v > 0x10FFFF) {
                            good = false;
                            break;
                        }
                    }
                    if (good) {
                        if (v > 0) appendUtf8(o, char32_t(v));
                        i = j + 1;
                        continue;
                    }
                } else {
                    auto it = namedEntities().find(string(name));
                    if (it != namedEntities().end()) appendUtf8(o, it->second);
                    i = j + 1;  // known: decoded; unknown: dropped
                    continue;
                }
            }
        }
        o.push_back(s[i]);
        i++;
    }
    return o;
}

// ---- style ------------------------------------------------------------------------------------------------------

struct Style {
    string family;  // lower-case first entry of font-family; "" = DreamSeeker's default face
    int pt = 10;
    bool bold = false, italic = false;
    double lh = 1.2;  // line-height multiplier; DreamSeeker's default is 1.2 (MS Sans Serif pitch 15 = floor(1.2 * 13))
    // Paint-only properties: they never change a measured width or height (see applyProps).
    uint32_t color = 0xffffffffu;  // 0xRRGGBBAA
    int align = 0;                 // 0 left, 1 center, 2 right
    int valign = 2;                // valign / vertical-align: 0 top, 1 middle, 2 bottom
    bool outline = false;
    uint32_t outlineColor = 0x000000ffu;
    int outlineWidth = 1;
    int outlineFlags = 0;
    bool underline = false;
    bool shadow = false;
    int shadowX = 0, shadowY = 0, shadowBlur = 0;
    uint32_t shadowColor = 0x000000ffu;
    // The element that set text-align: its text stands on lines of its own, also beside one with the same alignment
    // (stock 516.1689).
    int block = 0;
};
int g_blockSeq = 0;  // under the layout lock
using Decls = std::vector<std::pair<string, string>>;
struct Rule {
    string sel;
    Decls decls;
};

// HTML <font size=1..7> and the CSS size keywords (xx-small 8pt ... xx-large 48pt).
constexpr int kTierPt[8] = {0, 8, 10, 12, 18, 24, 36, 48};

std::optional<int> parseSize(string_view v0, int cur) {
    string v = lower(trim(v0));
    static const std::map<string, int> kw = {{"xx-small", 8}, {"x-small", 10}, {"small", 12},   {"medium", 18},
                                             {"large", 24},   {"x-large", 36}, {"xx-large", 48}};
    if (auto it = kw.find(v); it != kw.end()) return it->second;
    if (v == "smaller") return int(cur * 0.8);  // 0.8 of the current size
    if (v == "larger") return int(cur * 1.25);  // 1.25 of the current size
    size_t i = 0;
    while (i < v.size() && ((v[i] >= '0' && v[i] <= '9') || v[i] == '.' || v[i] == '+' || v[i] == '-')) i++;
    if (i == 0) return std::nullopt;
    double f = 0;
    try {
        f = std::stod(v.substr(0, i));
    } catch (...) { return std::nullopt; }
    string unit(trim(string_view(v).substr(i)));
    // A size stock cannot make a font of is ignored: 5000px draws at the size in effect, 4500px draws nothing because
    // its line does not fit (stock 516.1689; where between them was not probed).
    if ((unit == "px" || unit == "pt") && f > 4750) return std::nullopt;
    if (unit == "px" || unit == "pt") return roundHalfUp(f);  // px and pt are the same, rounded half up (8.5 -> 9)
    // em and %: 0.9 * cur * f.
    if (unit == "em") return roundHalfUp(cur * f * 0.9);
    if (unit == "%") return roundHalfUp(cur * f / 100 * 0.9);
    if (unit == "ex") return roundHalfUp(cur * f * 0.45);  // ex is half an em
    // Absolute units convert to pt exactly.
    if (unit == "pc") return roundHalfUp(f * 12);
    if (unit == "in") return roundHalfUp(f * 72);
    if (unit == "cm") return roundHalfUp(f * 72 / 2.54);
    if (unit == "mm") return roundHalfUp(f * 72 / 25.4);
    return std::nullopt;  // unitless: unmodelled
}

// Only a number or a percentage is a line-height; anything else (normal, 20px) gives pitch 0.
double parseLineHeight(string_view v0) {
    string v = lower(trim(v0));
    bool pct = !v.empty() && v.back() == '%';
    if (pct) v.pop_back();
    if (v.empty()) return 0;
    for (char c : v)
        if (!((c >= '0' && c <= '9') || c == '.')) return 0;
    double f = 0;
    try {
        f = std::stod(v);
    } catch (...) { return 0; }
    return pct ? f / 100 : f;
}

// A named colour, #rgb / #rgba / #rrggbb / #rrggbbaa, rgb() / rgba(). -> 0xRRGGBBAA.
std::optional<uint32_t> parseCssColor(string_view v0) {
    string v = lower(trim(v0));
    static const std::map<string, uint32_t> named = {
        {"black", 0x000000ff},  {"white", 0xffffffff},   {"red", 0xff0000ff},     {"green", 0x008000ff},
        {"lime", 0x00ff00ff},   {"blue", 0x0000ffff},    {"yellow", 0xffff00ff},  {"cyan", 0x00ffffff},
        {"aqua", 0x00ffffff},   {"magenta", 0xff00ffff}, {"fuchsia", 0xff00ffff}, {"silver", 0xc0c0c0ff},
        {"gray", 0x808080ff},   {"grey", 0x808080ff},    {"maroon", 0x800000ff},  {"olive", 0x808000ff},
        {"purple", 0x800080ff}, {"teal", 0x008080ff},    {"navy", 0x000080ff},    {"orange", 0xffa500ff},
        {"pink", 0xffc0cbff},   {"brown", 0xa52a2aff},   {"gold", 0xffd700ff},    {"transparent", 0x00000000}};
    if (auto it = named.find(v); it != named.end()) return it->second;
    if (v.rfind("rgb", 0) == 0) {
        size_t open = v.find('('), close = v.rfind(')');
        if (open == string::npos || close == string::npos || close < open) return std::nullopt;
        std::vector<string> parts;
        string cur;
        for (char c : v.substr(open + 1, close - open - 1)) {
            if (c == ',' || c == '/' || c == ' ') {
                if (!cur.empty()) parts.push_back(cur);
                cur.clear();
            } else
                cur.push_back(c);
        }
        if (!cur.empty()) parts.push_back(cur);
        if (parts.size() < 3) return std::nullopt;
        uint32_t out = 0;
        for (int i = 0; i < 3; i++) {
            bool pct = parts[size_t(i)].back() == '%';
            double f = 0;
            try {
                f = std::stod(pct ? parts[size_t(i)].substr(0, parts[size_t(i)].size() - 1) : parts[size_t(i)]);
            } catch (...) { return std::nullopt; }
            if (pct) f = f * 255 / 100;
            out = (out << 8) | uint32_t(std::clamp(roundHalfUp(f), 0, 255));
        }
        double a = 1;
        if (parts.size() >= 4) {
            try {
                a = std::stod(parts[3]);
            } catch (...) { return std::nullopt; }
        }
        return (out << 8) | uint32_t(std::clamp(roundHalfUp(a * 255), 0, 255));
    }
    if (v.empty() || v[0] != '#') return std::nullopt;
    string h = v.substr(1);
    if (h.size() == 3 || h.size() == 4) {
        string d;
        for (char c : h) {
            d.push_back(c);
            d.push_back(c);
        }
        h = d;
    }
    if (h.size() != 6 && h.size() != 8) return std::nullopt;
    uint32_t n = 0;
    for (char c : h) {
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
        if (d < 0) return std::nullopt;
        n = (n << 4) | uint32_t(d);
    }
    return h.size() == 6 ? (n << 8) | 0xffu : n;
}

// The first entry of a font-family list: a quoted name up to its closing quote, else up to the first comma.
string firstFamily(string_view v) {
    v = trim(v);
    if (!v.empty() && (v.front() == '"' || v.front() == '\'')) {
        size_t e = v.find(v.front(), 1);
        return lower(trim(v.substr(1, e == string_view::npos ? string_view::npos : e - 1)));
    }
    if (size_t c = v.find(','); c != string_view::npos) v = v.substr(0, c);
    v = trim(v);
    while (!v.empty() && (v.back() == '"' || v.back() == '\'')) v.remove_suffix(1);
    return lower(trim(v));
}

bool isBoldWeight(string_view v) {
    string t = lower(trim(v));
    return t == "bold" || t == "bolder" ||
           (!t.empty() && t.size() < 5 && t.find_first_not_of("0123456789") == string::npos && std::stoi(t) >= 600);
}

constexpr int kShorthandPt = 12;

int parseValign(string_view v, int cur) {
    string t = lower(trim(v));
    return t == "top" ? 0 : t == "middle" ? 1 : t == "bottom" ? 2 : cur;
}

// One declaration list (a style attribute or one rule). The first declaration of a property wins; the `font` shorthand
// sets family, size, weight and style at once, defaulting what it leaves out.
void applyProps(Style& st, const Decls& props) {
    std::vector<string> seen;
    auto first = [&](const string& k) {
        if (std::find(seen.begin(), seen.end(), k) != seen.end()) return false;
        seen.push_back(k);
        return true;
    };
    for (const auto& [k, v] : props) {
        if (k == "font") {
            string_view rest = trim(v);
            bool bold = false, italic = false;
            int pt = kShorthandPt;
            while (!rest.empty()) {
                size_t e = 0;
                while (e < rest.size() && !isWs(rest[e])) e++;
                string t = lower(rest.substr(0, e));
                if (t == "italic" || t == "oblique")
                    italic = true;
                else if (isBoldWeight(t))
                    bold = true;
                else if (t == "normal" || t == "lighter" || t == "small-caps" ||
                         (t.size() == 3 && t[1] == '0' && t[2] == '0')) {
                } else {
                    string sz = t.substr(0, t.find('/'));
                    if (auto p = parseSize(sz, st.pt); p && *p > 0) {
                        pt = *p;
                        rest = trim(rest.substr(e));
                    }
                    break;
                }
                rest = trim(rest.substr(e));
            }
            if (first("font-family")) st.family = firstFamily(rest);
            if (first("font-size")) st.pt = pt;
            if (first("font-weight")) st.bold = bold;
            if (first("font-style")) st.italic = italic;
            continue;
        }
        if (!first(k)) continue;
        if (k == "color") {
            if (auto c = parseCssColor(v)) st.color = *c;
        } else if (k == "text-align") {
            string t = lower(trim(v));
            if (t == "center")
                st.align = 1;
            else if (t == "right")
                st.align = 2;
            else if (t == "left")
                st.align = 0;
            if (t == "center" || t == "right" || t == "left") st.block = ++g_blockSeq;
        } else if (k == "text-decoration") {
            string t = lower(trim(v));
            if (t.find("underline") != string::npos)
                st.underline = true;
            else if (t == "none")
                st.underline = false;
        } else if (k == "text-shadow") {
            // "<x> <y> [blur] <colour>".
            st.shadow = false;
            int lengths[3] = {0, 0, 0}, n = 0;
            st.shadowColor = 0x000000ffu;
            string_view rest = v;
            while (!rest.empty()) {
                size_t sp = rest.find_first_of(" \t");
                string_view f = rest.substr(0, sp);
                if (!f.empty() && (f[0] == '-' || f[0] == '+' || f[0] == '.' || (f[0] >= '0' && f[0] <= '9'))) {
                    if (n < 3) lengths[n++] = std::atoi(string(f).c_str());
                } else if (n < 2) {  // a colour before the offsets is not a shadow (stock 516.1689)
                    n = 0;
                    break;
                } else if (auto col = parseCssColor(f))
                    st.shadowColor = *col;
                if (sp == string_view::npos) break;
                rest = rest.substr(sp + 1);
            }
            if (n >= 2) {
                st.shadow = true;
                st.shadowX = std::clamp(lengths[0], -64, 64);
                st.shadowY = std::clamp(lengths[1], -64, 64);
                st.shadowBlur = std::clamp(lengths[2], 0, 64);  // stock blurs by 20px too
            }
        } else if (k == "vertical-align") {
            st.valign = parseValign(v, st.valign);
        } else if (k == "-dm-text-outline" || k == "-dm-text-outline-color") {
            // "<width> <color> [sharp] [square]" (or a bare colour): stock's outline filter on the text, 1 px and black
            // unless a width or a colour is named (stock 516.1689); 0px is no outline.
            st.outline = true;
            st.outlineColor = 0x000000ffu;
            if (k == "-dm-text-outline") st.outlineWidth = 1, st.outlineFlags = 0;
            string_view rest = v;
            bool widthSet = k != "-dm-text-outline";
            while (!rest.empty()) {
                size_t sp = rest.find_first_of(" \t");
                string_view f = rest.substr(0, sp);
                if (!widthSet && !f.empty() && f[0] >= '0' && f[0] <= '9') {
                    st.outlineWidth = std::clamp(std::atoi(string(f).c_str()), 0, 64);  // 12px is 12px (stock 516.1689)
                    widthSet = true;
                } else if (k == "-dm-text-outline" && (lower(f) == "sharp" || lower(f) == "square"))
                    st.outlineFlags |= lower(f) == "sharp" ? 1 : 2;  // the outline filter's flags
                else if (auto c = parseCssColor(f))
                    st.outlineColor = *c;
                if (sp == string_view::npos) break;
                rest = rest.substr(sp + 1);
            }
        } else if (k == "font-family") {
            st.family = firstFamily(v);
        } else if (k == "font-size") {
            if (auto p = parseSize(v, st.pt); p && *p > 0) st.pt = *p;
        } else if (k == "font-weight") {
            st.bold = isBoldWeight(v);
        } else if (k == "font-style") {
            string t = lower(trim(v));
            st.italic = t == "italic" || t == "oblique";
        } else if (k == "line-height") {
            st.lh = parseLineHeight(v);
        }
    }
}

Decls parseDecls(string_view body) {
    Decls out;
    size_t p = 0;
    while (p <= body.size()) {
        size_t e = body.find(';', p);
        string_view d = body.substr(p, (e == string_view::npos ? body.size() : e) - p);
        p = e == string_view::npos ? body.size() + 1 : e + 1;
        size_t c = d.find(':');
        if (c == string_view::npos) continue;
        string k = lower(trim(d.substr(0, c)));
        string v(trim(d.substr(c + 1)));
        string lv = lower(v);
        if (lv.size() >= 10 && lv.compare(lv.size() - 10, 10, "!important") == 0)
            v = string(trim(string_view(v).substr(0, v.size() - 10)));
        if (!k.empty() && !v.empty()) out.emplace_back(k, v);
    }
    return out;
}

void parseSheet(string_view css, std::vector<Rule>& out) {
    while (!css.empty()) {
        size_t open = css.find('{');
        if (open == string_view::npos) break;
        size_t close = css.find('}', open);
        if (close == string_view::npos) break;
        string_view sels = css.substr(0, open);
        Decls d = parseDecls(css.substr(open + 1, close - open - 1));
        css = css.substr(close + 1);
        size_t q = 0;
        while (q <= sels.size()) {
            size_t e = sels.find(',', q);
            string_view sel = trim(sels.substr(q, (e == string_view::npos ? sels.size() : e) - q));
            q = e == string_view::npos ? sels.size() + 1 : e + 1;
            if (!sel.empty()) out.push_back(Rule{lower(sel), d});
        }
    }
}

// ---- markup ------------------------------------------------------------------------------------------------------

enum class TokKind { Text, Br, Para };
struct Token {
    TokKind kind;
    string text;
    Style style;
};

std::map<string, string> parseAttrs(string_view rest) {
    std::map<string, string> a;
    size_t i = 0;
    while (i < rest.size()) {
        while (i < rest.size() &&
               !((rest[i] >= 'a' && rest[i] <= 'z') || (rest[i] >= 'A' && rest[i] <= 'Z') || rest[i] == '-'))
            i++;
        size_t ks = i;
        while (i < rest.size() &&
               ((rest[i] >= 'a' && rest[i] <= 'z') || (rest[i] >= 'A' && rest[i] <= 'Z') || rest[i] == '-'))
            i++;
        string key = lower(rest.substr(ks, i - ks));
        size_t j = i;
        while (j < rest.size() && isWs(rest[j])) j++;
        if (key.empty() || j >= rest.size() || rest[j] != '=') continue;
        j++;
        while (j < rest.size() && isWs(rest[j])) j++;
        if (j >= rest.size()) break;
        string val;
        if (rest[j] == '\'' || rest[j] == '"') {
            size_t e = rest.find(rest[j], j + 1);
            if (e == string_view::npos) e = rest.size();
            val = string(rest.substr(j + 1, e - j - 1));
            i = e + 1;
        } else {
            size_t e = j;
            while (e < rest.size() && !isWs(rest[e]) && rest[e] != '>') e++;
            val = string(rest.substr(j, e - j));
            i = e;
        }
        a[key] = val;
    }
    return a;
}

bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::vector<Token> tokenize(string_view markup, const std::vector<Rule>& sheet) {
    // Stock 516.1689: a <style> element adds its rules and shows nothing, a rule may
    // select a tag or tag.class as well as .class, a closing tag that matches no open one is ignored, <a> is blue
    // and <center> centres.
    std::vector<Rule> rules = sheet;
    Style base;
    for (const Rule& r : rules)
        if (r.sel == "body") applyProps(base, r.decls);
    std::vector<Style> stack{base};
    std::vector<string> open;  // the tag of each style above the base
    std::vector<Token> toks;
    auto text = [&](string_view s) { toks.push_back({TokKind::Text, decodeEntities(s), stack.back()}); };
    size_t i = 0;
    while (i < markup.size()) {
        size_t lt = markup.find('<', i);
        if (lt == string_view::npos) {
            text(markup.substr(i));
            break;
        }
        if (lt > i) text(markup.substr(i, lt - i));
        // A '<' that does not open a tag (e.g. "<<<<") is literal text.
        if (lt + 1 >= markup.size() || !(isAlpha(markup[lt + 1]) || markup[lt + 1] == '/' || markup[lt + 1] == '!')) {
            text("<");
            i = lt + 1;
            continue;
        }
        size_t gt = markup.find('>', lt);
        if (gt == string_view::npos) {
            text(markup.substr(lt));
            break;
        }
        string_view tag = trim(markup.substr(lt + 1, gt - lt - 1));
        i = gt + 1;
        bool closing = !tag.empty() && tag[0] == '/';
        if (closing) tag.remove_prefix(1);
        size_t n = 0;
        while (n < tag.size() && (isAlpha(tag[n]) || (tag[n] >= '0' && tag[n] <= '9'))) n++;
        if (n == 0) continue;
        string name = lower(tag.substr(0, n));
        if (name == "br") {
            toks.push_back({TokKind::Br, {}, stack.back()});
            continue;
        }
        if (name == "p") toks.push_back({TokKind::Para, {}, stack.back()});  // <p> breaks the line; <div> does not
        if (name == "img" || name == "beep") continue;                       // <IMG> measures 0
        if (closing) {
            for (size_t k = open.size(); k-- > 0;)
                if (open[k] == name) {
                    open.resize(k);
                    stack.resize(k + 1);
                    break;
                }
            continue;
        }
        if (name == "style") {
            size_t end = i;
            for (;; end++) {
                end = markup.find('<', end);
                if (end == string_view::npos || lower(markup.substr(end, 8)) == "</style>") break;
            }
            parseSheet(markup.substr(i, end == string_view::npos ? string_view::npos : end - i), rules);
            i = end == string_view::npos ? markup.size() : end + 8;
            continue;
        }
        auto attrs = parseAttrs(tag.substr(n));
        Style st = stack.back();
        if (auto a = attrs.find("valign"); a != attrs.end()) st.valign = parseValign(a->second, st.valign);
        for (const Rule& r : rules)
            if (r.sel == name) applyProps(st, r.decls);
        if (name == "a")
            st.color = 0x0000ffffu;
        else if (name == "center")
            st.align = 1;
        if (name == "u") st.underline = true;
        if (name == "b" || name == "strong")
            st.bold = true;
        else if (name == "i" || name == "em")
            st.italic = true;
        else if (name == "tt" || name == "code" || name == "pre" || name == "kbd" || name == "samp")
            st.family = "monospace";
        else if (name == "big")
            st.pt = int(st.pt * 1.25);
        else if (name == "small")
            st.pt = int(st.pt * 0.8);
        else if (name == "font") {
            if (auto f = attrs.find("face"); f != attrs.end()) applyProps(st, {{"font-family", f->second}});
            if (auto f = attrs.find("color"); f != attrs.end()) applyProps(st, {{"color", f->second}});
            if (auto z = attrs.find("size"); z != attrs.end()) {
                string_view zs = trim(z->second);
                bool rel = !zs.empty() && (zs[0] == '+' || zs[0] == '-');
                int v = 0;
                try {
                    v = std::stoi(string(zs));
                } catch (...) { zs = {}; }
                if (!zs.empty()) {
                    // size=7 is 48pt anywhere. Relative sizes count from the tier at or below the current size.
                    int tier = 1;
                    for (int t = 1; t <= 7; t++)
                        if (kTierPt[t] <= st.pt) tier = t;
                    int to = std::clamp(rel ? tier + v : v, 1, 7);
                    st.pt = kTierPt[to];
                }
            }
        }
        if (auto c = attrs.find("class"); c != attrs.end()) {
            std::vector<string> cls;
            string_view cs = c->second;
            size_t p = 0;
            while (p < cs.size()) {
                while (p < cs.size() && isWs(cs[p])) p++;
                size_t e = p;
                while (e < cs.size() && !isWs(cs[e])) e++;
                if (e > p) {
                    cls.push_back("." + lower(cs.substr(p, e - p)));
                    cls.push_back(name + cls.back());
                }
                p = e;
            }
            // CSS cascade: stylesheet order decides, not the order of the class attribute.
            for (const Rule& r : rules)
                if (std::find(cls.begin(), cls.end(), r.sel) != cls.end()) applyProps(st, r.decls);
        }
        if (auto s = attrs.find("style"); s != attrs.end()) applyProps(st, parseDecls(s->second));
        stack.push_back(st);
        open.push_back(name);
    }
    return toks;
}

// ---- faces -------------------------------------------------------------------------------------------------------

std::mutex g_mu;
TrueTypeResolver g_resolver;
std::map<std::tuple<string, int, bool, bool>, std::shared_ptr<TrueTypeFace>> g_ttCache;

// Stock DreamSeeker: em = round(pt * 4/3), except that MeasureText answers pt 9 like pt 10 (drawing uses em 12).
int emOfPt(int pt, bool draw) {
    if (pt == 9 && !draw) return 13;
    return std::max(1, roundHalfUp(pt * 4.0 / 3.0));
}

struct Face {
    const GdiStrike* bm = nullptr;
    int k = 1;
    bool bold = false;
    std::shared_ptr<TrueTypeFace> tt;
    int ascent = 0, height = 0;
    DsFace desc;  // what the painter asks its glyph rasterizer for (render/maptextpaint.h)

    int adv(char32_t cp) const {
        if (tt) return tt->advance ? tt->advance(cp) : 0;
        int c = cp1252(cp);
        if (c < bm->first || c > 255) c = bm->defaultChar;
        int one = bm->adv[c - bm->first] * k + (bold ? 1 : 0);  // a bitmap bold adds 1 px per character
        return cp > 0xFFFF ? 2 * one : one;                     // a supplementary character is two default characters
    }
    int overhang(char32_t cp) const { return tt && tt->overhang ? tt->overhang(cp) : 0; }
    int defaultAdv() const {
        if (tt) return adv(0xFFFD);
        return bm->adv[bm->defaultChar - bm->first] * k + (bold ? 1 : 0);
    }
};

// GDI font mapping. Windows: stock asks for its faces with raster scaling off (what PROOF_QUALITY does), so a size
// is drawn from one strike as it is, the one GDI's mapper charges least: 150 for each pixel its character height is
// short of the request, 600 and 150 a pixel when it is taller, 30 for a strike of the 120-dpi files. Past 10000, that
// is 67 px short of the largest strike, GDI hands out Arial: nullptr. (The same choice as GDI's for every size up to
// 400 px in all three families on Windows 11; stock 516.1689: 16pt is the 25 px strike,
// 22pt the 29 px one and not the 36 px one a pixel taller, 29pt the 46 px one a pixel taller, 60px still that.)
// Wine: the largest strike whose character height fits the request, else the smallest; then integer scaling.
const GdiStrike* mapStrike(Bitmap b, int em, GdiProfile prof) {
    auto ss = strikesOf(b, prof);
    const GdiStrike* best = ss.front();
    if (prof == GdiProfile::Windows) {
        int least = -1;
        for (const GdiStrike* s : ss) {
            const int height = s->h - s->il;
            const int penalty = (height > em ? 600 + 150 * (height - em) : 150 * (em - height)) + (s->dpi == 96 ? 0 : 30);
            if (least < 0 || penalty < least) least = penalty, best = s;
        }
        return least > 10000 ? nullptr : best;
    }
    for (const GdiStrike* s : ss)
        if (s->h - s->il <= em) best = s;
    return best;
}

Face bitmapFace(Bitmap b, int em, bool bold, GdiProfile prof) {
    const GdiStrike* best = mapStrike(b, em, prof);
    if (!best) best = strikesOf(b, prof).back();  // only without a TrueType stack to draw Arial with
    Face f;
    f.bm = best;
    f.k = prof == GdiProfile::Windows ? 1 : std::max(1, (em + 2 * best->il) / best->h);
    f.bold = bold;
    f.ascent = best->ascent * f.k;
    f.height = best->h * f.k;
    f.desc.family = b == Bitmap::SmallFonts ? "small fonts" : b == Bitmap::Courier ? "courier" : "ms sans serif";
    f.desc.em = em;
    f.desc.bold = bold;
    f.desc.bitmap = true;
    f.desc.ascent = f.ascent;
    f.desc.height = f.height;
    return f;
}

Face resolveFace(const Style& st, GdiProfile prof, bool draw) {
    int em = emOfPt(st.pt, draw);
    string fam = st.family;
    auto bitmap = [&](Bitmap b) {
        Face f = bitmapFace(b, em, st.bold, prof);
        f.desc.italic = st.italic;
        return f;
    };
    const Bitmap named = fam.empty() || fam == "ms sans serif" || fam == "sans-serif" ? Bitmap::MsSansSerif
                         : fam == "small fonts"                                       ? Bitmap::SmallFonts
                         : fam == "monospace" || fam == "courier"                     ? Bitmap::Courier
                                                                                      : Bitmap::None;
    if (named != Bitmap::None) {
        if (mapStrike(named, em, prof)) return bitmap(named);
        fam = "arial";  // a size no strike is near
    }
    // A TrueType face. An unknown family (and `serif`) measures as Arial.
    if (g_resolver) {
        for (const string& want : {fam == "serif" ? string("arial") : fam, string("arial")}) {
            auto key = std::make_tuple(want, em, st.bold, st.italic);
            auto it = g_ttCache.find(key);
            if (it == g_ttCache.end()) {
                auto tf = std::make_shared<TrueTypeFace>();
                if (!g_resolver(want, em, st.bold, st.italic, *tf)) tf.reset();
                it = g_ttCache.emplace(key, tf).first;
            }
            if (it->second) {
                Face f;
                f.tt = it->second;
                f.ascent = it->second->ascent;
                f.height = it->second->ascent + it->second->descent;
                f.desc = DsFace{want, em, st.bold, st.italic, false, f.ascent, f.height};
                return f;
            }
        }
    }
    return bitmap(Bitmap::MsSansSerif);  // no TrueType stack: DreamSeeker's default face
}

// ---- layout ------------------------------------------------------------------------------------------------------

constexpr char32_t kTabAtStart = 0x110000;  // a tab opening a line advances by the default char only

struct Cell {
    char32_t cp = 0;
    int face = 0;
    int linked = -1;  // the face this character is drawn and advanced in when its own, a bitmap one, has no glyph for it
    double lh = 1.2;
    bool marker = false;  // carries the style in effect where a line starts (an empty line still has a height)
    int paint = 0;        // index into Built::paints (colour, outline, align): paint only, never a measurement
};
using Line = std::vector<Cell>;

bool isBreakSpace(char32_t c) { return c == ' ' || c == '\t' || c == 0xa0 || c == kTabAtStart; }

int advOf(const Cell& c, const std::vector<Face>& faces) {
    if (c.marker) return 0;
    const Face& f = faces[size_t(c.linked >= 0 ? c.linked : c.face)];
    if (c.cp == kTabAtStart) return f.defaultAdv();
    if (c.cp == '\t') return f.adv(' ') + f.defaultAdv();  // a tab is a space plus the default char
    return f.adv(c.cp);
}
int widthOf(const Line& l, const std::vector<Face>& faces) {
    int w = 0;
    for (const Cell& c : l) w += advOf(c, faces);
    return w;
}
bool hasGlyph(const Line& l) {
    for (const Cell& c : l)
        if (!c.marker && !isBreakSpace(c.cp)) return true;
    return false;
}
bool hasAny(const Line& l) {
    for (const Cell& c : l)
        if (!c.marker) return true;
    return false;
}

struct OutLine {
    int w = 0;
    Line cells;
};

// One hard line, unwrapped: the sum of advances. A trailing run of n plain spaces or tabs counts n-1 of them.
OutLine layoutUnwrapped(Line l, const std::vector<Face>& faces) {
    Line real;
    for (const Cell& c : l)
        if (!c.marker) real.push_back(c);
    if (real.empty()) return OutLine{0, l.empty() ? l : Line{l.front()}};
    if (real.front().cp == '\t') real.front().cp = kTabAtStart;
    size_t j = real.size();
    while (j > 0 && (real[j - 1].cp == ' ' || real[j - 1].cp == '\t')) j--;
    int w = 0;
    size_t upto = j < real.size() ? real.size() - 1 : real.size();
    for (size_t i = 0; i < upto; i++) w += advOf(real[i], faces);
    return OutLine{w, real};
}

std::vector<OutLine> wrapLine(const Line& chars, int width, const std::vector<Face>& faces) {
    // Items: runs of break-spaces, and words (split after '-', not '/').
    struct Item {
        bool space;
        Line cells;
    };
    std::vector<Item> items;
    Line cur;
    int curKind = -1;
    Line markers;
    for (const Cell& c : chars) {
        if (c.marker) markers.push_back(c);
        int k = isBreakSpace(c.cp) && !c.marker ? 1 : 0;
        if (k != curKind && !cur.empty()) {
            items.push_back({curKind == 1, cur});
            cur.clear();
        }
        cur.push_back(c);
        curKind = k;
        if (!c.marker && c.cp == '-' && k == 0) {
            items.push_back({false, cur});
            cur.clear();
            curKind = -1;
        }
    }
    if (!cur.empty()) items.push_back({curKind == 1, cur});

    std::vector<OutLine> out;
    Line line, pend;
    int lw = 0;
    auto flush = [&] {
        out.push_back(OutLine{lw, line});
        line.clear();
        lw = 0;
        pend.clear();
    };
    for (const Item& it : items) {
        if (it.space) {
            pend.insert(pend.end(), it.cells.begin(), it.cells.end());
            continue;
        }
        int ww = widthOf(it.cells, faces);
        int pw = widthOf(pend, faces);
        bool has = hasGlyph(line);
        if (has && lw + pw + ww > width) {
            flush();
            pw = 0;
        }
        if (!has && lw + pw + ww > width) {
            pend.clear();
            pw = 0;
        }  // leading spaces go when the word would overflow
        line.insert(line.end(), pend.begin(), pend.end());
        lw += pw;
        pend.clear();
        if (ww > width && !hasGlyph(line)) {
            // A word wider than the whole width: cut by character, greedily.
            for (const Cell& c : it.cells) {
                int a = advOf(c, faces);
                if (!line.empty() && lw + a > width && hasAny(line)) flush();
                line.push_back(c);
                lw += a;
            }
            continue;
        }
        line.insert(line.end(), it.cells.begin(), it.cells.end());
        lw += ww;
    }
    if (!pend.empty() && line.empty()) line = pend;
    if (line.empty()) line = markers.empty() ? (chars.empty() ? Line{} : Line{chars.front()}) : Line{markers.front()};
    out.push_back(OutLine{lw, line});
    return out;
}

// One layout, read by both the MeasureText reply (measureLocked) and the map painter (dsLayoutMaptext), so a box
// sized from the reply holds exactly the lines drawn.
struct Built {
    bool ok = false;  // false = "cannot measure" (empty text)
    bool anyGlyph = false;
    std::vector<Face> faces;
    std::vector<DsPaint> paints;
    std::vector<int> paintAlign;
    std::vector<int> paintValign;
    std::vector<OutLine> out;
};

Built buildLocked(string_view text, string_view mapStyle, string_view style, int width, GdiProfile prof, bool draw) {
    Built b;
    if (text.empty()) return b;  // DreamSeeker's "cannot measure" for empty text
    std::vector<Rule> rules;
    parseSheet(mapStyle, rules);  // the map control's sheet first, then the call's own
    parseSheet(style, rules);
    std::vector<Token> toks = tokenize(text, rules);

    std::vector<Face>& faces = b.faces;
    std::map<std::tuple<string, int, bool, bool>, int> faceIdx;
    auto faceOf = [&](const Style& st) {
        auto key = std::make_tuple(st.family, st.pt, st.bold, st.italic);
        auto it = faceIdx.find(key);
        if (it != faceIdx.end()) return it->second;
        faces.push_back(resolveFace(st, prof, draw));
        return faceIdx[key] = int(faces.size()) - 1;
    };
    using PaintKey = std::tuple<uint32_t, bool, uint32_t, int, int, int, bool, std::tuple<bool, int, int, int, uint32_t>, int>;
    std::map<PaintKey, int> paintIdx;
    auto paintOf = [&](const Style& st) {
        const bool outline = st.outline && st.outlineWidth > 0;
        PaintKey key{st.color, outline, st.outlineColor, st.align, st.valign, st.outlineWidth, st.underline,
                     {st.shadow, st.shadowX, st.shadowY, st.shadowBlur, st.shadowColor}, st.outlineFlags};
        auto it = paintIdx.find(key);
        if (it != paintIdx.end()) return it->second;
        b.paints.push_back(DsPaint{st.color, outline, st.outlineColor, st.outlineWidth, st.underline, st.shadow,
                                   st.shadowX, st.shadowY, st.shadowBlur, st.shadowColor, st.outlineFlags});
        b.paintAlign.push_back(st.align);
        b.paintValign.push_back(st.valign);
        return paintIdx[key] = int(b.paints.size()) - 1;
    };
    auto marker = [&](const Style& st) {
        Cell c;
        c.marker = true;
        c.face = faceOf(st);
        c.lh = st.lh;
        c.paint = paintOf(st);
        return c;
    };

    // Hard lines. kinds: 0 start, 1 <br>/<p>, 2 a newline character.
    std::vector<Line> lines{Line{marker(toks.empty() ? Style{} : toks.front().style)}};
    std::vector<int> ends{0};
    auto emptyLine = [](const Line& l) {
        for (const Cell& c : l)
            if (!c.marker) return false;
        return true;
    };
    int block = toks.empty() ? 0 : toks.front().style.block;
    for (const Token& t : toks) {
        if (t.kind == TokKind::Text && t.style.block != block) {  // into or out of an element that set text-align
            block = t.style.block;
            if (!emptyLine(lines.back())) {
                lines.push_back(Line{marker(t.style)});
                ends.push_back(1);
            } else
                lines.back() = Line{marker(t.style)};
        }
        if (t.kind != TokKind::Text) {
            if (t.kind == TokKind::Para && emptyLine(lines.back())) continue;  // <p></p><p>: one break, not two
            lines.push_back(Line{marker(t.style)});
            ends.push_back(1);
            continue;
        }
        std::u32string cps = decodeUtf8(t.text);
        for (size_t i = 0; i < cps.size(); i++) {
            char32_t c = cps[i];
            if (c == '\r') {
                if (i + 1 < cps.size() && cps[i + 1] == '\n') continue;
                c = '\n';
            }
            if (c == '\n') {
                lines.push_back(Line{marker(t.style)});
                ends.push_back(2);
                continue;
            }
            Cell cell;
            cell.cp = c;
            cell.face = faceOf(t.style);
            // A character outside a bitmap face's code page is not that face's default box in stock: GDI links it from
            // Microsoft Sans Serif at the size asked, on the bitmap face's line (stock 516.1689:
            // 8pt, 16pt and a TrueType face give the same pixels). At some sizes stock's linked glyphs are smaller than
            // asked, and a few symbols come from yet another face; for those this is an approximation.
            if (faces[size_t(cell.face)].bm && c > 0xff && c <= 0xffff && cp1252(c) < 0) {
                Style link = t.style;
                link.family = "microsoft sans serif";
                const int linked = faceOf(link);
                if (faces[size_t(linked)].tt) cell.linked = linked;
            }
            cell.lh = t.style.lh;
            cell.paint = paintOf(t.style);
            lines.back().push_back(cell);
        }
    }
    // A leading <br> is ignored, but a leading newline is not. Trailing empty lines of either kind are ignored.
    if (lines.size() > 1 && emptyLine(lines[0]) && ends[1] == 1) {
        lines.erase(lines.begin());
        ends.erase(ends.begin());
    }
    while (lines.size() > 1 && emptyLine(lines.back())) {
        lines.pop_back();
        ends.pop_back();
    }

    for (const Line& l : lines)
        for (const Cell& c : l)
            if (!c.marker && c.cp != ' ' && c.cp != '\t') b.anyGlyph = true;
    b.ok = true;
    if (!b.anyGlyph) return b;

    for (const Line& l : lines) {
        if (width <= 0)
            b.out.push_back(layoutUnwrapped(l, faces));
        else
            for (OutLine& o : wrapLine(l, width, faces)) b.out.push_back(std::move(o));
    }
    return b;
}

// One output line's vertical metrics: its ascent, descent and the pitch to the next line. false = no cells at all.
bool lineMetrics(const OutLine& o, const std::vector<Face>& faces, int& asc, int& desc, int& pitch) {
    Line use;
    for (const Cell& c : o.cells)
        if (!c.marker) use.push_back(c);
    if (use.empty()) use = o.cells;
    if (use.empty()) return false;
    asc = desc = pitch = 0;
    for (const Cell& c : use) {
        const Face& f = faces[size_t(c.face)];
        asc = std::max(asc, f.ascent);
        desc = std::max(desc, f.height - f.ascent);
        pitch = std::max(pitch, int(std::floor(c.lh * f.ascent + 1e-9)));
    }
    return true;
}

DsMeasure measureLocked(string_view text, string_view mapStyle, string_view style, int width, GdiProfile prof) {
    DsMeasure r;
    Built b = buildLocked(text, mapStyle, style, width, prof, false);
    if (!b.ok) return r;
    r.ok = true;
    if (!b.anyGlyph) {  // a line with no glyphs measures 0x0 (or width x 0)
        r.width = width > 0 ? width : 0;
        return r;
    }
    int w = 0, h = 0;
    for (size_t i = 0; i < b.out.size(); i++) {
        const OutLine& o = b.out[i];
        w = std::max(w, o.w);
        int asc, desc, pitch;
        if (!lineMetrics(o, b.faces, asc, desc, pitch)) continue;
        if (i + 1 == b.out.size())
            h += (width > 0 && o.w > width) ? 0 : asc + desc;  // a last line wider than the width adds no height
        else
            h += pitch;
    }
    r.width = width > 0 ? width : w;  // with a width, the reply's width is that width
    r.height = h;
    return r;
}

// The painter's view of the same layout, at the drawn em: drawn text spans what MeasureText reported (pt 9 aside).
DsLayout layoutLocked(string_view text, string_view mapStyle, int width, GdiProfile prof) {
    DsLayout L;
    Built b = buildLocked(text, mapStyle, {}, width, prof, true);
    if (!b.ok || !b.anyGlyph) return L;
    L.ok = true;
    for (const Face& f : b.faces) L.faces.push_back(f.desc);
    L.paints = b.paints;
    bool valignSet = false;
    int top = 0;
    for (size_t i = 0; i < b.out.size(); i++) {
        const OutLine& o = b.out[i];
        int asc, desc, pitch;
        if (!lineMetrics(o, b.faces, asc, desc, pitch)) continue;
        DsLine ln;
        ln.top = top;
        ln.ascent = asc;
        ln.height = asc + desc;
        ln.width = o.w;
        bool alignSet = false;
        int x = 0;
        bool run = false;  // the cell before was a linked glyph too
        for (const Cell& c : o.cells) {
            const int advance = advOf(c, b.faces);
            if (c.marker) continue;
            // The line takes the alignment of its first run, and the block the vertical alignment of its first glyph.
            if (!alignSet) {
                ln.align = b.paintAlign[size_t(c.paint)];
                alignSet = true;
            }
            if (!valignSet && !isBreakSpace(c.cp)) {
                L.valign = b.paintValign[size_t(c.paint)];
                valignSet = true;
            }
            if (!isBreakSpace(c.cp) && c.cp != '\t')
                ln.glyphs.push_back(DsGlyph{c.cp, x, c.linked >= 0 ? c.linked : c.face, c.paint, run && c.linked >= 0});
            run = c.linked >= 0;
            if (b.paints[size_t(c.paint)].underline) ln.underlines.push_back(DsUnderline{x, advance, c.face, c.paint});
            x += advance;
        }
        if (!alignSet && !o.cells.empty()) ln.align = b.paintAlign[size_t(o.cells.front().paint)];
        for (auto it = o.cells.rbegin(); it != o.cells.rend(); ++it) {
            if (it->marker) continue;
            if (!isBreakSpace(it->cp)) ln.overhang = b.faces[size_t(it->face)].overhang(it->cp);
            break;
        }
        L.lines.push_back(std::move(ln));
        if (i + 1 == b.out.size())
            // Drawn, the last line is as tall as its pitch when that is more: with line-height 2 the block ends
            // floor(2 * ascent) below the last line's top (stock 516.1689).
            top += (width > 0 && o.w > width) ? 0 : std::max(asc + desc, pitch);
        else
            top += pitch;
    }
    L.width = width > 0 ? width : 0;
    for (const DsLine& ln : L.lines)
        if (width <= 0) L.width = std::max(L.width, ln.width);
    L.height = top;
    return L;
}

}  // namespace

int cp1252Byte(char32_t cp) { return cp1252(cp); }

int dsBitmapAdvance(const DsFace& face, int b, GdiProfile profile) {
    if (!face.bitmap) return -1;
    const Bitmap bm = face.family == "small fonts" ? Bitmap::SmallFonts
                      : face.family == "courier"   ? Bitmap::Courier
                                                   : Bitmap::MsSansSerif;
    const GdiStrike* s = bitmapFace(bm, face.em, false, profile).bm;
    if (b < 0) b = s->defaultChar;
    return b < s->first || b > 255 ? -1 : s->adv[b - s->first];
}

void setTrueTypeResolver(TrueTypeResolver r) {
    std::lock_guard<std::mutex> g(g_mu);
    g_resolver = std::move(r);
    g_ttCache.clear();
}

DsMeasure dsMeasureText(string_view text, string_view mapStyle, string_view style, int width, GdiProfile profile) {
    std::lock_guard<std::mutex> g(g_mu);
    return measureLocked(text, mapStyle, style, width, profile);
}

DsLayout dsLayoutMaptext(string_view text, string_view mapStyle, int width, GdiProfile profile) {
    std::lock_guard<std::mutex> g(g_mu);
    return layoutLocked(text, mapStyle, width, profile);
}

MeasureTextAnswer dsMeasureTextAnswer(string_view text, string_view mapStyle, string_view style, int width,
                                      GdiProfile profile) {
    DsMeasure m = dsMeasureText(text, mapStyle, style, width, profile);
    if (!m.ok || m.width > 0xffff || m.height > 0xffff || m.width < 0 || m.height < 0) return {0xffff, 0xffff};
    return {uint16_t(m.width), uint16_t(m.height)};
}

namespace {
Bitmap bitmapOf(string_view family) {
    return family == "ms sans serif" ? Bitmap::MsSansSerif
           : family == "small fonts" ? Bitmap::SmallFonts
           : family == "courier"     ? Bitmap::Courier
                                     : Bitmap::None;
}
}  // namespace

int dsBitmapCharHeight(string_view family, int em, GdiProfile profile) {
    Bitmap b = bitmapOf(family);
    if (b == Bitmap::None) return 0;
    Face f = bitmapFace(b, std::max(1, em), false, profile);
    return (f.bm->h - f.bm->il) * f.k;
}

DsFace dsBitmapFace(string_view family, int em, bool bold, GdiProfile profile) {
    Bitmap b = bitmapOf(family);
    if (b == Bitmap::None) return {};
    return bitmapFace(b, std::max(1, em), bold, profile).desc;
}

int dsBitmapAdvancePx(const DsFace& face, char32_t cp, GdiProfile profile) {
    Bitmap b = bitmapOf(face.family);
    if (!face.bitmap || b == Bitmap::None) return 0;
    return bitmapFace(b, std::max(1, face.em), face.bold, profile).adv(cp);
}

string dsMeasureTextReplyBody(const MeasureTextRequest& req, string_view mapStyle, GdiProfile profile) {
    MeasureTextAnswer a = dsMeasureTextAnswer(req.text, mapStyle, req.style, req.width, profile);
    string o(8, '\0');
    o[0] = char(a.width & 0xff);
    o[1] = char(a.width >> 8);
    o[2] = char(a.height & 0xff);
    o[3] = char(a.height >> 8);
    for (int i = 0; i < 4; i++) o[4 + i] = char((req.id >> (8 * i)) & 0xff);
    return o;
}

}  // namespace fugue::render
