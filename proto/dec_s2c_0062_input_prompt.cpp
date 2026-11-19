// S2C::Input: input() prompt request (blocking).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [title\0][default\0][message\0][typeMask u32][filter u8][filterKind u8][id u32]. typeMask is passed through
//   uninterpreted.
InputPrompt decodeInputPrompt(ByteSpan body) {
    Reader r(body);
    InputPrompt m;
    m.title = r.cstr();
    m.defaultValue = r.cstr();
    m.message = r.cstr();
    m.typeMask = r.u32();
    m.filter = r.u8();
    m.filterKind = r.u8();
    m.id = r.u32();
    r.expectEnd();
    return m;
}

canon::Json toJson(const InputPrompt& m) {
    return canon::Object()
        .str("title", m.title)
        .str("default", m.defaultValue)
        .str("message", m.message)
        .u("typeMask", m.typeMask)
        .u("filter", m.filter)
        .u("filterKind", m.filterKind)
        .u("id", m.id)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Input, "cpp:stock-dd-wire", jsonOf<decodeInputPrompt>);

}  // namespace fugue::proto
