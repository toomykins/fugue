// S2C::VisContents: vis_contents of turfs (tag 1, with the turf's appearance), objs and mobs. [N u16] then records
// until the body ends; N is cross-checked.
#include "proto/registry.h"

namespace fugue::proto {

// Header [N u16], then per record [tag u8]: tag 1 = [x u16][y u16][z u16][appid u32][n u16]; other tags =
//   [ref u32][n u16]; then n x [type u8][value u32]. The
//   decoder loops until the body ends, then requires the header count to equal the records parsed.
VisContents decodeVisContents(ByteSpan body) {
    if (body.size() < 2) throw DecodeError("short 0xd8");
    Reader r(body);
    VisContents m;
    unsigned n = r.u16();
    while (r.more()) {
        VisContents::Record rec;
        rec.tag = r.u8();
        if (rec.tag == 0x01) {
            rec.x = r.u16();
            rec.y = r.u16();
            rec.z = r.u16();
            rec.appid = r.u32();
        } else {
            rec.ref = r.u32();
        }
        for (unsigned k = r.u16(), i = 0; i < k; i++) {
            VisItem v;
            v.type = r.u8();
            v.value = r.u32();
            rec.contents.push_back(v);
        }
        m.records.push_back(std::move(rec));
    }
    if (m.records.size() != n) throw DecodeError("0xd8 header count != records parsed");
    return m;
}

canon::Json toJson(const VisContents& m) {
    std::vector<canon::Json> recs;
    for (const VisContents::Record& rec : m.records) {
        std::vector<canon::Json> contents;
        for (const VisItem& v : rec.contents)
            contents.push_back(canon::Object().u("type", v.type).u("value", v.value).build());
        recs.push_back(canon::Object()
                           .u("tag", rec.tag)
                           .u("x", rec.x)
                           .u("y", rec.y)
                           .u("z", rec.z)
                           .u("ref", rec.ref)
                           .u("appid", rec.appid)
                           .set("contents", canon::array(contents))
                           .build());
    }
    return canon::Object().set("records", canon::array(recs)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::VisContents, "cpp:stock-dd-wire", jsonOf<decodeVisContents>);

}  // namespace fugue::proto
