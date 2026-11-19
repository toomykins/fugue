// S2C::FlickTarget: sent before a flick(), naming its atom. [AtomLoc: kind u8, ref u32, aux u16] then an optional
// [appid u32].
#include "proto/registry.h"

namespace fugue::proto {

// Layout [AtomLoc][appid u32]. The 7-byte AtomLoc-only form is also accepted (the appid is length-guarded in the
//   client). Any other length is rejected.
FlickTarget decodeFlickTarget(ByteSpan body) {
    if (body.size() != 11 && body.size() != 7)
        throw DecodeError("0xc7 body is " + std::to_string(body.size()) + " bytes, want 7 or 11");
    Reader r(body);
    FlickTarget m;
    m.loc.kind = r.u8();
    m.loc.ref = r.u32();
    m.loc.aux = r.u16();
    if (r.more()) {
        m.hasAppid = true;
        m.appid = r.u32();
    }
    return m;
}

canon::Json toJson(const FlickTarget& m) {
    return canon::Object()
        .set("loc", canon::Object().u("kind", m.loc.kind).u("ref", m.loc.ref).u("aux", m.loc.aux).build())
        .u("appid", m.appid)
        .boolean("hasAppid", m.hasAppid)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::FlickTarget, "cpp:stock-dd-wire", jsonOf<decodeFlickTarget>);

}  // namespace fugue::proto
