// S2C::MapGrid: initial viewport grid. The record is the MapGrid (repeat-previous grammar sized by the body);
// the same body ALSO drives the footprint machine as a full refresh (decodeMapRefresh).
#include "proto/g2_mapview.h"

namespace fugue::proto {

// Header x0,y0,z as three u16; then literal u32 cell ids, or a 0xFFFFFFFF run token followed by a u8 count.
//   The implicit initial previous id is 0, and a leading run gets one bonus cell.
MapGrid decodeMapGrid(ByteSpan body) {
    if (body.size() < 6) throw DecodeError("short map grid");
    Reader r(body);
    MapGrid m;
    m.x0 = r.u16();
    m.y0 = r.u16();
    m.z = r.u16();
    uint32_t cur = 0;
    bool first = true;
    while (r.more()) {
        uint32_t v = r.u32();  // throws on a truncated token
        if (v == 0xffffffffu) {
            unsigned run = unsigned(r.u8()) + 1;  // throws on a missing count
            if (first) run++;
            m.cells.insert(m.cells.end(), run, cur);
        } else {
            cur = v;
            m.cells.push_back(v);
        }
        first = false;
    }
    return m;
}

canon::Json toJson(const MapGrid& m) {
    std::vector<canon::Json> cells;
    cells.reserve(m.cells.size());
    for (uint32_t c : m.cells) cells.push_back(canon::u(c));
    return canon::Object().u("x0", m.x0).u("y0", m.y0).u("z", m.z).set("cells", canon::array(cells)).build();
}

namespace {

// The registry entry: the grid record, with the refresh machine advanced as a side effect (its errors are swallowed).
canon::Json json(State& st, ByteSpan body) {
    MapGrid m = decodeMapGrid(body);
    try {
        (void)decodeMapRefresh(st, S2C::MapGrid, body);
    } catch (const std::exception&) {}
    return toJson(m);
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::MapGrid, "cpp:stock-dd-wire", json);

}  // namespace
}  // namespace fugue::proto
