// S2C::Link: link(url). [url\0], nothing after.
#include "proto/registry.h"

namespace fugue::proto {

Link decodeLink(ByteSpan body) {
    Reader r(body);
    Link m;
    m.url = r.cstr();
    r.expectEnd();
    return m;
}

}  // namespace fugue::proto
