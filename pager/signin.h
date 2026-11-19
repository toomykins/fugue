// Sign in: BYOND username + password -> the hub's key.txt block, cached as the identity the client logs in with.
// The DownloadKey exchange is in signin.cpp. The 0x0042 challenge is not answered: replying makes the hub close
// the connection before the 0x0088 reply.
// The password lives only in the caller's memory for this call. The identity file and key.txt are bearer tokens:
// written 0600 and never logged.
#pragma once
#include <cstdint>
#include <string>
#include "net/bytes.h"
#include "pager/hublink.h"

namespace fugue::pager {

constexpr int kSignInTimeoutMs = 30000;  // sign-in deadline

enum class SignInFailure { None, MissingInput, WrongPassword, Unreachable, NoAnswer, Refused, BadKey, Disk };

struct SignInResult {
    bool ok = false;
    SignInFailure failure = SignInFailure::None;
    std::string account;     // on success: the account name as the hub spells it
    std::string keyTxtPath;  // where the block was written
    std::string detail;      // the technical sentence (for Details / the log). Never contains the password.
};

// What a player is told: plain words, no protocol detail.
std::string plainSignInMessage(SignInFailure f);

// The key.txt a fetched block goes into: explicit, $FUGUE_KEYTXT, the first EXISTING of
// ~/.byond/cfg/key.txt, ~/.byond/key.txt, ~/Documents/BYOND/cfg/key.txt, ~/Documents/BYOND/key.txt, else ~/.fugue/key.txt
// (not ~/.byond/cfg/key.txt: see signin.cpp for why).
std::string keyTxtWriteTarget(const std::string& explicitPath);

// "BEGIN KEY <name>" -> name.
std::string blockAccountName(const std::string& block);

// The full sign-in. Blocking (run it off the UI thread). hubAddr is "host:port"; only tests and dev builds pass another.
SignInResult signIn(const std::string& username, const std::string& password, const std::string& keytxtExplicit,
                    const std::string& hubAddr = kHubLinkAddr, int timeoutMs = kSignInTimeoutMs);

// The 0x00db hello body every dtclink participant sends first (byte-identical across roles).
fugue::Bytes hubHelloPayload();

}  // namespace fugue::pager
