// The map-window refresh family (MapRefreshFull MapRefresh MapGrid AreaGrid): decodeMapRefresh and decodeAreaGrid
// (proto/messages.h) run the footprint state machine kept here, in State::get<g2::MapView>(), beside the shared MapView
// struct. World bounds and window size are read from State::mapView, which the Bounds decoder already fills.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "proto/registry.h"

namespace fugue::proto::g2 {

struct MapView {
    uint16_t originX = 0, originY = 0, z = 0;            // window origin after the last successful refresh
    std::vector<std::pair<uint16_t, uint16_t>> pending;  // footprint the next AreaGrid must re-read
    bool dirty = false;                                  // a refresh armed a grid read for the next AreaGrid
};

// The RLE cell stream for exactly n cells, wide ids: [id u32], or [0xffffffff][n u8] repeating the last literal id for
// n+1 cells (the first literal is preceded by an implicit 0xffff).
std::vector<uint32_t> readRle(Reader& r, size_t n);

}  // namespace fugue::proto::g2
