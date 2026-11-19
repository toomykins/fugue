#include "tools/canon_json/canon_json.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace fugue::canon {

Json u(uint64_t v) { return std::to_string(v); }
Json i(int64_t v) { return std::to_string(v); }
Json boolean(bool v) { return v ? "true" : "false"; }
Json null() { return "null"; }

Json f32(uint32_t bits) {
    char b[40];
    std::snprintf(b, sizeof b, "{\"f32\":\"0x%08x\"}", bits);
    return b;
}
Json f64(uint64_t bits) {
    char b[48];
    std::snprintf(b, sizeof b, "{\"f64\":\"0x%016llx\"}", static_cast<unsigned long long>(bits));
    return b;
}
Json f32FromFloat(float v) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    return f32(b);
}
Json f64FromDouble(double v) {
    uint64_t b;
    std::memcpy(&b, &v, 8);
    return f64(b);
}

std::string hex(std::span<const uint8_t> b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}

// Strict UTF-8: shortest form only, no surrogates, <= U+10FFFF.
static size_t utf8Len(const unsigned char* p, size_t n) {
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return (n >= 2 && (p[1] & 0xC0) == 0x80) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (n < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && p[1] < 0xA0) return 0;  // overlong
        if (c == 0xED && p[1] > 0x9F) return 0;  // surrogates
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (n < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && p[1] < 0x90) return 0;  // overlong
        if (c == 0xF4 && p[1] > 0x8F) return 0;  // > U+10FFFF
        return 4;
    }
    return 0;
}

bool validUtf8(std::string_view s) {
    auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t n = s.size();
    for (size_t k = 0; k < n;) {
        size_t l = utf8Len(p + k, n - k);
        if (!l) return false;
        k += l;
    }
    return true;
}

static void escapeInto(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(char(c));
        } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else {
            out.push_back(char(c));
        }
    }
    out.push_back('"');
}

void writeString(std::string& out, std::string_view s) {
    if (validUtf8(s)) {
        escapeInto(out, s);
        return;
    }
    std::string clean;
    auto* p = reinterpret_cast<const unsigned char*>(s.data());
    for (size_t k = 0; k < s.size();) {
        size_t l = utf8Len(p + k, s.size() - k);
        if (!l) {
            clean += "\xEF\xBF\xBD";
            k++;
        } else {
            clean.append(s.substr(k, l));
            k += l;
        }
    }
    escapeInto(out, clean);
}

Json str(std::string_view s) {
    Json o;
    if (validUtf8(s)) {
        escapeInto(o, s);
    } else {
        o = "{\"bstr\":\"" + hex({reinterpret_cast<const uint8_t*>(s.data()), s.size()}) + "\"}";
    }
    return o;
}

Json bytes(std::span<const uint8_t> b) { return "\"" + hex(b) + "\""; }

Json array(const std::vector<Json>& e) {
    Json o = "[";
    for (size_t k = 0; k < e.size(); k++) {
        if (k) o.push_back(',');
        o += e[k];
    }
    o.push_back(']');
    return o;
}

static Json emitMap(const std::vector<std::pair<Json, Json>>& es) {
    Json o = "[";
    for (size_t k = 0; k < es.size(); k++) {
        if (k) o.push_back(',');
        o += "{\"k\":" + es[k].first + ",\"v\":" + es[k].second + "}";
    }
    o.push_back(']');
    return o;
}

Json mapU(std::vector<std::pair<uint64_t, Json>> e) {
    std::stable_sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<std::pair<Json, Json>> r;
    for (auto& x : e) r.emplace_back(u(x.first), std::move(x.second));
    return emitMap(r);
}
Json mapI(std::vector<std::pair<int64_t, Json>> e) {
    std::stable_sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<std::pair<Json, Json>> r;
    for (auto& x : e) r.emplace_back(i(x.first), std::move(x.second));
    return emitMap(r);
}
Json mapS(std::vector<std::pair<Json, Json>> e) {
    std::stable_sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.first < b.first; });  // bytewise
    return emitMap(e);
}

std::string fieldName(std::string_view n) {
    size_t k = 0;
    while (k < n.size() && n[k] >= 'A' && n[k] <= 'Z') k++;
    std::string o(n);
    auto low = [](char c) { return char(c - 'A' + 'a'); };
    if (k == 0) return o;
    if (k == n.size()) {
        for (auto& c : o) c = low(c);
        return o;
    }
    size_t lowerUpTo = (k == 1) ? 1 : k - 1;  // run of 1: lowercase it; longer: last letter starts next word
    for (size_t x = 0; x < lowerUpTo; x++) o[x] = low(o[x]);
    return o;
}

Object& Object::set(std::string_view name, Json value) {
    m_.emplace_back(std::string(name), std::move(value));
    return *this;
}

Json Object::build() const {
    auto v = m_;
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t k = 1; k < v.size(); k++)
        if (v[k].first == v[k - 1].first) throw std::logic_error("canon: duplicate field " + v[k].first);
    Json o = "{";
    for (size_t k = 0; k < v.size(); k++) {
        if (k) o.push_back(',');
        escapeInto(o, v[k].first);
        o.push_back(':');
        o += v[k].second;
    }
    o.push_back('}');
    return o;
}

static std::string envelope(size_t index, std::string_view dir, uint16_t typ, size_t len, std::string_view sha,
                            std::string_view dec) {
    char b[160];
    std::snprintf(b, sizeof b,
                  "{\"v\":1,\"level\":\"L1\",\"i\":%zu,\"dir\":\"%.*s\",\"typ\":\"0x%04x\",\"len\":%zu,\"sha256\":\"",
                  index, int(dir.size()), dir.data(), typ, len);
    std::string o = b;
    o.append(sha);
    o += "\",\"dec\":";
    writeString(o, dec);
    o.push_back(',');
    return o;
}

std::string recordOk(size_t index, std::string_view dir, uint16_t typ, size_t len, std::string_view sha,
                     std::string_view dec, const Json& fields) {
    std::string o = envelope(index, dir, typ, len, sha, dec);
    o += "\"ok\":true,\"fields\":";
    o += fields;
    o.push_back('}');
    return o;
}

std::string recordErr(size_t index, std::string_view dir, uint16_t typ, size_t len, std::string_view sha,
                      std::string_view dec, std::string_view err, std::span<const uint8_t> raw) {
    std::string o = envelope(index, dir, typ, len, sha, dec);
    o += "\"ok\":false,\"err\":";
    writeString(o, err);
    o += ",\"raw\":\"" + hex(raw) + "\"}";
    return o;
}

}  // namespace fugue::canon
