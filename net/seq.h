#pragma once
#include <cstdint>
namespace fugue::net {
// c2s frame sequence stepping: next = (seq * 0x43D4) mod 0xFFF1, 0 -> 1.
uint16_t nextSeq(uint16_t seq);
}  // namespace fugue::net
