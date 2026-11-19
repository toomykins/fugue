// S2C::MobBind: mob ref -> appearance bind. 9 bytes: [ref u32 LE][appid u32 LE][pad 00].
#include "proto/registry.h"

namespace fugue::proto {

// Framing: the client reads exactly two u32 ids (8 bytes). The 9th byte is pad and is ignored. The appid names an
//   S2C::Appearance.
RefBind decodeMobBind(ByteSpan body) {
    if (body.size() < 8) throw DecodeError("0x09 body is " + std::to_string(body.size()) + " bytes, want >= 8");
    Reader r(body);
    RefBind m;
    m.ref = r.u32();
    m.appid = r.u32();
    return m;
}

canon::Json toJson(const RefBind& m) { return canon::Object().u("ref", m.ref).u("appid", m.appid).build(); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::MobBind, "cpp:stock-dd-wire", jsonOf<decodeMobBind>);

}  // namespace fugue::proto
