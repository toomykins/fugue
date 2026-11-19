// See maptext.h.
#include "render/maptext.h"
#include "render/parsedouble.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "render/font.h"

namespace fugue::render {
namespace {

using std::string;
using std::string_view;

constexpr int kBaseSize = 11;
constexpr int kCssBase = 7;

// ---- string-library semantics -----------------------------------------------------------------------------

bool isUniSpace(char32_t r) {
    switch (r) {
        case '\t':
        case '\n':
        case '\v':
        case '\f':
        case '\r':
        case ' ':
        case 0x85:
        case 0xA0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202f:
        case 0x205f:
        case 0x3000: return true;
    }
    return r >= 0x2000 && r <= 0x200a;
}
bool isSpaceByte(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

DecodedRune decodeLastRune(string_view s, size_t end) {  // The rune that ends just before offset `end`.
    if (end == 0) return {0xFFFD, 0};
    long start = long(end) - 1;
    if (static_cast<unsigned char>(s[size_t(start)]) < 0x80) return {static_cast<unsigned char>(s[size_t(start)]), 1};
    long lim = long(end) - 4;
    if (lim < 0) lim = 0;
    for (start--; start >= lim; start--)
        if ((static_cast<unsigned char>(s[size_t(start)]) & 0xC0) != 0x80) break;
    if (start < 0) start = 0;
    DecodedRune d = decodeRune(s.substr(0, end), size_t(start));
    if (size_t(start) + size_t(d.size) != end) return {0xFFFD, 1};
    return d;
}

string_view trimSpace(string_view s) {
    size_t a = 0, b = s.size();
    while (a < b) {
        DecodedRune d = decodeRune(s.substr(0, b), a);
        if (!isUniSpace(d.r)) break;
        a += size_t(d.size);
    }
    while (b > a) {
        DecodedRune d = decodeLastRune(s.substr(0, b), b);
        if (!isUniSpace(d.r)) break;
        b -= size_t(d.size);
    }
    return s.substr(a, b - a);
}

std::vector<string_view> fields(string_view s) {
    std::vector<string_view> out;
    size_t i = 0, startF = string_view::npos;
    while (i < s.size()) {
        DecodedRune d = decodeRune(s, i);
        if (isUniSpace(d.r)) {
            if (startF != string_view::npos) {
                out.push_back(s.substr(startF, i - startF));
                startF = string_view::npos;
            }
        } else if (startF == string_view::npos)
            startF = i;
        i += size_t(d.size);
    }
    if (startF != string_view::npos) out.push_back(s.substr(startF));
    return out;
}

string toLower(string_view s) {
    string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            o.push_back((c >= 'A' && c <= 'Z') ? char(c + 32) : char(c));
            i++;
            continue;
        }
        DecodedRune d = decodeRune(s, i);
        if (d.r == 0x212A && d.size == 3)
            o.push_back('k');
        else if (d.r == 0x130 && d.size == 2)
            o.push_back('i');
        else
            o.append(s.substr(i, size_t(d.size)));
        i += size_t(d.size);
    }
    return o;
}

string_view trimLeftWs(string_view s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    return s.substr(a);
}
bool hasSuffix(string_view s, string_view x) { return s.size() >= x.size() && s.substr(s.size() - x.size()) == x; }
string_view trimSuffix(string_view s, string_view x) { return hasSuffix(s, x) ? s.substr(0, s.size() - x.size()) : s; }

// Base-10 integer with optional sign, int64 range.
bool goAtoi(string_view s, int64_t& out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i == s.size()) return false;
    uint64_t v = 0;
    const uint64_t limit = uint64_t(INT64_MAX) + unsigned(neg);
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        unsigned digit = unsigned(s[i] - '0');
        if (v > (limit - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = neg ? (v == uint64_t(INT64_MAX) + 1 ? INT64_MIN : -int64_t(v)) : int64_t(v);
    return true;
}

// float64 -> int64 as the x86-64 conversion does it: anything outside int64 is INT64_MIN.
int64_t goInt(double x) {
    if (!(x > -9223372036854775808.0 && x < 9223372036854775808.0)) return INT64_MIN;
    return static_cast<int64_t>(x);
}

// Float parse restricted to the alphabet [0-9.+-] (the only characters the CSS font-size parser passes).
bool goParseFloat(string_view s, double& out) {
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = s[i] == '-';
        i++;
    }
    size_t digits = 0, dots = 0;
    string body;
    for (; i < s.size(); i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            digits++;
            body.push_back(s[i]);
        } else if (s[i] == '.' && dots == 0) {
            dots++;
            body.push_back('.');
        } else
            return false;
    }
    if (digits == 0) return false;
    if (body.back() == '.') body.pop_back();  // "5." is legal
    if (body.front() == '.') body.insert(body.begin(), '0');
    double v = 0;
    auto r = parseDouble(body.data(), body.data() + body.size(), v);
    if (r.ec == std::errc::result_out_of_range) {
        // overflow (a nonzero integer part) is a range error; underflow reads as a tiny positive.
        bool bigInt = false;
        for (char c : body) {
            if (c == '.') break;
            if (c != '0') {
                bigInt = true;
                break;
            }
        }
        if (bigInt) return false;
        v = 4.9e-324;
    } else if (r.ec != std::errc() || r.ptr != body.data() + body.size()) {
        return false;
    }
    out = neg ? -v : v;
    return true;
}

