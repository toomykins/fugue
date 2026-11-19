// S2C::Winget: winget(control, prop) query (also winexists: empty prop + flag 1).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [control\0][prop\0][reqid u32][flag u8, length-guarded]; winexists rides this message with an
//   empty prop and flag 1. The flag byte is optional on the wire.
Winget decodeWinget(ByteSpan body) {
    Reader r(body);
    Winget m;
    m.control = r.cstr();
    m.prop = r.cstr();
    m.reqID = r.u32();
    if (r.more()) {
        m.flag = r.u8();
        m.hasFlag = true;
    }
    r.expectEnd();
    return m;
}

canon::Json toJson(const Winget& m) {
    return canon::Object()
        .str("control", m.control)
        .str("prop", m.prop)
        .u("reqID", m.reqID)
        .u("flag", m.flag)
        .boolean("hasFlag", m.hasFlag)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Winget, "cpp:stock-dd-wire", jsonOf<decodeWinget>);

}  // namespace fugue::proto
