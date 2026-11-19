// S2C::Winset: winset(control, "prop=val;...").
#include "proto/registry.h"

namespace fugue::proto {

// Layout [control\0][props\0]; a null control is an empty string. Props are stored raw, not url-decoded.
Winset decodeWinset(ByteSpan body) {
    Reader r(body);
    Winset m;
    m.control = r.cstr();
    m.props = r.cstr();
    r.expectEnd();
    return m;
}

canon::Json toJson(const Winset& m) { return canon::Object().str("control", m.control).str("props", m.props).build(); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Winset, "cpp:stock-dd-wire", jsonOf<decodeWinset>);

}  // namespace fugue::proto
