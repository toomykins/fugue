#include "account/keyblock.h"
#include <cstdint>
#include <cstring>

#include "net/bytes.h"

namespace fugue::account {
using fugue::Bytes;
using fugue::ByteSpan;

namespace {
// Fixed-trailer byte count for a key.txt block's format letter, as stock reads it.
size_t keyBlockTrailerLen(uint8_t letter) {
    switch (letter) {
        case 'A': return 4;
        case 'B': return 8;
        case 'C': return 12;
        default: return 13;
    }
}

std::string_view trimSp(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' ||
                          s.back() == '\v' || s.back() == '\f'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' ||
                          s.front() == '\v' || s.front() == '\f'))
        s.remove_prefix(1);
    return s;
}
// The line-1 trim: trailing CR/LF/space, leading space only.
std::string_view trimLine(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.remove_suffix(1);
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    return s;
}
bool eqFoldAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}
std::vector<std::string_view> splitNl(std::string_view s) {
    std::vector<std::string_view> out;
    size_t st = 0;
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == '\n') {
            out.push_back(s.substr(st, i - st));
            st = i + 1;
        }
    out.push_back(s.substr(st));
    return out;
}
}  // namespace

std::vector<std::string> keyNames(std::string_view keytxt) {
    std::vector<std::string> out;
    for (auto ln : splitNl(keytxt)) {
        ln = trimSp(ln);
        if (!ln.empty() && ln.back() == ']') ln.remove_suffix(1);
        ln = trimSp(ln);
        if (!ln.empty() && ln.front() == '[') ln.remove_prefix(1);
        constexpr std::string_view P = "BEGIN KEY ";
        if (ln.substr(0, P.size()) == P) {
            auto name = trimSp(ln.substr(P.size()));
            if (!name.empty()) out.emplace_back(name);
        }
    }
    return out;
}

std::optional<std::string> keyBlockLine1(std::string_view keytxt, std::string_view acct, std::string* err) {
    std::string marker = "BEGIN KEY " + std::string(acct);
    auto lines = splitNl(keytxt);
    for (size_t i = 0; i < lines.size(); i++) {
        if (eqFoldAscii(trimLine(lines[i]), marker) && i + 1 < lines.size()) return std::string(trimLine(lines[i + 1]));
    }
    if (err)
        *err = "KeyBlockLine1: [BEGIN KEY " + std::string(acct) + "] not found (names are matched case-insensitively)";
    return std::nullopt;
}

std::optional<Bytes> keyBlockBlob(std::string_view line1, std::string_view keyStr, std::string* err) {
    auto fail = [&](std::string m) -> std::optional<Bytes> {
        if (err) *err = std::move(m);
        return std::nullopt;
    };
    const size_t n = line1.size();
    auto c = [&](size_t i) { return uint8_t(line1[i]); };
    Bytes blob;
    size_t i = 2;
    while (i < n) {
        int count = (0x60 - c(i)) & 0xff;
        i++;
        if (count == 0 || count > 0x3f) break;
        while (count > 0) {
            if (i + 4 > n) break;
            uint8_t g0 = uint8_t(c(i) + 0xe0), g1 = uint8_t(c(i + 1) + 0xe0), g2 = uint8_t(c(i + 2) + 0xe0),
                    g3 = uint8_t(c(i + 3) + 0xe0);
            i += 4;
            uint8_t grp[3] = {uint8_t((g3 << 2) | (g2 >> 4)), uint8_t((g2 << 4) | (g1 >> 2)), uint8_t((g1 << 6) | g0)};
            int k = count > 3 ? 3 : count;
            blob.insert(blob.end(), grp, grp + k);
            count -= k;
        }
    }
    if (blob.size() < 3)
        return fail("decodeKeyBlockLine1: decoded blob is " + std::to_string(blob.size()) +
                    " bytes, too short to hold an identity");
    const size_t keylen = keyStr.size();
    if (keylen == 0) return fail("decodeKeyBlockLine1: no cipher key");
    Bytes key(keylen);
    for (size_t j = 0; j < keylen; j++) key[j] = uint8_t(uint8_t(keyStr[j]) + uint8_t(j));
    uint8_t bl = 0;
    size_t kidx = 0;
    for (size_t e = blob.size(); e-- > 0;) {
        blob[e] = uint8_t(blob[e] - bl);
        blob[e] = uint8_t(blob[e] - key[kidx]);
        bl = uint8_t(bl + blob[e]);
        if (++kidx >= keylen) kidx = 0;
    }
    if (blob[0] != 0)
        return fail(
            "decodeKeyBlockLine1: wrong cipher key -- leading byte is not 0x00 (key is md5(password + "
            "crypt(password,\"DM\")) for THIS account)");
    return blob;
}

