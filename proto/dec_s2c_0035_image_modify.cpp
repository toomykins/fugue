// S2C::ImageModify: client.images image modify. See proto/g4_imageop.cpp for the layout.
#include "proto/g4_imageop.h"

namespace fugue::proto {

// Layout: see g4_imageop.cpp.
ImageOp decodeImageModify(ByteSpan body) { return g4::readCreateModify(S2C::ImageModify, body); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ImageModify, "cpp:stock-dd-wire", jsonOf<decodeImageModify>);

}  // namespace fugue::proto
