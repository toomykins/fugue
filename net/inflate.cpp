#include "net/inflate.h"
#include <cstring>

namespace fugue::net {
namespace {
// Canonical-Huffman decoder in the style of RFC 1951 section 3.2.2 (count per length + sorted symbols).
struct Huff {
    uint16_t count[16];
    uint16_t symbol[320];
};

struct Inf {
    ByteSpan in;
    size_t pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    Bytes* out;
    size_t maxOut;
    bool err = false;

    int bits(int need) {
        uint32_t v = bitbuf;
        while (bitcnt < need) {
            if (pos >= in.size()) {
                err = true;
                return 0;
            }
            v |= uint32_t(in[pos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = v >> need;
        bitcnt -= need;
        return int(v & ((1u << need) - 1));
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= bits(1);
            if (err) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        err = true;
        return -1;
    }
};

// Returns 0 complete, >0 incomplete code, <0 over-subscribed.
int construct(Huff& h, const short* length, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (length[s] != 0) h.symbol[offs[length[s]]++] = uint16_t(s);
    return left;
}

const short kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                             193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                              6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Inf& s, const Huff& lencode, const Huff& distcode) {
    for (;;) {
        int sym = s.decode(lencode);
        if (s.err || sym < 0) return false;
        if (sym < 256) {
            if (s.out->size() >= s.maxOut) return false;
            s.out->push_back(uint8_t(sym));
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            int len = kLenBase[sym] + s.bits(kLenExtra[sym]);
            int ds = s.decode(distcode);
            if (s.err || ds < 0 || ds >= 30) return false;
            size_t dist = size_t(kDistBase[ds]) + size_t(s.bits(kDistExtra[ds]));
            if (s.err || dist > s.out->size() || s.out->size() + len > s.maxOut) return false;
            size_t from = s.out->size() - dist;
            for (int i = 0; i < len; i++) s.out->push_back((*s.out)[from + i]);
        }
    }
}

bool stored(Inf& s) {
    s.bitbuf = 0;
    s.bitcnt = 0;
    if (s.pos + 4 > s.in.size()) return false;
    unsigned len = s.in[s.pos] | s.in[s.pos + 1] << 8;
    unsigned nlen = s.in[s.pos + 2] | s.in[s.pos + 3] << 8;
    s.pos += 4;
    if (len != (~nlen & 0xffff) || s.pos + len > s.in.size() || s.out->size() + len > s.maxOut) return false;
    s.out->insert(s.out->end(), s.in.begin() + s.pos, s.in.begin() + s.pos + len);
    s.pos += len;
    return true;
}

bool fixedBlock(Inf& s) {
    static Huff lencode, distcode;
    static bool init = false;
    if (!init) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        construct(lencode, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        construct(distcode, l, 30);
        init = true;
    }
    return codes(s, lencode, distcode);
}

bool dynamicBlock(Inf& s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
    if (s.err || nlen > 286 || ndist > 30) return false;
    short lengths[320] = {0};
    Huff lencode, distcode;
    for (int i = 0; i < ncode; i++) lengths[order[i]] = short(s.bits(3));
    for (int i = ncode; i < 19; i++) lengths[order[i]] = 0;
    if (s.err || construct(lencode, lengths, 19) != 0) return false;
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = s.decode(lencode);
        if (s.err || sym < 0) return false;
        if (sym < 16) {
            lengths[idx++] = short(sym);
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (idx == 0) return false;
                len = lengths[idx - 1];
                rep = 3 + s.bits(2);
            } else if (sym == 17)
                rep = 3 + s.bits(3);
            else
                rep = 11 + s.bits(7);
            if (s.err || idx + rep > nlen + ndist) return false;
            while (rep--) lengths[idx++] = short(len);
        }
    }
    if (lengths[256] == 0) return false;
    int e = construct(lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1)) return false;
    e = construct(distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1)) return false;
    return codes(s, lencode, distcode);
}

bool inflateImpl(ByteSpan in, Bytes& out, size_t maxOut, size_t* consumed) {
    Inf s;
    s.in = in;
    s.out = &out;
    s.maxOut = maxOut;
    int last;
    do {
        last = s.bits(1);
        int type = s.bits(2);
        if (s.err) return false;
        bool ok = type == 0 ? stored(s) : type == 1 ? fixedBlock(s) : type == 2 ? dynamicBlock(s) : false;
        if (!ok || s.err) return false;
    } while (!last);
    if (consumed) *consumed = s.pos;
    return true;
}
}  // namespace

bool inflateRaw(ByteSpan in, Bytes& out, size_t maxOut) {
    out.clear();
    return inflateImpl(in, out, maxOut, nullptr);
}

uint32_t crc32Gzip(ByteSpan data) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool gunzip(ByteSpan in, Bytes& out, size_t maxOut) {
    out.clear();
    if (in.size() < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return false;
    uint8_t flg = in[3];
    size_t p = 10;
    if (flg & 4) {
        if (p + 2 > in.size()) return false;
        p += 2 + le16(&in[p]);
    }
    for (uint8_t bit : {uint8_t(8), uint8_t(16)})
        if (flg & bit) {
            while (p < in.size() && in[p]) p++;
            p++;
        }
    if (flg & 2) p += 2;
    if (p >= in.size()) return false;
    size_t used = 0;
    if (!inflateImpl(in.subspan(p), out, maxOut, &used)) {
        out.clear();
        return false;
    }
    p += used;
    if (p + 8 > in.size()) {
        out.clear();
        return false;
    }
    if (le32(&in[p]) != crc32Gzip(out) || le32(&in[p + 4]) != uint32_t(out.size())) {
        out.clear();
        return false;
    }
    return true;
}
}  // namespace fugue::net