std::optional<KeyBlockFields> decodeKeyBlock(std::string_view line1, std::string_view account, std::string_view keyStr,
                                             std::string* err) {
    auto blob = keyBlockBlob(line1, keyStr, err);
    if (!blob) return std::nullopt;
    auto fail = [&](std::string m) -> std::optional<KeyBlockFields> {
        if (err) *err = std::move(m);
        return std::nullopt;
    };
    // blob[0] is the stage-2 key-correctness byte; the DLL's plaintext is the rest. The identity
    // convention drops this leading byte -- mintAccountLoginRecord re-frames with its own leading
    // 00 00, so the on-wire bytes are unchanged.
    const uint8_t* plain = blob->data() + 1;
    const size_t plen = blob->size() - 1;
    const uint8_t letter = line1.empty() ? 0 : uint8_t(line1[0]);

    size_t pos = 0;
    auto has = [&](const char* m) {
        size_t ml = std::char_traits<char>::length(m);
        return pos + ml <= plen && std::memcmp(plain + pos, m, ml) == 0;
    };
    if (has("server: ")) {
        while (pos < plen && plain[pos] != '\n') pos++;
        if (pos < plen) pos++;
    }
    if (has("friends: ")) {
        pos += 9;
        while (pos < plen && plain[pos] != 0) pos++;
        if (pos < plen) pos++;
    }
    if (has("ds: ")) {
        pos += 4;
        while (pos + 2 <= plen) {
            uint16_t id = uint16_t(le16(plain + pos) ^ 0x59b3);
            pos += 2;
            if (id == 0) break;
            if (pos + 4 > plen) return fail("DecodeKeyBlock: truncated ds: record length");
            uint32_t rl = le32(plain + pos) ^ 0x34abc645u;
            pos += 4;
            if (pos + rl > plen) return fail("DecodeKeyBlock: ds: record overruns the block");
            pos += rl;
        }
    }
    if (has("save: ") || has("nave: ")) {
        pos += 6;
        if (pos + 2 > plen) return fail("DecodeKeyBlock: truncated save: length");
        uint32_t rl = uint32_t(plain[pos]) << 8 | plain[pos + 1];  // big-endian, unlike the rest
        pos += 2 + rl;
        if (pos > plen) return fail("DecodeKeyBlock: save: section overruns the block");
    }

    const size_t tail = keyBlockTrailerLen(letter);
    if (plen - pos < tail)
        return fail("DecodeKeyBlock: block too short for a format-'" + std::string(1, char(letter)) + "' trailer");
    KeyBlockFields f;
    const size_t identEnd = plen - tail;
    f.identity.assign(plain + pos, plain + identEnd);
    const uint8_t* tr = plain + identEnd;  // tr[0:2] = date; installId/innerKey follow
    if (letter > 'A') {
        f.installId = le32(tr + 2);
        f.haveHubIds = true;
        if (letter > 'B') f.innerKey = le32(tr + 6);
    }
    if (f.haveHubIds && (f.installId == 0 || (letter > 'B' && f.innerKey == 0)))
        return fail("DecodeKeyBlock: hub credentials in the block are zero");
    return f;
}

Bytes buildKeyBlob(ByteSpan identity, uint32_t installId, uint32_t innerKey, ByteSpan tail3) {
    Bytes b;
    b.push_back(0);
    b.insert(b.end(), identity.begin(), identity.end());
    b.push_back(0);  // terminator
    b.push_back(0);  // tail[0]
    putLe32(b, installId);
    putLe32(b, innerKey);
    for (int i = 0; i < 3; i++) b.push_back(i < int(tail3.size()) ? tail3[i] : 0);
    return b;
}

std::string encodeKeyLine1(ByteSpan blob, std::string_view keyStr, std::string_view seed) {
    // Stage 2 inverse: walk from the last index down with the same key stream and accumulator.
    const size_t keylen = keyStr.size();
    Bytes key(keylen);
    for (size_t j = 0; j < keylen; j++) key[j] = uint8_t(uint8_t(keyStr[j]) + uint8_t(j));
    Bytes enc(blob.begin(), blob.end());
    uint8_t bl = 0;
    size_t kidx = 0;
    for (size_t e = enc.size(); e-- > 0;) {
        uint8_t p = blob[e];
        enc[e] = uint8_t(p + key[kidx] + bl);
        bl = uint8_t(bl + p);
        if (++kidx >= keylen) kidx = 0;
    }
    // Stage 1 inverse: chunks of up to 45 bytes, each "count char" then ceil(count/3) 4-char groups.
    std::string out(seed.substr(0, 2));
    size_t pos = 0;
    while (pos < enc.size()) {
        size_t cnt = std::min<size_t>(45, enc.size() - pos);
        out.push_back(char(0x60 - int(cnt)));
        for (size_t k = 0; k < cnt; k += 3) {
            uint8_t b0 = enc[pos + k], b1 = k + 1 < cnt ? enc[pos + k + 1] : 0, b2 = k + 2 < cnt ? enc[pos + k + 2] : 0;
            uint8_t g0 = b2 & 0x3f, g1 = uint8_t(((b1 & 0xf) << 2) | (b2 >> 6)),
                    g2 = uint8_t(((b0 & 3) << 4) | (b1 >> 4)), g3 = b0 >> 2;
            out.push_back(char(g0 + 0x20));
            out.push_back(char(g1 + 0x20));
            out.push_back(char(g2 + 0x20));
            out.push_back(char(g3 + 0x20));
        }
        pos += cnt;
    }
    out.push_back('`');
    return out;
}

}  // namespace fugue::account
