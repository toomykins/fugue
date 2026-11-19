// S2C::Output: targeted output(value, control).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [value\n\0][control\0], raw bytes, NUL terminated. The BYOND-appended newline is removed from value (one only).
//   The text is NOT url-decoded: the DM code url-encodes it before it reaches the wire.
Output decodeOutput(ByteSpan body) {
    Reader r(body);
    Output m;
    m.value = r.cstr();
    m.control = r.cstr();
    r.expectEnd();
    if (!m.value.empty() && m.value.back() == '\n') m.value.pop_back();
    return m;
}

canon::Json toJson(const Output& m) { return canon::Object().str("value", m.value).str("control", m.control).build(); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Output, "cpp:stock-dd-wire", jsonOf<decodeOutput>);

}  // namespace fugue::proto
