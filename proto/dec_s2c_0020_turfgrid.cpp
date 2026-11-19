// S2C::AreaGrid: per-cell area-index grid + bind trailer; consumes the footprint armed by the preceding refresh.
#include "proto/g2_mapview.h"

namespace fugue::proto {

// decodeAreaGrid is in proto/g2_mapview.cpp, beside the machine it reads.
FUGUE_REGISTER_DECODER(Dir::S2C, S2C::AreaGrid, "cpp:stock-dd-wire", jsonOf<decodeAreaGrid>);

}  // namespace fugue::proto
