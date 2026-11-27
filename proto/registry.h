// Decoder registry: decoders self-register from their own .cpp, keyed by (dir, type).
//
// HOW TO ADD A DECODER: name the message type in proto/msg.h if it is new, add its struct, decode<Name>() and toJson() to
// proto/messages.h, define both in proto/dec_<dir>_<type>.cpp, and register it there with
// FUGUE_REGISTER_DECODER(dir, type, name, jsonOf<decode<Name>>) at namespace scope.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

#include "net/bytes.h"
#include "proto/mapview.h"
#include "proto/messages.h"
#include "proto/msg.h"
#include "proto/reader.h"
#include "tools/canon_json/canon_json.h"

namespace fugue::proto {

enum class Dir : uint8_t { C2S = 0, S2C = 1 };  // wire direction: client-to-server 0, server-to-client 1
inline const char* dirName(Dir d) { return d == Dir::C2S ? "c2s" : "s2c"; }

// Cross-frame state for one decode run, threaded through the decoders in order.
class State {
public:
    MapView mapView;           // see proto/mapview.h
    bool wideVerbIds = false;  // session handshake, NOT inferred from an appearance or 0x003b

    // Decoder-private state, no header change needed: any default-constructible type, created on
    // first use and kept until this State is destroyed. e.g.  auto& s = st.get<MyDecoderState>();
    template <class T>
    T& get() {
        auto& slot = ext_[std::type_index(typeid(T))];
        if (!slot) slot = std::shared_ptr<void>(new T(), [](void* p) { delete static_cast<T*>(p); });
        return *static_cast<T*>(slot.get());
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> ext_;
};

// A registry entry returns the canonical JSON of the "fields" value, or throws DecodeError / any std::exception for a
// malformed body. It must NOT print. Most are jsonOf<decodeX> (below).
using DecodeFn = canon::Json (*)(State&, ByteSpan body);

struct DecoderInfo {
    Dir dir;
    uint16_t type;
    const char* dec;  // name identifying the decoder that produced the entry
    DecodeFn fn;
};

class Registry {
public:
    static Registry& instance();
    void add(const DecoderInfo& d);  // duplicate (dir,type) aborts at startup: two decoders for one slot is a bug
    const DecoderInfo* find(Dir dir, uint16_t type) const;
    size_t size() const { return map_.size(); }

private:
    std::unordered_map<uint32_t, DecoderInfo> map_;
};

struct Registrar {
    Registrar(Dir d, uint16_t t, const char* dec, DecodeFn fn) { Registry::instance().add({d, t, dec, fn}); }
};

// The registry entry of a typed decoder: toJson(decode(body)), with or without the State.
template <auto Decode>
canon::Json jsonOf(State& st, ByteSpan body) {
    if constexpr (std::is_invocable_v<decltype(Decode), State&, ByteSpan>)
        return toJson(Decode(st, body));
    else
        return toJson(Decode(body));
}

#define FUGUE_CONCAT2_(a, b) a##b
#define FUGUE_CONCAT_(a, b) FUGUE_CONCAT2_(a, b)
// FUGUE_REGISTER_DECODER(Dir::S2C, S2C::SelfRef, "dec_s2c_0071_selfref", decode71);
#define FUGUE_REGISTER_DECODER(dir, type, decName, fn) \
    static ::fugue::proto::Registrar FUGUE_CONCAT_(fugue_registrar_, __LINE__)((dir), (type), (decName), (fn))

}  // namespace fugue::proto