// ---- entities --------------------------------------------------------------------------------------------------

void appendRune(string& o, char32_t r) {
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

string decodeEntities(string_view s) {
    if (s.find('&') == string_view::npos) return string(s);
    string b;
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            b.push_back(s[i]);
            i++;
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == string_view::npos || semi - i > 10) {
            b.push_back(s[i]);
            i++;
            continue;
        }
        size_t rel = semi - i;
        string_view ent = s.substr(i + 1, rel - 1);
        string low = toLower(ent);
        bool done = true;
        if (low == "nbsp")
            b.push_back(' ');
        else if (low == "amp")
            b.push_back('&');
        else if (low == "lt")
            b.push_back('<');
        else if (low == "gt")
            b.push_back('>');
        else if (low == "quot")
            b.push_back('"');
        else if (low == "apos")
            b.push_back('\'');
        else
            done = false;
        if (!done) {
            bool ok = false;
            if (!ent.empty() && ent[0] == '#') {
                string_view num = ent.substr(1);
                int base = 10;
                if (!num.empty() && (num[0] == 'x' || num[0] == 'X')) {
                    num = num.substr(1);
                    base = 16;
                }
                // Optional sign, at least one digit of the base, 32-bit range.
                size_t k = 0;
                bool neg = false;
                if (k < num.size() && (num[k] == '+' || num[k] == '-')) {
                    neg = num[k] == '-';
                    k++;
                }
                if (k < num.size()) {
                    int64_t n = 0;
                    bool good = true;
                    for (; k < num.size(); k++) {
                        int d = -1;
                        char c = num[k];
                        if (c >= '0' && c <= '9')
                            d = c - '0';
                        else if (base == 16 && c >= 'a' && c <= 'f')
                            d = c - 'a' + 10;
                        else if (base == 16 && c >= 'A' && c <= 'F')
                            d = c - 'A' + 10;
                        if (d < 0) {
                            good = false;
                            break;
                        }
                        n = n * base + d;  // <= 9 chars: cannot overflow int64
                    }
                    if (neg) n = -n;
                    if (good && n > 0 && n < 0x110000) {
                        appendRune(b, char32_t(n));
                        ok = true;
                    }
                }
            }
            if (!ok) b.append(s.substr(i, rel + 1));
        }
        i += rel + 1;
    }
    return b;
}

// ---- style ------------------------------------------------------------------------------------------------------

struct Style {
    int size = kBaseSize;
    bool bold = false, italic = false, mono = false;
};
using Props = std::map<string, string>;
using Sheet = std::map<string, Props>;

const GoFace& faceOf(const Style& st) {
    return goFace(st.size == 0 ? kBaseSize : st.size, st.bold, st.italic, st.mono);
}

int clampSize(int64_t px) {
    if (px < kFontMinSize) return kFontMinSize;
    if (px > kFontMaxSize) return kFontMaxSize;
    return int(px);
}
int sizeFromCSS(double cssPx) { return clampSize(goInt(cssPx * kBaseSize / kCssBase + 0.5)); }

bool cssIsBold(string_view v0) {
    string v = toLower(trimSpace(v0));
    for (const char* k : {"bold", "bolder", "700", "800", "900", "600"})
        if (v == k) return true;
    for (const char* k : {"normal", "lighter", "100", "200", "300", "400", "500"})
        if (v == k) return false;
    int64_t n;
    if (goAtoi(v, n)) return n >= 600;
    return false;
}

