// Minimal JSON reader for the tools and tests: command lines, fixture manifests, and canonical JSON
// read back. The client's live path never parses JSON.
// Numbers are integers only (the canon encoding never emits fractions); floats arrive as {"f32":"0x.."}.
// Strings: standard escapes; {"bstr":"<hex>"} is NOT unwrapped here (see JVal::bytesOrStr).
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fugue::model {

struct JVal {
    enum Kind : uint8_t { Null, Bool, Num, Str, Arr, Obj } kind = Null;
    bool b = false;
    int64_t n = 0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    // Object member lookup; a missing key (or a non-object) yields a shared Null value.
    const JVal& operator[](std::string_view key) const;
    const JVal& at(size_t i) const { return a[i]; }
    size_t size() const { return a.size(); }
    bool isNull() const { return kind == Null; }
    uint32_t u32() const { return uint32_t(n); }
    uint16_t u16() const { return uint16_t(n); }
    int64_t i() const { return n; }
    // {"f32":"0x3f800000"} -> bits. 0 for anything else.
    uint32_t f32bits() const;
    // A canonical string: the text, or for {"bstr":"<hex>"} the raw bytes.
    std::string bytesOrStr() const;
};

// Throws std::runtime_error on malformed input.
JVal parseJson(std::string_view text);
// Re-serialises a parsed value as canonical JSON (object member order is kept as parsed, which is the
// sorted order the decoders emit; strings re-escaped by canon::str). parse->dump is the identity on
// decoder output.
std::string dumpCanon(const JVal& v);

}  // namespace fugue::model
