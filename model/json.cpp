#include "model/json.h"

#include <stdexcept>

#include "net/bytes.h"
#include "tools/canon_json/canon_json.h"

namespace fugue::model {

namespace {

struct P {
    std::string_view t;
    size_t p = 0;
    [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("model json: ") + m); }
    void ws() {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) p++;
    }
    char peek() {
        if (p >= t.size()) fail("eof");
        return t[p];
    }

    std::string str() {
        if (peek() != '"') fail("string expected");
        p++;
        std::string o;
        while (true) {
            char c = peek();
            if (c == '"') {
                p++;
                return o;
            }
            if (c == '\\') {
                p++;
                char e = peek();
                p++;
                switch (e) {
                    case 'u': {
                        if (p + 4 > t.size()) fail("bad \\u");
                        unsigned v = unsigned(std::stoul(std::string(t.substr(p, 4)), nullptr, 16));
                        p += 4;
                        // canon emits \u00xx only for control bytes; any other code point is encoded as UTF-8.
                        if (v < 0x80)
                            o.push_back(char(v));
                        else if (v < 0x800) {
                            o.push_back(char(0xc0 | v >> 6));
                            o.push_back(char(0x80 | (v & 0x3f)));
                        } else {
                            o.push_back(char(0xe0 | v >> 12));
                            o.push_back(char(0x80 | ((v >> 6) & 0x3f)));
                            o.push_back(char(0x80 | (v & 0x3f)));
                        }
                        break;
                    }
                    case 'n': o.push_back('\n'); break;
                    case 't': o.push_back('\t'); break;
                    case 'r': o.push_back('\r'); break;
                    case 'b': o.push_back('\b'); break;
                    case 'f': o.push_back('\f'); break;
                    default: o.push_back(e);  // \" \\ \/
                }
            } else {
                o.push_back(c);
                p++;
            }
        }
    }

    JVal value() {
        ws();
        JVal v;
        char c = peek();
        if (c == '{') {
            v.kind = JVal::Obj;
            p++;
            ws();
            if (peek() == '}') {
                p++;
                return v;
            }
            while (true) {
                ws();
                std::string k = str();
                ws();
                if (peek() != ':') fail("':' expected");
                p++;
                JVal x = value();
                v.o.emplace_back(std::move(k), std::move(x));
                ws();
                char d = peek();
                p++;
                if (d == '}') return v;
                if (d != ',') fail("',' expected");
            }
        }
        if (c == '[') {
            v.kind = JVal::Arr;
            p++;
            ws();
            if (peek() == ']') {
                p++;
                return v;
            }
            while (true) {
                v.a.push_back(value());
                ws();
                char d = peek();
                p++;
                if (d == ']') return v;
                if (d != ',') fail("',' or ']' expected");
            }
        }
        if (c == '"') {
            v.kind = JVal::Str;
            v.s = str();
            return v;
        }
        if (t.substr(p, 4) == "true") {
            p += 4;
            v.kind = JVal::Bool;
            v.b = true;
            return v;
        }
        if (t.substr(p, 5) == "false") {
            p += 5;
            v.kind = JVal::Bool;
            return v;
        }
        if (t.substr(p, 4) == "null") {
            p += 4;
            return v;
        }
        // integer
        bool neg = false;
        if (c == '-') {
            neg = true;
            p++;
        }
        if (p >= t.size() || t[p] < '0' || t[p] > '9') fail("number expected");
        uint64_t x = 0;
        while (p < t.size() && t[p] >= '0' && t[p] <= '9') x = x * 10 + uint64_t(t[p++] - '0');
        v.kind = JVal::Num;
        v.n = neg ? -int64_t(x) : int64_t(x);
        return v;
    }
};

}  // namespace

const JVal& JVal::operator[](std::string_view key) const {
    static const JVal nullv;
    if (kind == Obj)
        for (auto& kv : o)
            if (kv.first == key) return kv.second;
    return nullv;
}

uint32_t JVal::f32bits() const {
    const JVal& h = (*this)["f32"];
    if (h.kind != Str || h.s.size() < 3) return 0;
    return uint32_t(std::stoul(h.s.substr(2), nullptr, 16));
}

std::string JVal::bytesOrStr() const {
    if (kind == Str) return s;
    const JVal& h = (*this)["bstr"];
    if (h.kind == Str) {
        auto b = hexDecode(h.s);
        if (b) return std::string(b->begin(), b->end());
    }
    return {};
}

JVal parseJson(std::string_view text) {
    P p{text};
    JVal v = p.value();
    p.ws();
    if (p.p != text.size()) p.fail("trailing data");
    return v;
}

std::string dumpCanon(const JVal& v) {
    switch (v.kind) {
        case JVal::Null: return "null";
        case JVal::Bool: return v.b ? "true" : "false";
        case JVal::Num: return canon::i(v.n);
        case JVal::Str: return canon::str(v.s);
        case JVal::Arr: {
            std::vector<std::string> e;
            for (auto& x : v.a) e.push_back(dumpCanon(x));
            return canon::array(e);
        }
        case JVal::Obj: {
            std::string o = "{";
            bool first = true;
            for (auto& [k, x] : v.o) {
                if (!first) o += ',';
                first = false;
                o += canon::str(k);
                o += ':';
                o += dumpCanon(x);
            }
            return o + "}";
        }
    }
    return "null";
}

}  // namespace fugue::model