std::optional<int> parseCSSFontSize(string_view v0, int cur) {
    string v = toLower(trimSpace(v0));
    if (v.empty()) return std::nullopt;
    if (cur <= 0) cur = kBaseSize;
    auto scaleCur = [&](double f) -> std::optional<int> { return clampSize(goInt(double(cur) * f + 0.5)); };
    if (v == "xx-small") return sizeFromCSS(5);
    if (v == "x-small") return sizeFromCSS(6);
    if (v == "small") return sizeFromCSS(6);
    if (v == "medium") return sizeFromCSS(kCssBase);
    if (v == "large") return sizeFromCSS(8);
    if (v == "x-large") return sizeFromCSS(10);
    if (v == "xx-large") return sizeFromCSS(12);
    if (v == "smaller") return scaleCur(0.8);
    if (v == "larger") return scaleCur(1.25);
    string_view num = v, unit;
    for (size_t i = 0; i < v.size(); i++) {
        char c = v[i];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') continue;
        num = string_view(v).substr(0, i);
        unit = string_view(v).substr(i);
        break;
    }
    double f;
    if (!goParseFloat(trimSpace(num), f) || f <= 0) return std::nullopt;
    string_view u = trimSpace(unit);
    if (u == "%") return scaleCur(f / 100);
    if (u == "em" || u == "rem") return scaleCur(f);
    if (u == "pt") return sizeFromCSS(f * 4 / 3);
    return sizeFromCSS(f);
}

void applyProps(Style& s, const Props& props) {
    for (const auto& [k, v] : props) {
        if (k == "font-size") {
            if (auto px = parseCSSFontSize(v, s.size)) s.size = *px;
        } else if (k == "font-weight") {
            s.bold = cssIsBold(v);
        } else if (k == "font-style") {
            string t = toLower(trimSpace(v));
            if (t == "italic" || t == "oblique")
                s.italic = true;
            else if (t == "normal")
                s.italic = false;
        }
        // colour, text-align, outline and font-family never change a measurement.
    }
}

Sheet parseStyleSheet(string_view css) {
    Sheet out;
    while (!css.empty()) {
        size_t open = css.find('{');
        if (open == string_view::npos) break;
        string_view sels = trimSpace(css.substr(0, open));
        string_view rest = css.substr(open + 1);
        size_t close = rest.find('}');
        if (close == string_view::npos) break;
        string_view body = rest.substr(0, close);
        css = rest.substr(close + 1);
        Props props;
        size_t p = 0;
        while (p <= body.size()) {
            size_t e = body.find(';', p);
            string_view decl = body.substr(p, (e == string_view::npos ? body.size() : e) - p);
            p = e == string_view::npos ? body.size() + 1 : e + 1;
            size_t c = decl.find(':');
            if (c == string_view::npos) continue;
            string k = toLower(trimSpace(decl.substr(0, c)));
            string_view v = trimSpace(decl.substr(c + 1));
            v = trimSuffix(trimSpace(trimSuffix(v, "!important")), "!");
            if (!k.empty() && !v.empty()) props[k] = string(trimSpace(v));
        }
        if (props.empty()) continue;
        size_t q = 0;
        while (q <= sels.size()) {
            size_t e = sels.find(',', q);
            string_view sel = trimSpace(sels.substr(q, (e == string_view::npos ? sels.size() : e) - q));
            q = e == string_view::npos ? sels.size() + 1 : e + 1;
            if (sel.empty()) continue;
            Props& dst = out[string(sel)];
            for (const auto& [k, v] : props) dst[k] = v;
        }
    }
    return out;
}

// ---- tags --------------------------------------------------------------------------------------------------------

struct Tag {
    string name;
    std::map<string, string> attrs;
    bool closing = false;
};

Tag splitTag(string_view tag) {
    Tag t;
    tag = trimSpace(trimSuffix(trimSpace(tag), "/"));
    if (!tag.empty() && tag[0] == '/') {
        t.closing = true;
        tag = tag.substr(1);
    }
    size_t i = 0;
    while (i < tag.size() && !isSpaceByte(tag[i])) i++;
    t.name = toLower(tag.substr(0, i));
    string_view rest = tag.substr(i);
    for (;;) {
        rest = trimLeftWs(rest);
        if (rest.empty()) break;
        size_t eq = rest.find('=');
        size_t sp = rest.find_first_of(" \t\r\n");
        if (eq == string_view::npos || (sp != string_view::npos && sp < eq)) {
            if (sp == string_view::npos) break;
            rest = rest.substr(sp);
            continue;
        }
        string key = toLower(trimSpace(rest.substr(0, eq)));
        rest = rest.substr(eq + 1);
        string_view val;
        if (!rest.empty() && (rest[0] == '\'' || rest[0] == '"')) {
            char q = rest[0];
            size_t end = rest.substr(1).find(q);
            if (end == string_view::npos) {
                val = rest.substr(1);
                rest = {};
            } else {
                val = rest.substr(1, end);
                rest = rest.substr(end + 2);
            }
        } else {
            size_t end = rest.find_first_of(" \t\r\n");
            if (end == string_view::npos) {
                val = rest;
                rest = {};
            } else {
                val = rest.substr(0, end);
                rest = rest.substr(end);
            }
        }
        t.attrs[key] = decodeEntities(val);
    }
    return t;
}

