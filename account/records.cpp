#include "account/records.h"
#include <cstdio>
#include <random>
#include "net/md5.h"
#include "net/resource.h"

namespace fugue::account {

std::string ckey(std::string_view s) {
    std::string o;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o.push_back(c);
    }
    return o;
}

std::optional<Bytes> mintAccountLoginRecord(ByteSpan identity, uint32_t cauthKey, ByteSpan installBlock, uint8_t marker,
                                            std::string* err) {
    if (identity.empty()) {
        if (err) *err = "accountIdentity is empty";
        return std::nullopt;
    }
    if (installBlock.size() != 17) {
        if (err) *err = "installBlock must be 17 bytes";
        return std::nullopt;
    }
    const size_t n = identity.size() + 1;
    Bytes rec(2 + n + 4 + 17 + 1 + 5, 0);
    std::copy(identity.begin(), identity.end(), rec.begin() + 2);
    for (int i = 0; i < 4; i++) rec[2 + n + i] = uint8_t(cauthKey >> (8 * i));
    std::copy(installBlock.begin(), installBlock.end(), rec.begin() + 6 + n);
    // Client capabilities byte, in installBlock[0]'s slot (that install byte is not sent). Bit 0x02 must be set, or
    // stock DreamDaemon ends the session on an image request.
    rec[6 + n] = 0x7f;
    rec[23 + n] = marker;
    return rec;
}

uint32_t cidFromDigest(ByteSpan d) {
    std::string h = hexEncode(d);
    return net::resId(ByteSpan(reinterpret_cast<const uint8_t*>(h.data()), h.size()));
}
bool isFugueCid(uint32_t cid) { return cid % 1000 == 523; }

static std::optional<Bytes> searchDigest(const std::string& domain, const std::string& seed) {
    for (int i = 0; i < 10000000; i++) {
        auto sum = net::md5(domain + seed + ":" + std::to_string(i));
        if (isFugueCid(cidFromDigest(sum))) return Bytes(sum.begin(), sum.end());
    }
    return std::nullopt;
}
std::optional<Bytes> digestForCkey(std::string_view key) { return searchDigest("fugue-cid-v1:key:", ckey(key)); }
std::optional<Bytes> digestForInstall(std::string_view sid) {
    return searchDigest("fugue-cid-v1:install:", std::string(sid));
}

std::optional<Bytes> watermarkedInstallBlock(ByteSpan installBlock, std::string_view key, std::string* err) {
    if (installBlock.size() != 17) {
        if (err) *err = "installBlock must be 17 bytes";
        return std::nullopt;
    }
    auto d = digestForCkey(key);
    if (!d) {
        if (err) *err = "no watermarked digest";
        return std::nullopt;
    }
    Bytes out(installBlock.begin(), installBlock.end());
    std::copy(d->begin(), d->end(), out.begin() + 1);
    return out;
}

uint8_t hubScramble(Bytes& buf, uint32_t key) {
    uint8_t bl = 0;
    for (auto& b : buf) {
        uint8_t dl = uint8_t(key >> (bl & 0x1f)) + bl;
        uint8_t al = b;
        bl = uint8_t(bl + al);
        b = uint8_t(al + dl);
    }
    return bl;
}
uint8_t hubUnscramble(Bytes& buf, uint32_t key) {
    uint8_t bl = 0;
    for (auto& b : buf) {
        uint8_t dl = uint8_t(key >> (bl & 0x1f)) + bl;
        uint8_t al = uint8_t(b - dl);
        bl = uint8_t(bl + al);
        b = al;
    }
    return bl;
}

