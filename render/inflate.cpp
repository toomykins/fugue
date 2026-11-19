// Spec: RFC 1950, RFC 1951 (see inflate.h).
#include "render/inflate.h"

#include <array>
#include <cstring>

namespace fugue::render {
namespace {

struct Bits {
    const uint8_t* p;
    size_t n, pos = 0;  // pos in bytes
    uint32_t buf = 0;
    int cnt = 0;
    bool eof = false;
    int bit() {
        if (cnt == 0) {
            if (pos >= n) {
                eof = true;
                return 0;
            }
            buf = p[pos++];
            cnt = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        cnt--;
        return b;
    }
    uint32_t bits(int k) {  // LSB-first
        uint32_t v = 0;
        for (int i = 0; i < k; i++) v |= uint32_t(bit()) << i;
        return v;
    }
};

// Canonical Huffman table: count[len] codes of each length, symbols sorted by (len, value).
struct Huff {
    std::array<uint16_t, 16> count{};
    std::vector<uint16_t> sym;
};

// Returns false if the code is over-subscribed. Incomplete codes are allowed (RFC 1951 permits a single
// distance code of one bit, and some encoders emit incomplete sets).
bool build(Huff& h, const uint8_t* len, int n) {
    h.count.fill(0);
    h.sym.assign(size_t(n), 0);
    for (int i = 0; i < n; i++) h.count[len[i]]++;
    int left = 1;
    for (int l = 1; l <= 15; l++) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return false;
    }
    std::array<uint16_t, 16> offs{};
    for (int l = 1; l < 15; l++) offs[l + 1] = uint16_t(offs[l] + h.count[l]);
    for (int i = 0; i < n; i++)
        if (len[i]) h.sym[offs[len[i]]++] = uint16_t(i);
    return true;
}

// -1 on error / eof.
int decodeSym(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= 15; l++) {
        code |= b.bit();
        if (b.eof) return -1;
        int count = h.count[l];
        if (code - count < first) return h.sym[size_t(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

ZStatus codes(Bits& b, std::vector<uint8_t>& out, size_t base, size_t limit, const Huff& lit, const Huff& dist) {
    for (;;) {
        int s = decodeSym(b, lit);
        if (s < 0) return b.eof ? ZStatus::Truncated : ZStatus::BadData;
        if (s < 256) {
            out.push_back(uint8_t(s));
        } else if (s == 256) {
            return ZStatus::Ok;
        } else {
            s -= 257;
            if (s >= 29) return ZStatus::BadData;
            size_t len = kLenBase[s] + b.bits(kLenExtra[s]);
            int ds = decodeSym(b, dist);
            if (ds < 0) return b.eof ? ZStatus::Truncated : ZStatus::BadData;
            if (ds >= 30) return ZStatus::BadData;
            size_t d = kDistBase[ds] + b.bits(kDistExtra[ds]);
            if (b.eof) return ZStatus::Truncated;
            if (d > out.size() - base) return ZStatus::BadData;
            size_t from = out.size() - d;
            for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
        }
        if (out.size() - base > limit) return ZStatus::Limit;
    }
}

}  // namespace

uint32_t crc32Ieee(const uint8_t* p, size_t n, uint32_t crc) {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            a[i] = c;
        }
        return a;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = t[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

ZStatus zlibInflate(const uint8_t* p, size_t n, std::vector<uint8_t>& out, size_t limit, size_t* used) {
    if (n < 2) return ZStatus::Truncated;
    // RFC 1950 2.2: CM=8, CINFO<=7, (CMF*256+FLG) % 31 == 0, FDICT unsupported (PNG/zTXt never set it).
    if ((p[0] & 0x0f) != 8 || (p[0] >> 4) > 7 || ((p[0] << 8) | p[1]) % 31 != 0 || (p[1] & 0x20))
        return ZStatus::BadHeader;
    Bits b{p + 2, n - 2};
    const size_t base = out.size();
    bool last = false;
    while (!last) {
        last = b.bit();
        uint32_t type = b.bits(2);
        if (b.eof) return ZStatus::Truncated;
        if (type == 0) {
            b.cnt = 0;  // discard the rest of the current byte
            if (b.pos + 4 > b.n) return ZStatus::Truncated;
            uint32_t len = b.p[b.pos] | (b.p[b.pos + 1] << 8);
            uint32_t nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xffff) != nlen) return ZStatus::BadData;
            if (b.pos + len > b.n) return ZStatus::Truncated;
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
            if (out.size() - base > limit) return ZStatus::Limit;
        } else if (type == 1) {
            static Huff lit, dist;
            static bool init = [] {
                uint8_t l[288];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                build(lit, l, 288);
                uint8_t d[30];
                for (auto& x : d) x = 5;
                build(dist, d, 30);
                return true;
            }();
            (void)init;
            ZStatus s = codes(b, out, base, limit, lit, dist);
            if (s != ZStatus::Ok) return s;
        } else if (type == 2) {
            int nlen = int(b.bits(5)) + 257, ndist = int(b.bits(5)) + 1, ncode = int(b.bits(4)) + 4;
            if (b.eof) return ZStatus::Truncated;
            if (nlen > 286 || ndist > 30) return ZStatus::BadData;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; i++) lengths[order[i]] = uint8_t(b.bits(3));
            Huff lencode;
            if (!build(lencode, lengths, 19)) return ZStatus::BadData;
            uint8_t ll[320] = {};
            int idx = 0;
            while (idx < nlen + ndist) {
                int sym = decodeSym(b, lencode);
                if (sym < 0) return b.eof ? ZStatus::Truncated : ZStatus::BadData;
                if (sym < 16) {
                    ll[idx++] = uint8_t(sym);
                } else {
                    int rep, val = 0;
                    if (sym == 16) {
                        if (idx == 0) return ZStatus::BadData;
                        val = ll[idx - 1];
                        rep = 3 + int(b.bits(2));
                    } else if (sym == 17) {
                        rep = 3 + int(b.bits(3));
                    } else {
                        rep = 11 + int(b.bits(7));
                    }
                    if (idx + rep > nlen + ndist) return ZStatus::BadData;
                    while (rep--) ll[idx++] = uint8_t(val);
                }
            }
            if (b.eof) return ZStatus::Truncated;
            if (ll[256] == 0) return ZStatus::BadData;  // no end-of-block code
            Huff lit, dist;
            if (!build(lit, ll, nlen) || !build(dist, ll + nlen, ndist)) return ZStatus::BadData;
            ZStatus s = codes(b, out, base, limit, lit, dist);
            if (s != ZStatus::Ok) return s;
        } else {
            return ZStatus::BadData;
        }
    }
    // Adler-32 trailer, big-endian, starts at the next byte boundary.
    b.cnt = 0;
    if (b.pos + 4 > b.n) return ZStatus::Truncated;
    uint32_t want = (uint32_t(b.p[b.pos]) << 24) | (uint32_t(b.p[b.pos + 1]) << 16) | (uint32_t(b.p[b.pos + 2]) << 8) |
                    b.p[b.pos + 3];
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = base; i < out.size(); i++) {
        s1 = (s1 + out[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    if (((s2 << 16) | s1) != want) return ZStatus::Checksum;
    if (used) *used = 2 + b.pos + 4;
    return ZStatus::Ok;
}

}  // namespace fugue::render
