// S2C::SkinResource: the world's skin by resource id (0xffffffff = no skin).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [resid u32][hub cstring][flags u16], with no length guards.
SkinResource decodeSkinResource(ByteSpan body) {
    Reader r(body);
    SkinResource m;
    m.resID = r.u32();
    m.hub = r.cstr();
    m.flags = r.u16();
    r.expectEnd();
    return m;
}

canon::Json toJson(const SkinResource& m) {
    return canon::Object().u("resID", m.resID).str("hub", m.hub).u("flags", m.flags).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::SkinResource, "cpp:stock-dd-wire", jsonOf<decodeSkinResource>);

}  // namespace fugue::proto
