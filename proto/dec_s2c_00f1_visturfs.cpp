// S2C::VisTurfs: the remote turfs a vis_contents names (vg's camera console), sent just before the InView that sets the
// holder's vis_contents.
//   [count u16] count x [turf cell index u32]
//   count cells of the MapRefresh RLE stream (proto/g2_mapview.h readRle): each turf's appearance id
//   count cells of a second such stream: each turf's turf-registry (AreaGrid) index
//   while bytes remain: [n u16] n x [index u32][appid u32], the AreaGrid bind table
// Wide ids only.
#include "proto/g2_mapview.h"

namespace fugue::proto {

VisTurfs decodeVisTurfs(ByteSpan body) {
    Reader r(body);
    VisTurfs m;
    unsigned n = r.u16();
    for (unsigned i = 0; i < n; i++) m.cells.push_back(r.u32());
    m.looks = g2::readRle(r, n);
    m.areas = g2::readRle(r, n);
    if (r.more())
        for (unsigned k = r.u16(), i = 0; i < k; i++) {
            AreaGrid::Bind b;
            b.index = r.u32();
            b.appid = r.u32();
            m.binds.push_back(b);
        }
    return m;
}

}  // namespace fugue::proto
