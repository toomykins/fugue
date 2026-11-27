// S2C::Verb: verb-list advertisement, ONE verb entry per frame.
#include "proto/registry.h"

namespace fugue::proto {

// Layout: [id u16, or u32 in the negotiated wide-verb mode][name\0][desc\0][cat\0][head u8][src u8][flags u8]([flagsExt u32][extByte u8] iff flags&0x80)
//   [argc u8] argc x [typemask u32][filter u8][filterKind u8]. Head, src, filter and filterKind are not decoded further.
Verb decodeVerb(State& st, ByteSpan body) {
    Reader r(body);
    Verb m;
    m.id = st.wideVerbIds ? r.u32() : r.u16();
    m.name = r.cstr();
    m.desc = r.cstr();
    m.category = r.cstr();
    m.head = r.u8();
    m.src = r.u8();
    m.flags = r.u8();
    if (m.flags & 0x80) {
        m.flagsExt = r.u32();
        m.extByte = r.u8();
    }
    for (unsigned n = r.u8(), i = 0; i < n; i++) {
        Verb::Arg a;
        a.typeMask = r.u32();
        a.filter = r.u8();
        a.filterKind = r.u8();
        m.args.push_back(a);
    }
    r.expectEnd();
    return m;
}

canon::Json toJson(const Verb& m) {
    std::vector<canon::Json> args;
    for (const Verb::Arg& a : m.args)
        args.push_back(
            canon::Object().u("typeMask", a.typeMask).u("filter", a.filter).u("filterKind", a.filterKind).build());
    return canon::Object()
        .u("id", m.id)
        .str("name", m.name)
        .str("desc", m.desc)
        .str("category", m.category)
        .u("head", m.head)
        .u("src", m.src)
        .u("flags", m.flags)
        .u("flagsExt", m.flagsExt)
        .u("extByte", m.extByte)
        .set("args", canon::array(args))
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Verb, "cpp:stock-dd-wire", jsonOf<decodeVerb>);

}  // namespace fugue::proto
