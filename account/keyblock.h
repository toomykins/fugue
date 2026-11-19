// key.txt blocks: marker scan (keyNames), per-account line-1 extraction, and the line-1 two-stage codec.
// Line 1 is stage 1 (run-length groups of base64-style characters) then stage 2 (subtractive cipher). The decoded
// plaintext (= blob[1:], since blob[0] is the stage-2 key-correctness byte) is a tagged container, NOT a fixed
// name+13+tail layout: optional server:/friends:/ds:/save: sections, then the identity, then a fixed trailer whose
// size is set by the format letter line1[0] (A:4 B:8 C:12 else:13), as stock reads it.
// Key blocks are bearer tokens; nothing here logs them.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "net/bytes.h"

namespace fugue::account {

// Account names from "[BEGIN KEY <name>]" / "BEGIN KEY <name>" marker lines, file order. Reads ONLY the
// markers, never the encoded lines under them.
std::vector<std::string> keyNames(std::string_view keytxt);

// The encoded line immediately after the marker for `acct` (ASCII case-insensitive), CR/space-trimmed.
std::optional<std::string> keyBlockLine1(std::string_view keytxt, std::string_view acct, std::string* err = nullptr);

struct KeyBlockFields {
    fugue::Bytes identity;  // variable length, as mintAccountLoginRecord wants it
    uint32_t installId = 0;
    uint32_t innerKey = 0;
    bool haveHubIds = false;  // whether the format letter carries installId/innerKey
};

// Stages 1+2 -> blob, checking blob[0]==0. keyStr = pwordCryptLevel2(password) (32 hex chars).
std::optional<fugue::Bytes> keyBlockBlob(std::string_view line1, std::string_view keyStr, std::string* err = nullptr);
// Full decode with the length / terminator / nonzero-credential framing checks. A wrong password fails here.
std::optional<KeyBlockFields> decodeKeyBlock(std::string_view line1, std::string_view account, std::string_view keyStr,
                                             std::string* err = nullptr);
// Synthesis (inverse of the above) for tests: blob -> line-1 text. `seed` is the 2 leading characters.
std::string encodeKeyLine1(fugue::ByteSpan blob, std::string_view keyStr, std::string_view seed = "xq");
// Builds blob = [00][identity][00][00 installID innerKey 3 bytes] for decodeKeyBlock.
fugue::Bytes buildKeyBlob(fugue::ByteSpan identity, uint32_t installId, uint32_t innerKey, fugue::ByteSpan tail3);

}  // namespace fugue::account
