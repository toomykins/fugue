#include "pager/text.h"
#include <climits>

namespace fugue::pager {

char32_t decodeRune(std::string_view s, size_t i, size_t* width) {
    auto b = [&](size_t k) { return uint8_t(s[k]); };
    uint8_t c = b(i);
    auto bad = [&]() {
        *width = 1;
        return char32_t(0xFFFD);
    };
    if (c < 0x80) {
        *width = 1;
        return c;
    }
    auto cont = [&](size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    if (c >= 0xC2 && c <= 0xDF) {
        if (!cont(i + 1)) return bad();
        *width = 2;
        return char32_t((c & 0x1F) << 6 | (b(i + 1) & 0x3F));
    }
    if (c >= 0xE0 && c <= 0xEF) {
        if (!cont(i + 1) || !cont(i + 2)) return bad();
        char32_t r = char32_t((c & 0x0F) << 12 | (b(i + 1) & 0x3F) << 6 | (b(i + 2) & 0x3F));
        if (r < 0x800 || (r >= 0xD800 && r <= 0xDFFF)) return bad();
        *width = 3;
        return r;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return bad();
        char32_t r = char32_t((c & 0x07) << 18 | (b(i + 1) & 0x3F) << 12 | (b(i + 2) & 0x3F) << 6 | (b(i + 3) & 0x3F));
        if (r < 0x10000 || r > 0x10FFFF) return bad();
        *width = 4;
        return r;
    }
    return bad();
}

void appendRune(std::string& o, char32_t r) {
    if (r > 0x10FFFF || (r >= 0xD800 && r <= 0xDFFF)) r = 0xFFFD;
    if (r < 0x80)
        o.push_back(char(r));
    else if (r < 0x800) {
        o.push_back(char(0xC0 | r >> 6));
        o.push_back(char(0x80 | (r & 0x3F)));
    } else if (r < 0x10000) {
        o.push_back(char(0xE0 | r >> 12));
        o.push_back(char(0x80 | ((r >> 6) & 0x3F)));
        o.push_back(char(0x80 | (r & 0x3F)));
    } else {
        o.push_back(char(0xF0 | r >> 18));
        o.push_back(char(0x80 | ((r >> 12) & 0x3F)));
        o.push_back(char(0x80 | ((r >> 6) & 0x3F)));
        o.push_back(char(0x80 | (r & 0x3F)));
    }
}

bool isGoSpace(char32_t r) {
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

std::string trimSpace(std::string_view s) {
    size_t a = 0, w;
    while (a < s.size()) {
        char32_t r = decodeRune(s, a, &w);
        if (!isGoSpace(r)) break;
        a += w;
    }
    size_t e = s.size();
    while (e > a) {  // trim from the right: find the start of the last rune
        size_t st = e - 1;
        while (st > a && (uint8_t(s[st]) & 0xC0) == 0x80 && e - st < 4) st--;
        char32_t r = decodeRune(s, st, &w);
        if (st + w != e) {
            st = e - 1;
            r = 0xFFFD;
        }
        if (!isGoSpace(r)) break;
        e = st;
    }
    return std::string(s.substr(a, e - a));
}

std::vector<std::string> fields(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    bool in = false;
    for (size_t i = 0; i < s.size();) {
        size_t w;
        char32_t r = decodeRune(s, i, &w);
        if (isGoSpace(r)) {
            if (in) {
                out.push_back(cur);
                cur.clear();
                in = false;
            }
        } else {
            cur.append(s.substr(i, w));
            in = true;
        }
        i += w;
    }
    if (in) out.push_back(cur);
    return out;
}

size_t runeCount(std::string_view s) {
    size_t n = 0, w;
    for (size_t i = 0; i < s.size(); i += w) {
        decodeRune(s, i, &w);
        n++;
    }
    return n;
}

std::string padRight(std::string_view s, size_t runes) {
    std::string o(s);
    for (size_t n = runeCount(s); n < runes; n++) o.push_back(' ');
    return o;
}

std::string goQuote(std::string_view s) {
    static const char* hx = "0123456789abcdef";
    std::string o = "\"";
    for (size_t i = 0; i < s.size();) {
        size_t w;
        char32_t r = decodeRune(s, i, &w);
        if (r == 0xFFFD && w == 1) {
            o += "\\x";
            o.push_back(hx[uint8_t(s[i]) >> 4]);
            o.push_back(hx[uint8_t(s[i]) & 15]);
            i++;
            continue;
        }
        switch (r) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\a': o += "\\a"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\v': o += "\\v"; break;
            default:
                if (r < 0x20 || r == 0x7f) {
                    o += "\\x";
                    o.push_back(hx[r >> 4]);
                    o.push_back(hx[r & 15]);
                } else if (r >= 0x80 && r < 0xA0) {
                    o += "\\u00";
                    o.push_back(hx[r >> 4]);
                    o.push_back(hx[r & 15]);
                }  // C1 controls are non-printable
                else if (r == 0xAD || r == 0x2028 || r == 0x2029 ||
                         r == 0xFEFF) {  // a few common non-printables, not a full Unicode table
                    o += r > 0xFFFF ? "\\U" : "\\u";
                    for (int sh = 12; sh >= 0; sh -= 4) o.push_back(hx[(r >> sh) & 15]);
                } else
                    o.append(s.substr(i, w));
        }
        i += w;
    }
    return o + "\"";
}

std::string latin1ToUtf8(std::string_view raw) {
    std::string o;
    o.reserve(raw.size());
    for (unsigned char c : raw) appendRune(o, c);
    return o;
}

bool goAtoi(std::string_view s, long long* out) {
    *out = 0;
    if (s.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i == s.size()) return false;
    unsigned long long v = 0;
    bool over = false;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') {
            *out = 0;
            return false;
        }
        unsigned d = unsigned(s[i] - '0');
        if (v > (ULLONG_MAX - d) / 10)
            over = true;
        else
            v = v * 10 + d;
    }
    unsigned long long lim = neg ? (1ULL << 63) : (1ULL << 63) - 1;
    if (over || v > lim) {
        *out = neg ? LLONG_MIN : LLONG_MAX;
        return false;
    }
    *out = neg ? (long long)(0 - v) : (long long)v;
    return true;
}

}  // namespace fugue::pager