static void putStr0(Bytes& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

Bytes buildDownloadKeyRequest(std::string_view username, std::string_view domain, std::string_view cred1,
                              std::string_view crypt, std::string_view installId, uint32_t nonce) {
    Bytes b;
    putStr0(b, username);
    putStr0(b, domain);
    b.insert(b.end(), cred1.begin(), cred1.end());
    b.push_back(';');
    putStr0(b, crypt);
    putLe32(b, 2);
    putStr0(b, installId);
    uint8_t sum = hubScramble(b, nonce);
    b.push_back(sum);
    return b;
}

std::optional<std::string> parseDownloadKeyReply(ByteSpan body, uint32_t nonce, std::string* err) {
    auto fail = [&](const char* m) -> std::optional<std::string> {
        if (err) *err = m;
        return std::nullopt;
    };
    if (body.size() < 2) return fail("hub: DownloadKey reply too short");
    Bytes dec(body.begin(), body.end());
    hubUnscramble(dec, nonce);
    if (dec[0] != 1) return fail("hub: DownloadKey refused (status != 1)");
    std::string text(dec.begin() + 1, dec.end());
    size_t i = text.find("BEGIN KEY ");
    if (i == std::string::npos) return fail("hub: DownloadKey reply has no BEGIN KEY block");
    size_t j = text.find("END KEY", i);
    if (j == std::string::npos) return fail("hub: DownloadKey reply BEGIN KEY block unterminated");
    return text.substr(i, j - i + 7);
}

std::optional<ChallengeAnswer> mintChallengeAnswer(ByteSpan challenge, std::string_view accountId, uint32_t installId,
                                                   uint32_t innerKey, ByteSpan padPlain, std::string* err) {
    if (challenge.size() < 16) {
        if (err) *err = "hub: 0x42 challenge too short (" + std::to_string(challenge.size()) + " bytes, want >=16)";
        return std::nullopt;
    }
    if (padPlain.size() % 4 || padPlain.size() < 8 || padPlain.size() > 44) {
        if (err) *err = "pad must be 2..11 u32 words";
        return std::nullopt;
    }
    uint32_t nonce = le32(&challenge[12]);
    ChallengeAnswer r;
    for (size_t i = 0; i < padPlain.size(); i += 4) r.sigmaPad += le32(&padPlain[i]);
    Bytes scr(padPlain.begin(), padPlain.end());
    r.innerSum = hubScramble(scr, innerKey);
    Bytes body = scr;
    body.push_back(r.innerSum);
    body.insert(body.end(), accountId.begin(), accountId.end());
    body.push_back(0);
    putLe32(body, uint32_t(accountId.size() + 1));
    putLe32(body, 1);
    putLe32(body, installId);
    putLe32(body, 516);
    r.plainBeforeOuter = body;
    uint8_t osum = hubScramble(body, nonce);
    body.push_back(osum);
    r.answer = std::move(body);
    return r;
}

std::optional<ChallengeAnswer> mintChallengeAnswerRandom(ByteSpan challenge, std::string_view accountId,
                                                         uint32_t installId, uint32_t innerKey, std::string* err) {
    std::random_device rd;
    int count = int(rd() % 10) + 2;
    Bytes pad;
    for (int i = 0; i < count; i++) putLe32(pad, rd());
    return mintChallengeAnswer(challenge, accountId, installId, innerKey, pad, err);
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint32_t rekey(std::string_view cert, uint32_t seed) {
    if (cert.size() <= 2) return seed;
    // sscanf(cert, "%2x"): up to two leading hex digits, at least one.
    int n = 0, cnt = 0;
    while (cnt < 2 && cnt < int(cert.size()) && hexDigit(cert[cnt]) >= 0) {
        n = n * 16 + hexDigit(cert[cnt]);
        cnt++;
    }
    if (cnt == 0) return seed;
    if (size_t(n) * 8 + 2 > cert.size()) return seed;
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 4) {
        uint8_t byteVal = uint8_t(seed >> shift);
        int idx = int(float(byteVal) * float(n * 8) * (1.0f / 256.0f));
        size_t pos = 2 + size_t(idx);
        int nib = 0;
        if (pos < cert.size()) {
            int v = hexDigit(cert[pos]);
            if (v >= 0) nib = v;
        }
        result |= uint32_t(nib) << shift;
    }
    return result;
}

uint32_t rekeyedLoginKey(std::string_view cert, uint32_t kOld) {
    uint32_t sum = kOld + rekey(cert, kOld);
    return sum == 0 ? 0x72eba290u : sum;
}

std::string certStrFrom(uint32_t r1, uint32_t r2, uint32_t r3) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "03%.8x%.8x%.8x", r1, r2, r3);
    return buf;
}
std::string generateCertStr() {
    std::random_device rd;
    return certStrFrom(rd(), rd(), rd());
}
std::string cauthName(std::string_view accountKey, std::string_view serverAddr) {
    return ckey(accountKey) + "@" + std::string(serverAddr);
}
Bytes encodeCertResponseCached(std::string_view certStr, std::string_view pp) {
    Bytes b{1};
    putStr0(b, certStr);
    putStr0(b, pp);
    return b;
}

Bytes buildCertMintRequest(uint32_t cauthKey, std::string_view certStr, uint32_t key5e) {
    Bytes b;
    putLe32(b, cauthKey);
    putStr0(b, certStr);
    uint8_t sum = hubScramble(b, key5e);
    b.push_back(sum);
    return b;
}

std::optional<uint32_t> parseHubTokenReply(ByteSpan body, uint32_t key5e, std::string* err) {
    if (body.size() < 2) {
        if (err) *err = "hub: 0x0043 reply too short (" + std::to_string(body.size()) + " bytes)";
        return std::nullopt;
    }
    Bytes p(body.begin(), body.end() - 1);  // the trailing sum byte is dropped unchecked
    hubUnscramble(p, key5e);
    std::string text;
    for (size_t i = 1; i < p.size() && p[i]; i++) text.push_back(char(p[i]));
    if (p[0] < 1 || text.empty()) {
        if (err) *err = "hub: refused (code=" + std::to_string(p[0]) + " text=\"" + text + "\")";
        return std::nullopt;
    }
    uint64_t v = 0;  // hex digits only, no prefix, value fits 32 bits
    for (char c : text) {
        int d = hexDigit(c);
        if (d < 0 || (v = v * 16 + uint64_t(d)) > 0xffffffffull) {
            if (err) *err = "hub: reply text \"" + text + "\" is not a hex token";
            return std::nullopt;
        }
    }
    return uint32_t(v);
}

Bytes encodeCertResponseToken(uint32_t token) {
    Bytes b{0};
    putLe32(b, token);
    return b;
}

}  // namespace fugue::account
