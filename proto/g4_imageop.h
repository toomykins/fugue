// Shared by the client.images decoders ImageCreate / ImageDelete / ImageModify: all three decode to proto::ImageOp
// (proto/messages.h), so the same key set is emitted.
#pragma once
#include <cstdint>

#include "proto/registry.h"

namespace fugue::proto::g4 {

// ImageCreate / ImageModify body: [imageID u32][appid u32][locKind u8][loc u32][m u16] m x {type u8, value u32}.
ImageOp readCreateModify(uint16_t typ, ByteSpan body);

}  // namespace fugue::proto::g4
