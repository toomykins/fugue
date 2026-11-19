// S2C::BrowseRsc: browse_rsc batch - a REPEATING [ordinal u32][cstring] loop.
#include "proto/registry.h"

namespace fugue::proto {

// A repeating [ordinal u32][cstring] loop, run to the end of the body. An empty body means no records.
BrowseRsc decodeBrowseRsc(ByteSpan body) {
    Reader r(body);
    BrowseRsc m;
    while (r.more()) {
        BrowseRsc::Record rec;
        rec.resID = r.u32();
        rec.name = r.cstr();
        m.records.push_back(std::move(rec));
    }
    return m;
}

canon::Json toJson(const BrowseRsc& m) {
    std::vector<canon::Json> recs;
    for (const auto& rec : m.records)
        recs.push_back(canon::Object().u("resID", rec.resID).str("name", rec.name).build());
    return canon::Object().set("records", canon::array(recs)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::BrowseRsc, "cpp:stock-dd-wire", jsonOf<decodeBrowseRsc>);

}  // namespace fugue::proto
