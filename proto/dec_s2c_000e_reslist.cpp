// S2C::ResourceList: resource-availability list: concatenated 5-byte [content-id u32][type u8], no header.
// The resource ordinal (index across frames) is not part of the decoded value; the running total is kept in State
// so a consumer can derive it.
#include "proto/registry.h"

namespace fugue::proto {
namespace {
struct ResListState {
    uint64_t total = 0;
};  // entries seen so far in this session (next ordinal)
}  // namespace

// Array of [content-id u32][type u8] with no count header. Ordinals run across frame splits.
ResourceList decodeResourceList(State& st, ByteSpan body) {
    if (body.size() % 5 != 0) throw DecodeError("0x0e length not a multiple of 5");
    Reader r(body);
    ResourceList m;
    m.entries.reserve(body.size() / 5);
    while (r.more()) {
        ResourceList::Entry e;
        e.id = r.u32();
        e.type = r.u8();
        m.entries.push_back(e);
    }
    st.get<ResListState>().total += m.entries.size();
    return m;
}

canon::Json toJson(const ResourceList& m) {
    std::vector<canon::Json> es;
    es.reserve(m.entries.size());
    for (const auto& e : m.entries) es.push_back(canon::Object().u("id", e.id).u("type", e.type).build());
    return canon::array(es);
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ResourceList, "cpp:stock-dd-wire", jsonOf<decodeResourceList>);

}  // namespace fugue::proto
