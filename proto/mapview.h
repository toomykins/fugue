// Shared map-view state. The map decoders (MapRefreshFull MapRefresh AreaGrid MapGrid) are stateful: they need the
// map bounds from Bounds and the grid from MapGrid, and dropping a MapGrid silently desyncs every later
// MapRefresh/AreaGrid. Ids are wide (u32). The footprint state machine lives in g2_mapview.h.
#pragma once
#include <cstdint>

namespace fugue::proto {

struct MapView {
    // Set by the Bounds decoder (the only writer).
    bool haveBounds = false;
    uint16_t maxX = 0, maxY = 0, winW = 0, winH = 0;
};

}  // namespace fugue::proto
