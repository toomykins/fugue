// Traditional DES crypt(3) and the password-hash helpers. Pure functions, no Qt.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include "net/bytes.h"

namespace fugue::account {
using fugue::Bytes;
using fugue::ByteSpan;

struct CryptResult {
    bool ok = false;
    std::string value;  // 13 chars: salt + 11 B64
    std::string error;  // "invalid crypt salt: ..." when !ok
};

// Only the first 8 password bytes count, low 7 bits each. Salt: two characters from "./0-9A-Za-z".
CryptResult desCrypt(ByteSpan password, const char salt[2]);

// level 1: crypt(password, "DM").
std::string pwordCrypt(ByteSpan password);
// level 2: lowercase md5 hex of (password + level1). This is also the key.txt line-1 stage-2 key string.
std::string pwordCryptLevel2(ByteSpan password);
// "level2;level1" -- the 0x0088 DownloadKey credential field.
std::string downloadKeyCredential(ByteSpan password);

}  // namespace fugue::account
