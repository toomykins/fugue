// Body cipher: the self-keying additive stream cipher applied to every post-handshake frame body in
// both directions (key = K_login), and to the handshake reply (key = base session key).
#pragma once
#include "net/bytes.h"

namespace fugue::net {

// Encrypt and append the 1-byte checksum (= running sum of the plaintext). Output is plain.size()+1.
Bytes encryptBody(ByteSpan plain, uint32_t key);
// Same stream without the checksum byte (the C2S::Login body).
Bytes encryptBodyNoChecksum(ByteSpan plain, uint32_t key);
// Inverse of encryptBody. Returns false when the input is empty or the checksum does not match.
bool decryptBody(ByteSpan cipher, uint32_t key, Bytes& plainOut);
Bytes decryptBodyNoChecksum(ByteSpan cipher, uint32_t key);

}  // namespace fugue::net
