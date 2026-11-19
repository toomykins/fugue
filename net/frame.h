// Wire framing.
//   s2c: [type u16 BE][len u16 BE][body]   (no seq prefix)
//   c2s: [seq u16 BE][type u16 BE][len u16 BE][body]
//   extended escape (either direction, bodies > 0xFFFF): [Extended][0006][realType u16 BE][realLen u32 BE][body]
#pragma once
#include <optional>
#include <string>
#include "net/bytes.h"
#include "proto/msg.h"

namespace fugue::net {

constexpr uint32_t kMaxShortPayload = 0xFFFF;
constexpr uint32_t kMaxExtPayload = 0xFFFFFF;

struct Frame {
    uint16_t type = 0;
    Bytes payload;
};

enum class ParseStatus { Ok, Incomplete, Error };
struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    size_t used = 0;
    std::string err;
};

// Parse ONE frame from the front of `data` (s2c form; for c2s strip the 2-byte seq first).
// Incomplete means more bytes are needed; at EOF that is the "short payload" error.
ParseResult parseFrame(ByteSpan data, Frame& out);

// Encode one frame (short form, or the extended escape above 0xFFFF). nullopt above 0xFFFFFF.
std::optional<Bytes> encodeFrame(uint16_t type, ByteSpan payload);

// c2s: seq(2 BE) ++ encodeFrame(type, encryptBody(plainBody, key)).
std::optional<Bytes> encodeC2S(uint16_t seq, uint16_t type, ByteSpan plainBody, uint32_t key);

// Incremental s2c stream reader: feed arbitrary chunks, pull whole frames.
class FrameReader {
public:
    void feed(ByteSpan chunk) { buf_.insert(buf_.end(), chunk.begin(), chunk.end()); }
    // Ok: frame returned. Incomplete: need more. Error: stream is unusable (see error()).
    ParseStatus next(Frame& out);
    size_t buffered() const { return buf_.size() - pos_; }
    const std::string& error() const { return err_; }

private:
    Bytes buf_;
    size_t pos_ = 0;
    std::string err_;
};

}  // namespace fugue::net
