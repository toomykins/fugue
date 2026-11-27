// The Handshake (version handshake), the base session key, the encrypted reply, nonce and K_login.
#pragma once
#include "net/bytes.h"

namespace fugue::net {

// The Handshake body, little-endian: proto, field2, field3 (u32 each), then, when extended, extra1 and extra2
// (u32 each), then tail (u16). 14 bytes, or 22 extended. The stock 516 client sends the extended form with proto=516,
// field2=276, field3=<client nonce>, extra1=(build << 16) | <first seq>, extra2=0.
struct Handshake {
    uint32_t proto = 0;
    uint32_t field2 = 0;
    uint32_t field3 = 0;
    uint16_t tail = 0;
    bool extended = false;
    uint32_t extra1 = 0;
    uint32_t extra2 = 0;
};

Bytes encodeHandshake(const Handshake& h);

// Base session key = field3 + (field2 << 16) + proto (mod 2^32). Keys the handshake reply.
uint32_t sessionKey(const Handshake& h);

// Decrypted reply plaintext: [u32 major][u32 field2][3 flag bytes][nonce...]. nonce = plain[11:].
struct HandshakeReply {
    uint32_t major = 0;
    uint32_t field2 = 0;
    uint8_t flags[3] = {0, 0, 0};
    // Stock 516.1688: the third handshake flag's bit 1 selects u32 verb IDs.
    // This is independent of 0x003b's flags (the 33k-proc fixture distinguishes them).
    bool wideVerbIds() const { return (flags[2] & 2) != 0; }
    Bytes nonce;
};
// false when plain is shorter than the 11-byte fixed prefix.
bool parseHandshakeReply(ByteSpan plain, HandshakeReply& out);

// K_login = base + delta(nonce), where delta is the nonce word after the first terminator word
// (((w + 0x71bd632f) >> 1) & 0x02004000 != 0). No terminator, or one in the last word, gives K_login = base.
uint32_t loginKey(uint32_t base, ByteSpan nonce);

}  // namespace fugue::net
