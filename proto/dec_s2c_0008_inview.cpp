// S2C::InView: in-view object update. [flags u8][u16 if flags&2][u16 if flags&4] then the ten counted
// object arrays and an optional tail byte (shared grammar in g3_inview.cpp).
#include "proto/g3_inview.h"

namespace fugue::proto {

InView decodeInView(ByteSpan body) {
    Reader r(body);
    InView m;
    m.flags = r.u8();
    m.hasExtra1 = (m.flags & 2) != 0;
    m.hasExtra2 = (m.flags & 4) != 0;
    if (m.hasExtra1) m.extra1 = r.u16();
    if (m.hasExtra2) m.extra2 = r.u16();
    m.objects = g3::readObjectArrays(r);
    r.expectEnd();
    return m;
}

canon::Json toJson(const InView& m) {
    canon::Object o;
    o.u("flags", m.flags)
        .u("extra1", m.extra1)
        .u("extra2", m.extra2)
        .boolean("hasExtra1", m.hasExtra1)
        .boolean("hasExtra2", m.hasExtra2);
    addJson(o, m.objects);
    return o.build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::InView, "cpp:stock-dd-wire", jsonOf<decodeInView>);

}  // namespace fugue::proto
