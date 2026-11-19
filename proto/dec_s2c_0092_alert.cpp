// S2C::Alert: an alert() prompt.
#include "proto/registry.h"

namespace fugue::proto {

// [id u32][message\0][title\0][count u16][button\0]*count, nothing after.
Alert decodeAlert(ByteSpan body) {
    Reader r(body);
    Alert m;
    m.id = r.u32();
    m.message = r.cstr();
    m.title = r.cstr();
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.buttons.push_back(r.cstr());
    r.expectEnd();
    return m;
}

}  // namespace fugue::proto
