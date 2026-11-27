#include "fugue_env.h"
#include "pager/signin.h"
#include "platform/file.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include "account/desc.h"
#include "account/keyblock.h"
#include "account/records.h"
#include "account/store.h"
#include "pager/hublink.h"

namespace fugue::pager {
namespace {
using Clock = HubLink::Clock;

constexpr uint16_t kChallengeType = 0x0042;    // hub challenge
constexpr uint16_t kHubReplyType = 0x0043;     // hub reply: [code u8][text cstring], scrambled + sum
constexpr uint16_t kHelloType = 0x00db;        // hello
constexpr uint16_t kDownloadKeyType = 0x0088;  // key download request/reply

bool isFile(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}
std::string slurp(const std::string& p) {
    std::string o;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) o.append(b, n);
        std::fclose(f);
    }
    return o;
}
std::string trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

SignInResult fail(SignInFailure f, std::string detail) {
    SignInResult r;
    r.failure = f;
    r.detail = std::move(detail);
    return r;
}
}  // namespace

std::string plainSignInMessage(SignInFailure f) {
    switch (f) {
        case SignInFailure::None: return "";
        case SignInFailure::MissingInput: return "Enter your BYOND username and password.";
        case SignInFailure::WrongPassword: return "Wrong username or password.";
        case SignInFailure::Unreachable: return "Could not reach BYOND. Check your internet connection and try again.";
        case SignInFailure::NoAnswer: return "BYOND did not answer. Try again in a minute.";
        case SignInFailure::Refused: return "BYOND would not sign you in right now. Try again later.";
        case SignInFailure::BadKey: return "Signed in, but your BYOND key could not be read. Try signing in again.";
        case SignInFailure::Disk: return "Could not save your sign-in on this computer.";
    }
    return "";
}

Bytes hubHelloPayload() { return {0x02, 0x00, 0x01, 0x00, 0x04, 0x02, 0x94, 0x06, 0x00}; }

std::string keyTxtWriteTarget(const std::string& explicitPath) {
    if (!explicitPath.empty()) return explicitPath;
    if (auto e = fugueEnv("KEYTXT"); e && !e->empty()) return *e;
    auto h = account::homeDir();
    if (!h) return "";
    const std::string native[] = {*h + "/.byond/cfg/key.txt", *h + "/.byond/key.txt",
                                  *h + "/Documents/BYOND/cfg/key.txt", *h + "/Documents/BYOND/key.txt"};
    for (auto& p : native)
        if (isFile(p)) return p;
    // No BYOND install: keep the block in Fugue's own directory. Not ~/.byond/cfg: inside a Flatpak that is the host's
    // directory mounted read-only, or part of the sandbox's throwaway home, where a write succeeds and is lost at the next start.
    return account::fugueDir(nullptr) + "/key.txt";
}

std::string blockAccountName(const std::string& block) {
    size_t i = block.find("BEGIN KEY ");
    if (i == std::string::npos) return "";
    std::string rest = block.substr(i + 10);
    if (size_t j = rest.find_first_of("\r\n"); j != std::string::npos) rest = rest.substr(0, j);
    return trim(rest);
}

