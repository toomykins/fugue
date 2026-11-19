// Account login record, computer_id watermark, and hub-link primitives. Pure functions, no sockets, no Qt.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "net/bytes.h"

namespace fugue::account {
using fugue::Bytes;
using fugue::ByteSpan;

// BYOND ckey(): lowercase, keep [a-z0-9].
std::string ckey(std::string_view s);

// ---- account login record (type 0x1a) ----
// The identity is length-delimited and may contain 0x00.
std::optional<Bytes> mintAccountLoginRecord(ByteSpan identity, uint32_t cauthKey, ByteSpan installBlock, uint8_t marker,
                                            std::string* err = nullptr);
// Copy of installBlock with its digest bytes replaced for the ckey.
std::optional<Bytes> watermarkedInstallBlock(ByteSpan installBlock, std::string_view key, std::string* err = nullptr);

// ---- computer_id ----
uint32_t cidFromDigest(ByteSpan digest16);                    // CRC-32/XFER over the lowercase hex text
bool isFugueCid(uint32_t cid);                                // cid % 1000 == 523
std::optional<Bytes> digestForCkey(std::string_view key);     // Deterministic digest for a ckey.
std::optional<Bytes> digestForInstall(std::string_view sid);  // Deterministic digest for an install id.

// ---- hub-link scramble (returns the trailing sum byte) ----
uint8_t hubScramble(Bytes& buf, uint32_t key);
uint8_t hubUnscramble(Bytes& buf, uint32_t key);

// Key-download request body (scrambled, with a trailing sum).
Bytes buildDownloadKeyRequest(std::string_view username, std::string_view domain, std::string_view cred1,
                              std::string_view crypt, std::string_view installId, uint32_t nonce);
// Parse the key-download reply into its key block.
std::optional<std::string> parseDownloadKeyReply(ByteSpan body, uint32_t nonce, std::string* err = nullptr);

// Challenge answer. `padPlain` holds the pad words; the random variant draws them from the OS RNG.
struct ChallengeAnswer {
    Bytes answer;
    uint32_t sigmaPad = 0;
    Bytes plainBeforeOuter;
    uint8_t innerSum = 0;
};
std::optional<ChallengeAnswer> mintChallengeAnswer(ByteSpan challenge, std::string_view accountId, uint32_t installId,
                                                   uint32_t innerKey, ByteSpan padPlain, std::string* err = nullptr);
std::optional<ChallengeAnswer> mintChallengeAnswerRandom(ByteSpan challenge, std::string_view accountId,
                                                         uint32_t installId, uint32_t innerKey,
                                                         std::string* err = nullptr);

// ---- Rekey ----
uint32_t rekey(std::string_view certStr, uint32_t seed);
uint32_t rekeyedLoginKey(std::string_view certStr,
                         uint32_t kOld);  // Rekeyed login key; a zero K_old maps to a fixed constant.

// certStr from three words; the random variant uses the OS RNG.
std::string certStrFrom(uint32_t r1, uint32_t r2, uint32_t r3);
std::string generateCertStr();
// cfg/cauth.txt record name: ckey(account)+"@"+addr.
std::string cauthName(std::string_view accountKey, std::string_view serverAddr);
// Response body for a cached-certificate login flag.
Bytes encodeCertResponseCached(std::string_view certStr, std::string_view pp);

// ---- login-cert token mint over the hub link ----
// key5e scrambles both this request and the hub's reply.
Bytes buildCertMintRequest(uint32_t cauthKey, std::string_view certStr, uint32_t key5e);
// Parse the hub's token reply; the token is the hex text it carries.
std::optional<uint32_t> parseHubTokenReply(ByteSpan body, uint32_t key5e, std::string* err = nullptr);
// Game-socket answer that carries the minted token.
Bytes encodeCertResponseToken(uint32_t token);

}  // namespace fugue::account
