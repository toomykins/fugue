// S2C::Bounds: map / view bounds. 24 bytes, twelve u16 LE.
//   u16[0] maxX   u16[1] maxY      (world size in tiles)
//   u16[2] winW   u16[3] winH      (map window, tiles; can be larger than the view)
//   u16[4..5]                      (unnamed)
//   u16[6] viewW  u16[7] viewH     (displayed view extent per axis)
//   u16[8..11]                     (unnamed)
#include "proto/registry.h"

namespace fugue::proto {

// The minimum body is 14 bytes. viewH defaults to viewW when the body is < 16 bytes. view = (viewW-1)/2 in 16-bit
//   arithmetic, so viewW == 0 wraps to 32767, kept as-is. rest = body[8:24] only when the body is >= 24 bytes.
//   Offsets 4/6 are the window size, not the map size.
Bounds decodeBounds(State& st, ByteSpan body) {
    if (body.size() < 14) throw DecodeError("short bounds");
    Reader r(body);
    Bounds m;
    m.maxX = r.u16();
    m.maxY = r.u16();
    m.winW = r.u16();
    m.winH = r.u16();
    r.take(4);
    m.viewW = r.u16();
    m.viewH = r.remaining() >= 2 ? r.u16() : m.viewW;
    m.view = uint16_t(uint16_t(m.viewW - 1) / 2);
    if (body.size() >= 24) {
        auto rest = body.subspan(8, 16);
        m.rest.assign(rest.begin(), rest.end());
    }

    // Record the map bounds and window size on every Bounds; the map refresh decoders depend on them.
    st.mapView.haveBounds = true;
    st.mapView.maxX = m.maxX;
    st.mapView.maxY = m.maxY;
    st.mapView.winW = m.winW;
    st.mapView.winH = m.winH;
    return m;
}

canon::Json toJson(const Bounds& m) {
    return canon::Object()
        .u("maxX", m.maxX)
        .u("maxY", m.maxY)
        .u("winW", m.winW)
        .u("winH", m.winH)
        .u("view", m.view)
        .u("viewW", m.viewW)
        .u("viewH", m.viewH)
        .bytes("rest", m.rest)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Bounds, "cpp:stock-dd-wire", jsonOf<decodeBounds>);

}  // namespace fugue::proto
