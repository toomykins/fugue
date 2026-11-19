// S2C::ClientAppearance: client flyweight rebind. [appid u32][u16 x 1..4]; 12 bytes in the usual case.
#include "proto/registry.h"

namespace fugue::proto {

// Layout [appid u32][u16 x 1..4]. The appid equals the preceding Appearance's appid.
//   The client reads an id, then four i16 values, length-guarded after the first u16, so 6/8/10/12-byte bodies are legal.
//   Absent trailing u16s are 0, with nFields = count present.
ClientAppearance decodeClientAppearance(ByteSpan body) {
    if (body.size() < 6 || body.size() > 12 || (body.size() & 1))
        throw DecodeError("0x38 body is " + std::to_string(body.size()) + " bytes, want 6, 8, 10 or 12");
    Reader r(body);
    ClientAppearance m;
    m.appid = r.u32();
    while (r.more()) m.fields[m.nFields++] = r.u16();
    return m;
}

canon::Json toJson(const ClientAppearance& m) {
    std::vector<canon::Json> f;
    for (uint16_t v : m.fields) f.push_back(canon::u(v));
    return canon::Object().u("appid", m.appid).set("fields", canon::array(f)).u("nFields", m.nFields).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ClientAppearance, "cpp:stock-dd-wire", jsonOf<decodeClientAppearance>);

}  // namespace fugue::proto
