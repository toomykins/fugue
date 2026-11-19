// S2C::ContentsDelta: usr.contents entering / leaving (model/world.cpp). Four back-to-back u16-counted arrays, no
// header/trailer.
#include "proto/registry.h"

namespace fugue::proto {

// [n u16] n x [id u32]   [n u16] n x [id u32][appid u32]   [n u16] n x [id u32]   [n u16] n x [id u32][appid u32]
ContentsDelta decodeContentsDelta(ByteSpan body) {
    Reader r(body);
    auto ids = [&](std::vector<uint32_t>& out) {
        for (unsigned n = r.u16(); n > 0; n--) out.push_back(r.u32());
    };
    auto pairs = [&](std::vector<ContentsDelta::Pair>& out) {
        for (unsigned n = r.u16(); n > 0; n--) {
            ContentsDelta::Pair p;
            p.id = r.u32();
            p.appearance = r.u32();
            out.push_back(p);
        }
    };
    ContentsDelta m;
    ids(m.addA);
    pairs(m.bindA);
    ids(m.removeB);
    pairs(m.bindB);
    r.expectEnd();
    return m;
}

canon::Json toJson(const ContentsDelta& m) {
    auto ids = [](const std::vector<uint32_t>& v) {
        std::vector<canon::Json> out;
        for (uint32_t id : v) out.push_back(canon::u(id));
        return canon::array(out);
    };
    auto pairs = [](const std::vector<ContentsDelta::Pair>& v) {
        std::vector<canon::Json> out;
        for (const auto& p : v) out.push_back(canon::Object().u("id", p.id).u("appearance", p.appearance).build());
        return canon::array(out);
    };
    return canon::Object()
        .set("addA", ids(m.addA))
        .set("bindA", pairs(m.bindA))
        .set("removeB", ids(m.removeB))
        .set("bindB", pairs(m.bindB))
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ContentsDelta, "cpp:stock-dd-wire", jsonOf<decodeContentsDelta>);

}  // namespace fugue::proto
