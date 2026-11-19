// S2C::ImageCreate: client.images += /image  (create). See proto/g4_imageop.cpp for the layout.
#include "proto/g4_imageop.h"

namespace fugue::proto {

// Layout: see g4_imageop.cpp.
ImageOp decodeImageCreate(ByteSpan body) { return g4::readCreateModify(S2C::ImageCreate, body); }

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ImageCreate, "cpp:stock-dd-wire", jsonOf<decodeImageCreate>);

}  // namespace fugue::proto
