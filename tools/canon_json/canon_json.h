// Canonical JSON writer: deterministic, byte-exact encoding of decoded wire records. No Qt, no dependencies.
//
// Every function returns a canonical JSON TEXT FRAGMENT (std::string); compose by passing fragments
// to Object/array/map*. Nothing here parses JSON.
//
//   integer (any width)  -> decimal           u(), i()
//   bool                 -> true/false        boolean()
//   float32 / float64    -> {"f32":"0x%08x"} / {"f64":"0x%016x"} (IEEE-754 bit pattern)   f32(), f64()
//   string               -> JSON string if valid UTF-8 else {"bstr":"<hex>"}              str()
//   byte span            -> lowercase hex string                                          bytes()
//   sequence             -> array in element order; empty = []                            array()
//   map                  -> [{"k":K,"v":V},...] sorted by key (numeric for ints, else
//                           bytewise on the key's encoding)                               mapU/mapI/mapS
//   object               -> object, keys bytewise sorted, no duplicates                   Object
//   null pointer         -> null                                                          null()
// String escaping: `"` `\` and bytes < 0x20 (as \u00xx). Nothing else.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fugue::canon {

using Json = std::string;

Json u(uint64_t v);
Json i(int64_t v);
Json boolean(bool v);
Json null();
Json f32(uint32_t bits);
Json f64(uint64_t bits);
Json f32FromFloat(float v);
Json f64FromDouble(double v);
Json str(std::string_view s);
Json bytes(std::span<const uint8_t> b);
Json array(const std::vector<Json>& elems);

Json mapU(std::vector<std::pair<uint64_t, Json>> entries);
Json mapI(std::vector<std::pair<int64_t, Json>> entries);
// Keys are already-canonical key encodings (e.g. str("x")); sorted bytewise on that text.
Json mapS(std::vector<std::pair<Json, Json>> entries);

// CamelCase name -> canonical field name: the leading capital run is lowercased, except that the last
// capital of a longer run starts the next word ("URLPath" -> "urlPath").
std::string fieldName(std::string_view goName);

class Object {
public:
    Object& set(std::string_view name, Json value);
    Object& u(std::string_view name, uint64_t v) { return set(name, canon::u(v)); }
    Object& i(std::string_view name, int64_t v) { return set(name, canon::i(v)); }
    Object& boolean(std::string_view name, bool v) { return set(name, canon::boolean(v)); }
    Object& str(std::string_view name, std::string_view v) { return set(name, canon::str(v)); }
    Object& bytes(std::string_view name, std::span<const uint8_t> v) { return set(name, canon::bytes(v)); }
    // Sorted by name; throws std::logic_error on a duplicate name.
    Json build() const;

private:
    std::vector<std::pair<std::string, Json>> m_;
};

// ---- helpers ----
bool validUtf8(std::string_view s);
std::string hex(std::span<const uint8_t> b);
// Append `s` as a JSON string with the minimal escaping. Invalid UTF-8 bytes become U+FFFD
// (used for prose such as `err`; str() instead switches to {"bstr":..} for field values).
void writeString(std::string& out, std::string_view s);

// recordOk/recordErr envelope: fixed key order, no trailing newline. The err form carries the raw frame as hex.
std::string recordOk(size_t index, std::string_view dir, uint16_t typ, size_t len, std::string_view sha256,
                     std::string_view dec, const Json& fields);
std::string recordErr(size_t index, std::string_view dir, uint16_t typ, size_t len, std::string_view sha256,
                      std::string_view dec, std::string_view err, std::span<const uint8_t> raw);

}  // namespace fugue::canon
