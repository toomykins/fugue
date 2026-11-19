// S2C::ImageDelete: client.images -= ... One frame carries EVERY image removed in the same tick: n x u32 ids.
#include "proto/g4_imageop.h"

namespace fugue::proto {

// The body is n x u32 ids. The first fills imageID and the rest go to moreIDs; the other ImageOp fields stay zero/empty.
ImageOp decodeImageDelete(ByteSpan body) {
    if (body.size() < 4 || body.size() % 4 != 0)
        throw DecodeError("0x34 body of " + std::to_string(body.size()) + " bytes is not a list of u32 image ids");
    Reader r(body);
    ImageOp m;
    m.op = S2C::ImageDelete;
    m.imageID = r.u32();
    while (r.more()) m.moreIDs.push_back(r.u32());
    return m;
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ImageDelete, "cpp:stock-dd-wire", jsonOf<decodeImageDelete>);

}  // namespace fugue::proto
