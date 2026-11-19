// MD5 (RFC 1321). Used only for the per-install machine digest, whose 16 raw bytes ride in the C2S::Login credential
// at [21:37]. No Qt.
#pragma once
#include <array>
#include "net/bytes.h"

namespace fugue::net {
std::array<uint8_t, 16> md5(ByteSpan data);
std::array<uint8_t, 16> md5(std::string_view s);
}  // namespace fugue::net
