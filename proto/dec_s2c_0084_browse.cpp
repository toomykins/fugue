// S2C::Browse: browse(html, params).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [html\0][params\0][flag u8]; flag 0 = html present, 1 = html null / close. The flag is accepted only as 0 or 1,
//   and nothing may trail it.
Browse decodeBrowse(ByteSpan body) {
    Reader r(body);
    Browse m;
    m.html = r.cstr();
    m.params = r.cstr();
    uint8_t flag = r.u8();
    if (flag > 1) throw DecodeError("0x84 flag is not 0/1");
    r.expectEnd();
    m.close = flag == 1;
    return m;
}

canon::Json toJson(const Browse& m) {
    return canon::Object().str("html", m.html).str("params", m.params).boolean("close", m.close).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Browse, "cpp:stock-dd-wire", jsonOf<decodeBrowse>);

}  // namespace fugue::proto
