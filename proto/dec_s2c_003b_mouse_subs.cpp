// S2C::MouseSubs: global config - the FormatFlags word (mouse subscriptions).
#include "proto/registry.h"

namespace fugue::proto {

// Body [12:16) is the FormatFlags word verbatim; a second word at [16:20) is present iff flags bit 31 is set.
//   Bytes [0:12) (per-flag bytes, resource count) are not decoded. A body shorter than 16 (or 20 with bit 31) throws.
MouseSubs decodeMouseSubs(ByteSpan body) {
    if (body.size() < 16) throw DecodeError("0x3b shorter than 16 bytes");
    Reader r(body);
    r.take(12);
    MouseSubs m;
    m.flags = r.u32();
    if (m.flags & 0x80000000u) {
        if (body.size() < 20) throw DecodeError("0x3b bit31 set but no second word");
        m.extra = r.u32();
    }
    return m;
}

canon::Json toJson(const MouseSubs& m) {
    return canon::Object().u("flags", m.flags).u("extra", m.extra).boolean("known", true).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::MouseSubs, "cpp:stock-dd-wire", jsonOf<decodeMouseSubs>);

}  // namespace fugue::proto
