// Guest login credential (the 47-byte C2S::Login body) and the per-install machine identity behind it.
//
// The 16 scaffold bytes are the public Guest key block's prefix; the server refuses a login without them.
#pragma once
#include <array>
#include <string>
#include "net/bytes.h"

namespace fugue::net {

using Digest16 = std::array<uint8_t, 16>;

// computer_id = CRC-32/XFER over the 32 LOWERCASE ASCII hex characters of the digest.
uint32_t computerId(const Digest16& d);

// Per-install digest: the first i >= 0 whose MD5("fugue-cid-v1:install:" + sid + ":" + decimal(i)) gives
// computer_id % 1000 == 523.
Digest16 digestForInstall(const std::string& sid);

// Build the 47-byte credential: scaffold[0:16] | window[16:20] | 0x7f | digest[21:37] |
// (computer_id ^ 0x3C201960) LE [37:41] | 0x1a | 5 x 00. `window` is the guest's cauth key for the endpoint, LE
// (net/cauth.h: stock DS caches it per "guest@host:port"); DD ignores it for identity.
Bytes mintGuestCredential(const Digest16& digest, const uint8_t window[4]);

// Per-install SID: 24 lowercase hex chars persisted at <dir>/sid (0600), created on first use. The default
// directory is ~/.fugue. When the file cannot be written, *err is set
// and an ephemeral sid is returned rather than failing the login.
std::string defaultStateDir();
std::string loadOrCreateSid(const std::string& dir, std::string* err);

}  // namespace fugue::net
