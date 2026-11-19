// The cursor every decoder reads its body with: bounds-checked, little-endian, front to back.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

#include "net/bytes.h"

namespace fugue::proto {

// Thrown by Reader and by decoders on malformed input; the caller reports the message as undecodable.
struct DecodeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(ByteSpan b) : b_(b) {}
    size_t remaining() const { return b_.size() - p_; }
    bool more() const { return p_ < b_.size(); }
    size_t pos() const { return p_; }
    size_t size() const { return b_.size(); }

    uint8_t u8() {
        need(1);
        return b_[p_++];
    }
    uint16_t u16() {
        need(2);
        auto v = le16(&b_[p_]);
        p_ += 2;
        return v;
    }
    int16_t s16() { return int16_t(u16()); }
    uint32_t u32() {
        need(4);
        auto v = le32(&b_[p_]);
        p_ += 4;
        return v;
    }
    // One byte, or, when its high bit is set, a u32 re-read from the same offset.
    uint32_t varint() {
        need(1);
        return (b_[p_] & 0x80) ? u32() : u8();
    }
    ByteSpan take(size_t n) {
        need(n);
        auto s = b_.subspan(p_, n);
        p_ += n;
        return s;
    }
    ByteSpan rest() { return take(remaining()); }
    // The bytes read since `start` (a pos() taken earlier).
    ByteSpan since(size_t start) const { return b_.subspan(start, p_ - start); }
    // NUL-terminated string (terminator consumed, not returned). Throws if there is no NUL.
    std::string cstr() {
        for (size_t k = p_; k < b_.size(); k++)
            if (b_[k] == 0) {
                std::string s(reinterpret_cast<const char*>(b_.data()) + p_, k - p_);
                p_ = k + 1;
                return s;
            }
        throw DecodeError("unterminated string at " + std::to_string(p_));
    }
    void expectEnd() const {
        if (remaining()) throw DecodeError("trailing " + std::to_string(remaining()) + " bytes");
    }

private:
    void need(size_t n) const {
        if (remaining() < n)
            throw DecodeError("short body: need " + std::to_string(n) + " at " + std::to_string(p_) + ", have " +
                              std::to_string(remaining()));
    }
    ByteSpan b_;
    size_t p_ = 0;
};

}  // namespace fugue::proto
