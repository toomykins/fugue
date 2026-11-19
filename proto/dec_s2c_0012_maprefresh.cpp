// S2C::MapRefreshFull: map-window refresh (appearance grid), always a full refresh - stateful, see proto/g2_mapview.h.
#include "proto/g2_mapview.h"

namespace fugue::proto {
namespace {

// Machine: proto/g2_mapview.cpp.
MapRefresh decode(State& st, ByteSpan body) { return decodeMapRefresh(st, S2C::MapRefreshFull, body); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::MapRefreshFull, "cpp:stock-dd-wire", jsonOf<decode>);

}  // namespace
}  // namespace fugue::proto
