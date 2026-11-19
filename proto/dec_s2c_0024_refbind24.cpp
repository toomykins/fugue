// S2C::ObjBind: obj ref -> appearance bind. Exactly 8 bytes: [ref u32 LE][appid u32 LE].
#include "proto/registry.h"

namespace fugue::proto {

// Two wide (u32) ids with no slack; a body of any other size throws rather than guessing a width.
RefBind decodeObjBind(ByteSpan body) {
    if (body.size() != 8) throw DecodeError("0x24 body is " + std::to_string(body.size()) + " bytes, want 8");
    Reader r(body);
    RefBind m;
    m.ref = r.u32();
    m.appid = r.u32();
    return m;
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ObjBind, "cpp:stock-dd-wire", jsonOf<decodeObjBind>);

}  // namespace fugue::proto
