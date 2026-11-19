// S2C::Hud: client.screen membership. A flat list of self-framed records.
#include "proto/registry.h"

namespace fugue::proto {

// Each record is [op u8][end u16] then its fields; end is a cumulative END OFFSET from the body start.
//   op 01 remove (9 bytes):      [index u32][flags u16]
//   op 02 / 03 add (14 bytes):   [kind u8][index u32][ver u16][appid u32]
//   Fields not present in a record are 0.
Hud decodeHud(ByteSpan body) {
    Reader r(body);
    Hud m;
    while (r.more()) {
        size_t start = r.pos();
        if (r.remaining() < 3) throw DecodeError("0x76: short record header");
        Hud::Record rec;
        rec.op = r.u8();
        size_t end = r.u16();
        if (end <= start || end > body.size()) throw DecodeError("0x76: bad end offset " + std::to_string(end));
        size_t len = end - start;
        if (rec.op == 0x01) {
            if (len != 9) throw DecodeError("0x76 remove record is not 9 bytes");
            rec.index = r.u32();
            rec.flags = r.u16();
        } else if (rec.op == 0x02 || rec.op == 0x03) {
            if (len != 14) throw DecodeError("0x76 add record is not 14 bytes");
            rec.kind = r.u8();
            rec.index = r.u32();
            rec.ver = r.u16();
            rec.appid = r.u32();
        } else {
            throw DecodeError("0x76: unknown record tag");
        }
        m.records.push_back(rec);
    }
    return m;
}

canon::Json toJson(const Hud::Record& rec) {
    return canon::Object()
        .u("op", rec.op)
        .u("kind", rec.kind)
        .u("index", rec.index)
        .u("ver", rec.ver)
        .u("appid", rec.appid)
        .u("flags", rec.flags)
        .build();
}

canon::Json toJson(const Hud& m) {
    std::vector<canon::Json> out;
    for (const Hud::Record& rec : m.records) out.push_back(toJson(rec));
    return canon::array(out);
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Hud, "cpp:stock-dd-wire", jsonOf<decodeHud>);

}  // namespace fugue::proto
