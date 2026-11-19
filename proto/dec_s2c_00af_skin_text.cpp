// S2C::SkinText: the world's .dmf interface as literal source text.
#include "proto/registry.h"

namespace fugue::proto {

// Layout [skin cstring][hub cstring]? [flags u16]?; the last two are length-guarded, and an empty body
//   means no skin.
SkinText decodeSkinText(ByteSpan body) {
    SkinText m;
    if (body.empty()) return m;
    Reader r(body);
    m.dmf = r.cstr();
    if (r.more()) {
        m.hub = r.cstr();
        m.hasHub = true;
    }
    if (r.more()) {
        m.flags = r.u16();
        m.hasFlags = true;
    }
    r.expectEnd();
    return m;
}

canon::Json toJson(const SkinText& m) {
    return canon::Object()
        .str("dmf", m.dmf)
        .str("hub", m.hub)
        .boolean("hasHub", m.hasHub)
        .u("flags", m.flags)
        .boolean("hasFlags", m.hasFlags)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::SkinText, "cpp:stock-dd-wire", jsonOf<decodeSkinText>);

}  // namespace fugue::proto
