#include "proto/g4_imageop.h"

namespace fugue::proto {

canon::Json toJson(const ImageOp& m) {
    std::vector<canon::Json> vis, more;
    for (const VisItem& v : m.visContents) vis.push_back(canon::Object().u("type", v.type).u("value", v.value).build());
    for (uint32_t id : m.moreIDs) more.push_back(canon::u(id));
    return canon::Object()
        .u("appid", m.appid)
        .u("cell", m.cell)
        .u("imageID", m.imageID)
        .u("locKind", m.locKind)
        .u("locRef", m.locRef)
        .set("moreIDs", canon::array(more))
        .u("op", m.op)
        .set("visContents", canon::array(vis))
        .build();
}

namespace g4 {

// Layout: [image-id u32][appid u32][loc-kind u8][loc]; a turf loc is a flat u32 cell, a mob/obj loc is
// a ref. The two bytes after the loc are the vis_contents count m, then m x {Type u8, Value u32}.
// A turf (loc-kind 0x07) fills cell, otherwise locRef.
// The body must be exactly 15 + 5m bytes; anything else throws rather than guessing a framing.
ImageOp readCreateModify(uint16_t typ, ByteSpan body) {
    Reader r(body);
    ImageOp m;
    m.op = typ;
    m.imageID = r.u32();
    m.appid = r.u32();
    m.locKind = r.u8();
    uint32_t loc = r.u32();
    if (m.locKind == 0x07)
        m.cell = loc;
    else
        m.locRef = loc;
    for (unsigned n = r.u16(), i = 0; i < n; i++) {
        VisItem v;
        v.type = r.u8();
        v.value = r.u32();
        m.visContents.push_back(v);
    }
    r.expectEnd();
    return m;
}

}  // namespace g4
}  // namespace fugue::proto
