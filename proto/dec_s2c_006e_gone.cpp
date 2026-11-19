// S2C::MobGone, S2C::ObjGone: the records the server no longer has. [id u32]...; a trailing partial id is ignored.
#include "proto/registry.h"

namespace fugue::proto {

Gone decodeGone(ByteSpan body) {
    Reader r(body);
    Gone m;
    while (r.remaining() >= 4) m.ids.push_back(r.u32());
    return m;
}

}  // namespace fugue::proto
