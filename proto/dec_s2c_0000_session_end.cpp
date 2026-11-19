// S2C::SessionEnd: session / version control ("session end").
//   body = [reason u16 LE] ++ optional [requiredVersion u32 LE] (read when the body is at least 6 bytes)
//   reason: 4/5 "Version %u is required" (requiredVersion follows), 6 reset, 9 virtual disconnect,
//   0x0b lock, >= 0x0c "Server closing connection".
#include "proto/registry.h"

namespace fugue::proto {

// Example: `05 00 | ea 01 00 00` is reason 5, requiredVersion 490. A 6-byte body with version 0 and the 2-byte form
//   both give requiredVersion 0. A 3-5 byte body does too: DreamSeeker's u32 read underflows to 0 and the frame is still
//   handled, so it is accepted here rather than refused.
SessionEnd decodeSessionEnd(ByteSpan body) {
    if (body.size() < 2) throw DecodeError("short session");
    Reader r(body);
    SessionEnd m;
    m.reason = r.u16();
    if (body.size() >= 6) m.requiredVersion = r.u32();
    return m;
}

canon::Json toJson(const SessionEnd& m) {
    return canon::Object().u("reason", m.reason).u("requiredVersion", m.requiredVersion).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::SessionEnd, "cpp:stock-dd-wire", jsonOf<decodeSessionEnd>);

}  // namespace fugue::proto
