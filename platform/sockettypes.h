#pragma once
#include <cstdint>
namespace fugue::socket {
#ifdef _WIN32
using Handle = intptr_t;  // SOCKET is pointer-sized; -1 denotes INVALID_SOCKET.
#else
using Handle = int;
#endif
}  // namespace fugue::socket