double pow125(int n) {
    double f = 1.0;
    for (; n > 0; n--) f *= 1.25;
    for (; n < 0; n++) f /= 1.25;
    return f;
}

void applyTagStyle(Style& st, const std::map<string, string>& attrs, const Sheet& sheet) {
    if (auto it = attrs.find("class"); it != attrs.end()) {
        for (string_view c : fields(it->second)) {
            auto s = sheet.find("." + string(c));
            if (s != sheet.end()) applyProps(st, s->second);
        }
    }
    if (auto it = attrs.find("style"); it != attrs.end()) {
        Props props;
        const string& inl = it->second;
        size_t p = 0;
        while (p <= inl.size()) {
            size_t e = inl.find(';', p);
            string_view decl = string_view(inl).substr(p, (e == string::npos ? inl.size() : e) - p);
            p = e == string::npos ? inl.size() + 1 : e + 1;
            size_t c = decl.find(':');
            if (c == string_view::npos) continue;
            props[toLower(trimSpace(decl.substr(0, c)))] = string(trimSpace(decl.substr(c + 1)));
        }
        applyProps(st, props);
    }
    // color= and align= cannot change a measurement.
    if (auto it = attrs.find("size"); it != attrs.end()) {
        string_view sz = trimSpace(it->second);
        if (!sz.empty()) {
            bool rel = sz[0] == '+' || sz[0] == '-';
            int64_t n;
            if (goAtoi(sz, n)) {
                int64_t step = rel ? n : n - 3;
                if (step < -2) step = -2;
                if (step > 4) step = 4;
                st.size = sizeFromCSS(double(kCssBase) * pow125(int(step)));
            }
        }
    }
}

// ---- tokenizer ------------------------------------------------------------------------------------

struct Token {
    string text;
    Style style;
    bool brk = false, space = false;
};

std::vector<string> splitKeepSpaces(string_view s) {
    std::vector<string> out;
    size_t start = 0;
    bool inSpace = false;
    for (size_t i = 0; i < s.size(); i++) {
        bool sp = isSpaceByte(s[i]);
        if (i == 0) {
            inSpace = sp;
            continue;
        }
        if (sp != inSpace) {
            out.emplace_back(s.substr(start, i - start));
            start = i;
            inSpace = sp;
        }
    }
    if (start < s.size()) out.emplace_back(s.substr(start));
    return out;
}

bool oneOf(const string& n, std::initializer_list<const char*> l) {
    for (const char* k : l)
        if (n == k) return true;
    return false;
}

