// S2C::FlickState: flick() icon_state payload.
//   [variant u8 = 0][icon_state \0][kind u8 = 2][ref u32][iconResIndex u32][aux u16]
#include "proto/registry.h"

namespace fugue::proto {

// The AtomLoc is [kind u8][ref u32][iconResIndex u32][aux u16]. Only variant 0 and AtomLoc kind 2 are decoded; other
//   values are refused rather than guessed. The first byte is a variant discriminator: 0 string only, 1 index only, 2 both.
FlickState decodeFlickState(ByteSpan body) {
    Reader r(body);
    FlickState m;
    m.variant = r.u8();
    if (m.variant != 0) throw DecodeError("0x6c variant " + std::to_string(m.variant) + " not decoded");
    m.iconState = r.cstr();
    if (r.remaining() != 11) throw DecodeError("0x6c tail is not 11 bytes");
    m.loc.kind = r.u8();
    if (m.loc.kind != 2) throw DecodeError("0x6c AtomLoc kind " + std::to_string(m.loc.kind) + " not decoded");
    m.loc.ref = r.u32();
    m.iconResIndex = r.u32();
    m.loc.aux = r.u16();
    return m;
}

canon::Json toJson(const FlickState& m) {
    return canon::Object()
        .u("variant", m.variant)
        .str("iconState", m.iconState)
        .set("loc", canon::Object().u("kind", m.loc.kind).u("ref", m.loc.ref).u("aux", m.loc.aux).build())
        .u("iconResIndex", m.iconResIndex)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::FlickState, "cpp:stock-dd-wire", jsonOf<decodeFlickState>);

}  // namespace fugue::proto
