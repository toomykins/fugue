// S2C::PromptWithdraw: the server no longer waits on an alert() or input().
#include "proto/registry.h"

namespace fugue::proto {

// [id u32][kind u8], nothing after.
PromptWithdraw decodePromptWithdraw(ByteSpan body) {
    Reader r(body);
    PromptWithdraw m;
    m.id = r.u32();
    m.kind = r.u8();
    r.expectEnd();
    return m;
}

}  // namespace fugue::proto
