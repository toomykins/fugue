// Login-certificate token for a hub-registered server: answers the 0x001c challenge with a token minted over the hub
// link, sent back as the game-socket 0x0036, then re-keys the cipher as the 0x0036 ack requires.
// installId and innerKey are per account; another account's pair makes the hub answer as that account.
// cauthKey and certStr come from the per-connection cauth state (net/cauth.h).
#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include "account/login.h"
#include "net/bytes.h"
#include "net/session.h"
#include "pager/hublink.h"

namespace fugue::pager {

constexpr int kHubCertTimeoutMs = 15000;                     // whole-exchange deadline for the token mint
constexpr std::chrono::milliseconds kHubMintGap{5000};       // least time between two token mints
constexpr std::chrono::milliseconds kHubMintMaxGap{300000};  // the gap grows with each consecutive failure up to this

struct HubCertIdentity {
    std::string accountId;
    uint32_t installId = 0, innerKey = 0, cauthKey = 0;
    std::string certStr;
};

// The live token mint. padPlain (tests only) pins the 0x0042 answer's padding; null = random. Never logs a value.
std::optional<uint32_t> queryTokenCert(const std::string& hubAddr, const HubCertIdentity& id, int timeoutMs,
                                       std::string* err, const Bytes* padPlain = nullptr);

// The per-session identity for an account login: fresh accountId, cauthKey and certStr left 0/empty for the session.
// nullopt when the identity has no hub constants (an identity cache written without them): *why says so.
std::optional<HubCertIdentity> hubCertIdentityFor(const account::AccountLogin& al, std::string* why);

// Arms cfg.hubCertAnswer / cfg.hubRekey for this account login. false (cfg untouched, *why set) when hubCertIdentityFor
// fails. A non-hub server never sends 0x001c, so arming it costs nothing there. Mints are spaced by HubThrottle
// (kHubMintGap, growing after failures), so a server that repeats its challenge cannot make the client hammer the hub.
// The answer outlives one session (a link() joins the next world with the same cfg): a challenge that comes inside
// kHubMintGap of the last mint blocks until the gap has passed; one with more than kHubMintGap still to wait is refused.
bool wireHubCert(net::SessionConfig& cfg, const account::AccountLogin& al, std::string* why,
                 const std::string& hubAddr = kHubLinkAddr, int timeoutMs = kHubCertTimeoutMs);

}  // namespace fugue::pager
