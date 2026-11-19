#include "net/frame.h"
#include "net/cipher.h"

namespace fugue::net {

ParseStatus FrameReader::next(Frame& out) {
    if (!err_.empty()) return ParseStatus::Error;
    ParseResult r = parseFrame(ByteSpan(buf_).subspan(pos_), out);
    if (r.status == ParseStatus::Error) {
        err_ = r.err;
        return ParseStatus::Error;
    }
    if (r.status != ParseStatus::Ok) return ParseStatus::Incomplete;
    pos_ += r.used;
    if (pos_ > (1u << 20) && pos_ * 2 > buf_.size()) {
        buf_.erase(buf_.begin(), buf_.begin() + pos_);
        pos_ = 0;
    }
    return ParseStatus::Ok;
}

// c2s = seq ++ type ++ len ++ EncryptBody(body, K_login); len counts the checksum byte.
std::optional<Bytes> encodeC2S(uint16_t seq, uint16_t type, ByteSpan plainBody, uint32_t key) {
    auto f = encodeFrame(type, encryptBody(plainBody, key));
    if (!f) return std::nullopt;
    Bytes out;
    putBe16(out, seq);
    out.insert(out.end(), f->begin(), f->end());
    return out;
}

}  // namespace fugue::net
