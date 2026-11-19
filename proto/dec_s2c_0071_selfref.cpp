// S2C::SelfRef: a bare [value u32 LE]: the client's own mob ref.
#include "proto/registry.h"

namespace fugue::proto {

// Exactly 4 bytes.
SelfRef decodeSelfRef(ByteSpan body) {
    if (body.size() != 4) throw DecodeError("0x71 body is " + std::to_string(body.size()) + " bytes, want 4");
    return SelfRef{Reader(body).u32()};
}

canon::Json toJson(const SelfRef& m) { return canon::Object().u("value", m.value).build(); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::SelfRef, "cpp:stock-dd-wire", jsonOf<decodeSelfRef>);

}  // namespace fugue::proto