std::vector<Token> parseMaptext(string_view markup, const Sheet& sheet, const Style& base) {
    std::vector<Token> toks;
    std::vector<Style> stack{base};
    auto emitText = [&](const string& s) {
        for (const string& part : splitKeepSpaces(s)) {
            if (part.empty()) continue;
            toks.push_back({part, stack.back(), false, trimSpace(part).empty()});
        }
    };
    auto pop = [&] {
        if (stack.size() > 1) stack.pop_back();
    };
    size_t i = 0;
    while (i < markup.size()) {
        size_t lt = markup.find('<', i);
        if (lt == string_view::npos) {
            emitText(decodeEntities(markup.substr(i)));
            break;
        }
        if (lt > i) emitText(decodeEntities(markup.substr(i, lt - i)));
        i = lt;
        size_t gt = markup.find('>', i);
        if (gt == string_view::npos) {
            emitText(decodeEntities(markup.substr(i)));
            break;
        }
        string_view tagText = markup.substr(i + 1, gt - i - 1);
        i = gt + 1;
        Tag t = splitTag(tagText);
        const string& name = t.name;
        if (name == "br") {
            toks.push_back({"", stack.back(), true, false});
        } else if (oneOf(name, {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6"})) {
            toks.push_back({"", stack.back(), true, false});
            if (!t.closing) {
                Style st = stack.back();
                applyTagStyle(st, t.attrs, sheet);
                stack.push_back(st);
            } else {
                pop();
            }
        } else if (name == "img" || name == "beep") {
            // dropped
        } else if (oneOf(name, {"span", "font", "a", "b",    "i",   "u",   "em",   "strong", "small",   "big", "center",
                                "tt",   "code", "s", "cite", "var", "kbd", "samp", "dfn",    "acronym", "pre"})) {
            if (t.closing) {
                pop();
                continue;
            }
            Style st = stack.back();
            if (name == "b" || name == "strong")
                st.bold = true;
            else if (oneOf(name, {"i", "em", "cite", "var", "dfn"}))
                st.italic = true;
            else if (name == "small") {
                if (auto px = parseCSSFontSize("smaller", st.size)) st.size = *px;
            } else if (name == "big") {
                if (auto px = parseCSSFontSize("larger", st.size)) st.size = *px;
            } else if (oneOf(name, {"tt", "code", "kbd", "samp", "pre"}))
                st.mono = true;
            applyTagStyle(st, t.attrs, sheet);
            stack.push_back(st);
        }
    }
    return toks;
}

// ---- layout (reduced to the measured numbers) ---------------------------------------------------------------

MaptextMeasure layoutMeasure(string_view markup, const Sheet& sheet, int width) {
    const Style base;  // default style: size 11, raster
    std::vector<Token> toks = parseMaptext(markup, sheet, base);
    const GoFace& baseFace = faceOf(base);

    MaptextMeasure lay;
    bool anyLine = false;
    bool hasRuns = false;  // line has at least one run
    int lineW = 0;
    bool pendingSpace = false;
    Style pendingStyle;
    int lineAsc = baseFace.ascent, lineDesc = baseFace.descent;

    auto flush = [&] {
        pendingSpace = false;
        if (lineW > lay.width) lay.width = lineW;
        lay.height += lineAsc + lineDesc + 1;
        anyLine = true;
        hasRuns = false;
        lineW = 0;
        lineAsc = baseFace.ascent;
        lineDesc = baseFace.descent;
    };
    auto add = [&](string_view text, const Style& st) {
        hasRuns = true;
        const GoFace& f = faceOf(st);
        if (f.ascent > lineAsc) lineAsc = f.ascent;
        if (f.descent > lineDesc) lineDesc = f.descent;
        lineW += f.width(text);
    };

    for (const Token& t : toks) {
        if (t.brk) {
            flush();
            continue;
        }
        if (t.space) {
            if (hasRuns) {
                pendingSpace = true;
                pendingStyle = t.style;
            }
            continue;
        }
        const GoFace& face = faceOf(t.style);
        int w = face.width(t.text);
        int sepW = pendingSpace ? faceOf(pendingStyle).width(" ") : 0;
        if (width > 0 && lineW + sepW + w > width && hasRuns) {
            flush();
        } else if (pendingSpace) {
            add(" ", pendingStyle);
            pendingSpace = false;
        }
        if (width > 0 && w > width) {
            string_view rest = t.text;
            while (face.width(rest) > width) {
                string_view cut = face.clip(rest, width);
                if (cut.empty()) cut = rest.substr(0, 1);  // one byte, so the cut can split a multi-byte rune
                add(cut, t.style);
                flush();
                rest = rest.substr(cut.size());
            }
            if (!rest.empty()) add(rest, t.style);
            continue;
        }
        add(t.text, t.style);
    }
    if (hasRuns || !anyLine) flush();
    return lay;
}

}  // namespace

MaptextMeasure measureMaptext(string_view markup, string_view style, int width) {
    return layoutMeasure(markup, parseStyleSheet(style), width);
}

MeasureTextAnswer measureTextAnswer(string_view text, string_view style, uint16_t width) {
    MaptextMeasure m = measureMaptext(text, style, width);
    if (m.width > 0xffff || m.height > 0xffff) return {0xffff, 0xffff};
    return {uint16_t(m.width), uint16_t(m.height)};
}

string measureTextReplyBody(const MeasureTextRequest& req) {
    MeasureTextAnswer a = measureTextAnswer(req.text, req.style, req.width);
    string o(8, '\0');
    o[0] = char(a.width & 0xff);
    o[1] = char(a.width >> 8);
    o[2] = char(a.height & 0xff);
    o[3] = char(a.height >> 8);
    for (int i = 0; i < 4; i++) o[4 + i] = char((req.id >> (8 * i)) & 0xff);
    return o;
}

}  // namespace fugue::render
