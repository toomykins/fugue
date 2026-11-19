// Wire layout: header [origin x u16][origin y u16][z u16]; a full refresh is forced (0x7fff) for MapRefreshFull,
// MapGrid and a z change; the walk is y-outer, x-inner; the RLE cell stream has N+1 run semantics with lastID=0xffff;
// the AreaGrid trailer is [count u16] n x [index][appid]; ids are u32. The preserve rectangle and world-bounds test use
// 16-bit wraparound arithmetic, and view state advances only when the whole frame decodes.
#include "proto/g2_mapview.h"

namespace fugue::proto {

namespace {

using Cells = std::vector<std::pair<uint16_t, uint16_t>>;

Cells footprint(const State& st, const g2::MapView& v, uint16_t originX, uint16_t originY, bool full) {
    const auto& b = st.mapView;
    int32_t x1, y1, x2, y2;
    if (full) {
        x1 = y1 = x2 = y2 = 0x7fff;
    } else {
        x1 = int16_t(uint16_t(v.originX - originX));
        y1 = int16_t(uint16_t(v.originY - originY));
        x2 = int16_t(uint16_t(uint16_t(x1) - 1 + b.winW));
        y2 = int16_t(uint16_t(uint16_t(y1) - 1 + b.winH));
    }
    Cells out;
    out.reserve(size_t(b.winW) * b.winH);
    for (int32_t y = 0; y < b.winH; y++) {
        for (int32_t x = 0; x < b.winW; x++) {
            if (x >= x1 && x <= x2 && y >= y1 && y <= y2) continue;  // preserved by the client
            int32_t wx = int16_t(uint16_t(uint16_t(x) + originX));
            int32_t wy = int16_t(uint16_t(uint16_t(y) + originY));
            if (wx < 0 || wy < 0 || wx >= int16_t(b.maxX) || wy >= int16_t(b.maxY)) continue;  // outside world
            out.push_back({uint16_t(wx), uint16_t(wy)});
        }
    }
    return out;
}

}  // namespace

// The RLE cell stream for exactly n cells, wide ids.
std::vector<uint32_t> g2::readRle(Reader& r, size_t n) {
    std::vector<uint32_t> out;
    out.reserve(n);
    uint32_t last = 0xffff, cur = 0;
    unsigned run = 0;
    for (size_t i = 0; i < n; i++) {
        if (run > 0) {
            run--;
            out.push_back(cur);
            continue;
        }
        uint32_t v = r.u32();
        if (v == 0xffffffffu) {
            cur = last;
            run = r.u8();
        } else {
            cur = last = v;
        }
        out.push_back(cur);
    }
    return out;
}

MapRefresh decodeMapRefresh(State& st, uint16_t typ, ByteSpan body) {
    if (typ != S2C::MapRefreshFull && typ != S2C::MapRefresh && typ != S2C::MapGrid)
        throw DecodeError("not a map refresh type");
    if (st.mapView.winW == 0 || st.mapView.winH == 0) throw DecodeError("map window size unknown (no 0x003e yet)");
    auto& v = st.get<g2::MapView>();
    Reader r(body);
    MapRefresh m;
    m.originX = r.u16();
    m.originY = r.u16();
    m.z = r.u16();
    m.full = typ == S2C::MapRefreshFull || typ == S2C::MapGrid || m.z != v.z;
    Cells cells = footprint(st, v, m.originX, m.originY, m.full);
    std::vector<uint32_t> ids = g2::readRle(r, cells.size());
    r.expectEnd();

    m.cells.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); i++) m.cells.push_back({cells[i].first, cells[i].second, ids[i]});
    v.originX = m.originX;
    v.originY = m.originY;
    v.z = m.z;
    v.pending = std::move(cells);
    v.dirty = true;
    return m;
}

AreaGrid decodeAreaGrid(State& st, ByteSpan body) {
    auto& v = st.get<g2::MapView>();
    Reader r(body);
    AreaGrid m;
    // The armed flag is cleared right after the grid read, before the trailer can fail.
    if (v.dirty) {
        std::vector<uint32_t> ids = g2::readRle(r, v.pending.size());
        m.cells.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); i++) m.cells.push_back({v.pending[i].first, v.pending[i].second, ids[i]});
        v.dirty = false;
    }
    for (unsigned n = r.u16(), i = 0; i < n; i++) {
        AreaGrid::Bind b;
        b.index = r.u32();
        b.appid = r.u32();
        m.binds.push_back(b);
    }
    r.expectEnd();
    return m;
}

canon::Json toJson(const MapRefresh& m) {
    std::vector<canon::Json> js;
    js.reserve(m.cells.size());
    for (const MapRefresh::Cell& c : m.cells)
        js.push_back(canon::Object().u("x", c.x).u("y", c.y).u("appid", c.appid).build());
    return canon::Object()
        .u("originX", m.originX)
        .u("originY", m.originY)
        .u("z", m.z)
        .boolean("full", m.full)
        .set("cells", canon::array(js))
        .build();
}

canon::Json toJson(const AreaGrid& m) {
    std::vector<canon::Json> cells, binds;
    cells.reserve(m.cells.size());
    for (const AreaGrid::Cell& c : m.cells)
        cells.push_back(canon::Object().u("x", c.x).u("y", c.y).u("index", c.index).build());
    for (const AreaGrid::Bind& b : m.binds)
        binds.push_back(canon::Object().u("index", b.index).u("appid", b.appid).build());
    return canon::Object().set("cells", canon::array(cells)).set("binds", canon::array(binds)).build();
}

}  // namespace fugue::proto
