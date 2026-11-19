#include "account/desc.h"
#include <cstdint>
#include <cstring>
#include "net/md5.h"

namespace fugue::account {
namespace {
static const uint8_t kIP[64] = {58, 50, 42, 34, 26, 18, 10, 2,  60, 52, 44, 36, 28, 20, 12, 4,  62, 54, 46, 38, 30, 22,
                                14, 6,  64, 56, 48, 40, 32, 24, 16, 8,  57, 49, 41, 33, 25, 17, 9,  1,  59, 51, 43, 35,
                                27, 19, 11, 3,  61, 53, 45, 37, 29, 21, 13, 5,  63, 55, 47, 39, 31, 23, 15, 7};
static const uint8_t kFP[64] = {40, 8,  48, 16, 56, 24, 64, 32, 39, 7,  47, 15, 55, 23, 63, 31, 38, 6,  46, 14, 54, 22,
                                62, 30, 37, 5,  45, 13, 53, 21, 61, 29, 36, 4,  44, 12, 52, 20, 60, 28, 35, 3,  43, 11,
                                51, 19, 59, 27, 34, 2,  42, 10, 50, 18, 58, 26, 33, 1,  41, 9,  49, 17, 57, 25};
static const uint8_t kE[48] = {32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
                               12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
                               22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
static const uint8_t kP[32] = {16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
                               2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};
static const uint8_t kPC1[56] = {57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18, 10, 2,  59, 51, 43,
                                 35, 27, 19, 11, 3,  60, 52, 44, 36, 63, 55, 47, 39, 31, 23, 15, 7,  62, 54,
                                 46, 38, 30, 22, 14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};
static const uint8_t kPC2[48] = {14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10, 23, 19, 12, 4,
                                 26, 8,  16, 7,  27, 20, 13, 2,  41, 52, 31, 37, 47, 55, 30, 40,
                                 51, 45, 33, 48, 44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};
static const uint8_t kS[8][4][16] = {
    14, 4,  13, 1,  2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0,  7,  0,  15, 7,  4,  14, 2,  13, 1,  10, 6,  12, 11, 9,
    5,  3,  8,  4,  1,  14, 8,  13, 6,  2,  11, 15, 12, 9,  7,  3,  10, 5,  0,  15, 12, 8,  2,  4,  9,  1,  7,  5,  11,
    3,  14, 10, 0,  6,  13, 15, 1,  8,  14, 6,  11, 3,  4,  9,  7,  2,  13, 12, 0,  5,  10, 3,  13, 4,  7,  15, 2,  8,
    14, 12, 0,  1,  10, 6,  9,  11, 5,  0,  14, 7,  11, 10, 4,  13, 1,  5,  8,  12, 6,  9,  3,  2,  15, 13, 8,  10, 1,
    3,  15, 4,  2,  11, 6,  7,  12, 0,  5,  14, 9,  10, 0,  9,  14, 6,  3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,  13,
    7,  0,  9,  3,  4,  6,  10, 2,  8,  5,  14, 12, 11, 15, 1,  13, 6,  4,  9,  8,  15, 3,  0,  11, 1,  2,  12, 5,  10,
    14, 7,  1,  10, 13, 0,  6,  9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12, 7,  13, 14, 3,  0,  6,  9,  10, 1,  2,  8,
    5,  11, 12, 4,  15, 13, 8,  11, 5,  6,  15, 0,  3,  4,  7,  2,  12, 1,  10, 14, 9,  10, 6,  9,  0,  12, 11, 7,  13,
    15, 1,  3,  14, 5,  2,  8,  4,  3,  15, 0,  6,  10, 1,  13, 8,  9,  4,  5,  11, 12, 7,  2,  14, 2,  12, 4,  1,  7,
    10, 11, 6,  8,  5,  3,  15, 13, 0,  14, 9,  14, 11, 2,  12, 4,  7,  13, 1,  5,  0,  15, 10, 3,  9,  8,  6,  4,  2,
    1,  11, 10, 13, 7,  8,  15, 9,  12, 5,  6,  3,  0,  14, 11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4,  5,
    3,  12, 1,  10, 15, 9,  2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11, 10, 15, 4,  2,  7,  12, 9,  5,  6,  1,  13, 14,
    0,  11, 3,  8,  9,  14, 15, 5,  2,  8,  12, 3,  7,  0,  4,  10, 1,  13, 11, 6,  4,  3,  2,  12, 9,  5,  15, 10, 11,
    14, 1,  7,  6,  0,  8,  13, 4,  11, 2,  14, 15, 0,  8,  13, 3,  12, 9,  7,  5,  10, 6,  1,  13, 0,  11, 7,  4,  9,
    1,  10, 14, 3,  5,  12, 2,  15, 8,  6,  1,  4,  11, 13, 12, 3,  7,  14, 10, 15, 6,  8,  0,  5,  9,  2,  6,  11, 13,
    8,  1,  4,  10, 7,  9,  5,  0,  15, 14, 2,  3,  12, 13, 2,  8,  4,  6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,
    1,  15, 13, 8,  10, 3,  7,  4,  12, 5,  6,  11, 0,  14, 9,  2,  7,  11, 4,  1,  9,  12, 14, 2,  0,  6,  10, 13, 15,
    3,  5,  8,  2,  1,  14, 7,  4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11};
static const uint8_t kShifts[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

// FIPS tables are 1-indexed, bit 1 = MSB of an inBits-wide input.
uint64_t permute(uint64_t in, int inBits, const uint8_t* t, int n) {
    uint64_t out = 0;
    for (int i = 0; i < n; i++) out = (out << 1) | ((in >> (inBits - t[i])) & 1);
    return out;
}

int b64Index(char c) {
    if (c == '.') return 0;
    if (c == '/') return 1;
    if (c >= '0' && c <= '9') return c - '0' + 2;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 12;
    if (c >= 'a' && c <= 'z') return c - 'a' + 38;
    return -1;
}
const char kB64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void subkeys(ByteSpan pw, uint64_t out[16]) {
    uint64_t key = 0;
    for (int i = 0; i < 8; i++) key = (key << 8) | (i < int(pw.size()) ? uint64_t((pw[i] & 0x7f) << 1) : 0);
    uint64_t cd = permute(key, 64, kPC1, 56);
    uint32_t c = uint32_t(cd >> 28) & 0xfffffff, d = uint32_t(cd) & 0xfffffff;
    for (int r = 0; r < 16; r++) {
        for (int s = 0; s < kShifts[r]; s++) {
            c = ((c << 1) | (c >> 27)) & 0xfffffff;
            d = ((d << 1) | (d >> 27)) & 0xfffffff;
        }
        out[r] = permute((uint64_t(c) << 28) | d, 56, kPC2, 48);
    }
}

uint64_t encryptBlock(uint64_t block, const uint64_t sk[16], const uint8_t e[48]) {
    uint64_t ip = permute(block, 64, kIP, 64);
    uint32_t l = uint32_t(ip >> 32), r = uint32_t(ip);
    for (int round = 0; round < 16; round++) {
        uint64_t x = permute(r, 32, e, 48) ^ sk[round];
        uint32_t sOut = 0;
        for (int box = 0; box < 8; box++) {
            unsigned six = unsigned(x >> (42 - 6 * box)) & 0x3f;
            unsigned row = ((six >> 4) & 2) | (six & 1);
            unsigned col = (six >> 1) & 0xf;
            sOut = (sOut << 4) | kS[box][row][col];
        }
        uint32_t f = uint32_t(permute(sOut, 32, kP, 32));
        uint32_t nr = l ^ f;
        l = r;
        r = nr;
    }
    return permute((uint64_t(r) << 32) | l, 64, kFP, 64);
}
}  // namespace

CryptResult desCrypt(ByteSpan password, const char salt[2]) {
    CryptResult res;
    int v0 = b64Index(salt[0]), v1 = b64Index(salt[1]);
    if (v0 < 0 || v1 < 0) {
        res.error = "invalid crypt salt: must be 2 characters from \"./0-9A-Za-z\"";
        return res;
    }
    uint8_t e[48];
    std::memcpy(e, kE, 48);
    for (int i = 0; i < 12; i++) {
        int bit = i < 6 ? (v0 >> i) & 1 : (v1 >> (i - 6)) & 1;
        if (bit) std::swap(e[i], e[i + 24]);
    }
    uint64_t sk[16];
    subkeys(password, sk);
    uint64_t blk = 0;
    for (int i = 0; i < 25; i++) blk = encryptBlock(blk, sk, e);
    res.value.push_back(salt[0]);
    res.value.push_back(salt[1]);
    // 64 bits + 2 zero pad bits = 66 = 11 x 6, MSB first.
    for (int i = 0; i < 11; i++) {
        int shift = 64 - 6 * (i + 1);  // may be negative on the last group (pad bits)
        unsigned v = shift >= 0 ? unsigned(blk >> shift) & 0x3f : unsigned(blk << -shift) & 0x3f;
        res.value.push_back(kB64[v]);
    }
    res.ok = true;
    return res;
}

std::string pwordCrypt(ByteSpan password) {
    const char salt[2] = {'D', 'M'};
    return desCrypt(password, salt).value;
}

std::string pwordCryptLevel2(ByteSpan password) {
    std::string l1 = pwordCrypt(password);
    Bytes buf(password.begin(), password.end());
    buf.insert(buf.end(), l1.begin(), l1.end());
    return hexEncode(net::md5(buf));
}

std::string downloadKeyCredential(ByteSpan password) { return pwordCryptLevel2(password) + ";" + pwordCrypt(password); }

}  // namespace fugue::account
