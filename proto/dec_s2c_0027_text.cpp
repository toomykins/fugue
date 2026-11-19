// S2C::Text (plain text output), S2C::WorldName and S2C::HostMessage: each a cstring first.
#include "proto/registry.h"

namespace fugue::proto {

// [text \0 ...]: the bytes up to the first NUL, or the whole body when there is none. What follows the NUL is not read.
Text decodeText(ByteSpan body) {
    size_t n = 0;
    while (n < body.size() && body[n] != 0) n++;
    return Text{std::string(reinterpret_cast<const char*>(body.data()), n)};
}

}  // namespace fugue::proto
