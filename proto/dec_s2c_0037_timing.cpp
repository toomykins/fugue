// S2C::Timing: [tick ms u32][client frame ms u32], each read only when the body holds it.
#include "proto/registry.h"

namespace fugue::proto {

Timing decodeTiming(ByteSpan body) {
    Reader r(body);
    Timing m;
    if (r.remaining() >= 4) m.tickMs = r.u32();
    if (r.remaining() >= 4) m.frameMs = r.u32();
    return m;
}

}  // namespace fugue::proto
