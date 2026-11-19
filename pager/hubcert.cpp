#include "pager/hubcert.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include "account/records.h"

namespace fugue::pager {
namespace {
constexpr uint16_t kChallengeType = 0x0042;      // hub challenge
constexpr uint16_t kHubReplyType = 0x0043;       // hub token reply
constexpr uint16_t kHelloType = 0x00db;          // hello
constexpr uint16_t kHelloEEType = 0x00ee;        // hello (EE)
constexpr uint16_t kCertRequestType = 0x001c;    // login-certificate request
constexpr uint16_t kServerlistReqType = 0x00a3;  // server list request
constexpr uint16_t kTokenAckType = 0x007b;       // token ack: byte 0 = online
constexpr uint16_t kTokenPreReplyType = 0x00a2;  // token pre-reply: 3 bytes, ignored
const Bytes kHello{0x02, 0x00, 0x01, 0x00, 0x04,
                   0x02, 0x94, 0x06, 0x00};  // hello payload (see hubHelloPayload in pager/signin.h)
}  // namespace

std::optional<uint32_t> queryTokenCert(const std::string& hubAddr, const HubCertIdentity& id, int timeoutMs,
                                       std::string* err, const Bytes* padPlain) {
    std::string e;
    auto fail = [&](std::string m) -> std::optional<uint32_t> {
        if (err) *err = std::move(m);
        return std::nullopt;
    };
    HubLink link(HubLink::Clock::now() + std::chrono::milliseconds(timeoutMs));
    if (!link.dial(hubAddr, &e)) return fail(e);
    uint16_t type = 0;
    Bytes body;
    if (!link.readFrame(&type, &body, &e)) return fail("hub: read challenge: " + e);
    if (type != kChallengeType || body.size() < 16)
        return fail("hub: bad challenge (type 0x" +
                    [&] {
                        char b[8];
                        std::snprintf(b, sizeof b, "%04x", type);
                        return std::string(b);
                    }() +
                    " len " + std::to_string(body.size()) + ")");
    const uint32_t nonce = le32(&body[12]);
    auto ans = padPlain ? account::mintChallengeAnswer(body, id.accountId, id.installId, id.innerKey, *padPlain, &e)
                        : account::mintChallengeAnswerRandom(body, id.accountId, id.installId, id.innerKey, &e);
    if (!ans) return fail("hub: build challenge answer: " + e);
    const uint32_t key5e = nonce + ans->sigmaPad;  // scrambles the 0x001c request and the 0x0043 reply
    const std::pair<uint16_t, Bytes> out[] = {
        {kHelloType, kHello},
        {kHelloEEType, {}},
        {kChallengeType, ans->answer},
        {kCertRequestType, account::buildCertMintRequest(id.cauthKey, id.certStr, key5e)},
        {kServerlistReqType, {0x00}}};
    for (auto& f : out)
        if (!link.writeFrame(f.first, f.second, &e)) return fail("hub: write frame: " + e);
    bool online = false;
    for (;;) {
        if (!link.readFrame(&type, &body, &e)) return fail("hub: read reply: " + e);
        if (type == kTokenAckType) {
            online = !body.empty() && body[0] != 0;
            continue;
        }
        if (type == kTokenPreReplyType) continue;
        if (type != kHubReplyType) {
            char b[8];
            std::snprintf(b, sizeof b, "%04x", type);
            return fail(std::string("hub: unexpected frame 0x") + b + " before token reply");
        }
        auto tok = account::parseHubTokenReply(body, key5e, &e);
        if (!tok) return fail(e + (online ? " (online=true)" : " (online=false)"));
        return tok;
    }
}

std::optional<HubCertIdentity> hubCertIdentityFor(const account::AccountLogin& al, std::string* why) {
    if (al.installId == 0 || al.innerKey == 0) {
        if (why) *why = "the identity (" + al.source + ") carries no hub constants; sign in through the pager again";
        return std::nullopt;
    }
    HubCertIdentity id;
    std::random_device rd;
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) id.accountId.push_back(hx[rd() & 15]);
    id.installId = al.installId;
    id.innerKey = al.innerKey;
    return id;  // cauthKey + certStr are the session's (net/cauth.h CauthCache), filled in per challenge by wireHubCert's answer
}

bool wireHubCert(net::SessionConfig& cfg, const account::AccountLogin& al, std::string* why, const std::string& hubAddr,
                 int timeoutMs) {
    auto id = hubCertIdentityFor(al, why);
    if (!id) return false;
    // A server can send 0x001c as often as it likes, so the mints are spaced out, and spaced further after failures.
    struct Gate {
        std::mutex m;
        HubThrottle t{kHubMintGap, kHubMintMaxGap};
    };
    auto gate = std::make_shared<Gate>();
    cfg.hubCertAnswer = [id = *id, hubAddr, timeoutMs, gate](uint32_t cauthKey, const std::string& certStr,
                                                             std::string* err) -> std::optional<Bytes> {
        // A challenge inside the plain gap waits it out: the session that a link() started right after a login is a new
        // connection the server will not initialise without an answer. One with more than kHubMintGap still to wait is refused.
        for (;;) {
            int wait = 0;
            {
                std::lock_guard<std::mutex> lk(gate->m);
                if (gate->t.allow(HubThrottle::Clock::now(), &wait)) break;
            }
            if (wait > kHubMintGap.count() + 1) {
                if (err)
                    *err = "hub: token mint throttled, next allowed in " + std::to_string((wait + 999) / 1000) + " s";
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(wait));
        }
        HubCertIdentity q = id;
        q.cauthKey = cauthKey;  // the key the 0x1a carried
        q.certStr = certStr;
        auto tok = queryTokenCert(hubAddr, q, timeoutMs, err);
        {
            std::lock_guard<std::mutex> lk(gate->m);
            gate->t.record(bool(tok));
        }
        if (!tok) return std::nullopt;
        return account::encodeCertResponseToken(*tok);
    };
    cfg.hubRekey = [](const std::string& certStr, uint32_t kOld) { return account::rekeyedLoginKey(certStr, kOld); };
    return true;
}

}  // namespace fugue::pager
