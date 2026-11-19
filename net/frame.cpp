#include "net/frame.h"

namespace fugue::net {

// The Extended escape carries a u16 real type and a u32 length, and lengths above 0xffffff are rejected. The escape
// is taken for any len >= 6, not only len == 6: the stock handler branches on the type alone.
ParseResult parseFrame(ByteSpan d, Frame& out) {
    ParseResult r;
    if (d.size() < 4) return r;
    uint16_t typ = be16(&d[0]);
    uint32_t len = be16(&d[2]);
    size_t hdr = 4;
    if (typ == S2C::Extended && len >= 6) {
        if (d.size() < 10) return r;
        typ = be16(&d[4]);
        len = be32(&d[6]);
        if (len > kMaxExtPayload) {
            r.status = ParseStatus::Error;
            r.err = "extended payload exceeds 0xffffff";
            return r;
        }
        hdr = 10;
    }
    if (d.size() < hdr + len) return r;
    out.type = typ;
    out.payload.assign(d.begin() + hdr, d.begin() + hdr + len);
    r.status = ParseStatus::Ok;
    r.used = hdr + len;
    return r;
}

std::optional<Bytes> encodeFrame(uint16_t type, ByteSpan payload) {
    if (payload.size() > kMaxExtPayload) return std::nullopt;
    Bytes out;
    if (payload.size() > kMaxShortPayload) {
        out.reserve(10 + payload.size());
        putBe16(out, C2S::Extended);
        putBe16(out, 6);
        putBe16(out, type);
        putBe32(out, uint32_t(payload.size()));
    } else {
        out.reserve(4 + payload.size());
        putBe16(out, type);
        putBe16(out, uint16_t(payload.size()));
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

}  // namespace fugue::net
