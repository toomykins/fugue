// S2C::MeasureText: client.MeasureText() query.
#include <algorithm>

#include "proto/registry.h"

namespace fugue::proto {

// Layout [text\0][style\0][width u16][id u32]. A style with no NUL reads as "", and the six bytes after the text are
// then the width and id (as internal/proto.DecodeMeasureTextRequest reads it).
MeasureText decodeMeasureText(ByteSpan body) {
    Reader r(body);
    MeasureText m;
    m.text = r.cstr();
    if (std::find(body.begin() + ptrdiff_t(r.pos()), body.end(), uint8_t(0)) != body.end()) m.style = r.cstr();
    m.width = r.u16();
    m.id = r.u32();
    r.expectEnd();
    return m;
}

canon::Json toJson(const MeasureText& m) {
    return canon::Object().str("text", m.text).str("style", m.style).u("width", m.width).u("id", m.id).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::MeasureText, "cpp:stock-dd-wire", jsonOf<decodeMeasureText>);

}  // namespace fugue::proto