SignInResult signIn(const std::string& usernameIn, const std::string& password, const std::string& keytxtExplicit,
                    const std::string& hubAddr, int timeoutMs) {
    const std::string username = trim(usernameIn);
    if (username.empty() || password.empty())
        return fail(SignInFailure::MissingInput, "a username and a password are both required");
    ByteSpan pw(reinterpret_cast<const uint8_t*>(password.data()), password.size());
    const std::string l1 = account::pwordCrypt(pw), l2 = account::pwordCryptLevel2(pw);
    std::string err;
    auto sid = account::pagerSid(&err);
    if (!sid) return fail(SignInFailure::Disk, "sid: " + err);
    if (sid->size() != 24) return fail(SignInFailure::Disk, "sid must be exactly 24 characters");

    // ---- the hub exchange (DownloadKey) ----
    HubLink link(Clock::now() + std::chrono::milliseconds(timeoutMs));
    if (!link.dial(hubAddr, &err)) return fail(SignInFailure::Unreachable, err);
    uint16_t type = 0;
    Bytes body;
    if (!link.readFrame(&type, &body, &err)) return fail(SignInFailure::Unreachable, "hub: read challenge: " + err);
    if (type != kChallengeType || body.size() < 16)
        return fail(SignInFailure::NoAnswer, "hub: expected a 0x0042 challenge of >= 16 bytes");
    const uint32_t nonce =
        uint32_t(body[12]) | uint32_t(body[13]) << 8 | uint32_t(body[14]) << 16 | uint32_t(body[15]) << 24;
    if (!link.writeFrame(kHelloType, hubHelloPayload(), &err))
        return fail(SignInFailure::Unreachable, "hub: write hello: " + err);
    // The second name is BYOND's fixed hub service name in this request, not an account of ours.
    if (!link.writeFrame(kDownloadKeyType, account::buildDownloadKeyRequest(username, "dantom", l2, l1, *sid, nonce),
                         &err))
        return fail(SignInFailure::Unreachable, "hub: write 0x0088: " + err);
    std::string block;
    for (;;) {
        if (!link.readFrame(&type, &body, &err)) return fail(SignInFailure::NoAnswer, "hub: read 0x0088 reply: " + err);
        if (type == kDownloadKeyType) {
            std::string perr;
            auto b = account::parseDownloadKeyReply(body, nonce, &perr);
            if (!b) {
                if (perr.find("status != 1") != std::string::npos)
                    return fail(SignInFailure::WrongPassword,
                                "hub refused the key for \"" + username +
                                    "\" \xe2\x80\x94 wrong password, or the account does not exist");
                return fail(SignInFailure::NoAnswer, perr);
            }
            block = *b;
            break;
        }
        if (type == kHubReplyType && !body.empty()) {
            Bytes p(body.begin(), body.end() - 1);
            account::hubUnscramble(p, nonce);
            std::string text;
            for (size_t i = 1; i < p.size() && p[i]; i++) text.push_back(char(p[i]));
            return fail(SignInFailure::Refused, "hub: DownloadKey refused (code=" +
                                                    std::to_string(p.empty() ? 0 : p[0]) + " text=\"" + text + "\")");
        }
    }

    // ---- write key.txt, adopt the hub's casing, decode + cache the identity ----
    SignInResult r;
    r.keyTxtPath = keyTxtWriteTarget(keytxtExplicit);
    if (r.keyTxtPath.empty()) return fail(SignInFailure::Disk, "$HOME is not defined");
    std::string existing = isFile(r.keyTxtPath) ? slurp(r.keyTxtPath) : std::string();
    if (!account::atomicWrite(r.keyTxtPath, account::keyTxtWithBlock(existing, block), &err))
        return fail(SignInFailure::Disk, "key.txt: " + err);
    r.account = blockAccountName(block);
    if (r.account.empty()) r.account = username;
    std::string derr;
    auto line1 = account::keyBlockLine1(account::keyTxtWithBlock("", block), r.account, &derr);
    std::optional<account::KeyBlockFields> f;
    if (line1) f = account::decodeKeyBlock(*line1, r.account, l2, &derr);
    if (!f) {
        // Drop any cached identity for this account; it may be a well-formed block that failed to decode.
        std::string ip = account::identityPath(r.account);
        if (!ip.empty()) platform::unlink(ip.c_str());
        auto out = fail(SignInFailure::BadKey,
                        "fetched " + r.account +
                            "'s key but could not decode it (any stale cached identity has been removed): " + derr);
        out.account = r.account;
        out.keyTxtPath = r.keyTxtPath;
        return out;
    }
    account::Account a;
    a.identity = f->identity;
    a.installId = f->installId;
    a.innerKey = f->innerKey;
    if (!account::saveAccount(r.account, a, &err)) return fail(SignInFailure::Disk, "identity: " + err);
    r.ok = true;
    r.detail = "wrote " + r.account + "'s key block to " + r.keyTxtPath + " and cached the identity";
    return r;
}

}  // namespace fugue::pager
