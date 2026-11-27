// Which account a client run plays as (app/main.cpp). Rules, in order:
//   1. `--account K` (the pager's join passes it whenever an account is signed in) is K, read from its identity file.
//      No usable identity -> the run fails (exit 2). `--account ""` is an explicit guest.
//   2. No --account on a DreamSeeker-style player launch: the account signed in through the pager (pager.json's
//      `account` and ~/.fugue/identity-<ckey>), the way DreamSeeker reads the pager-written key.txt. Nothing about the
//      account is on argv. No account signed in, the "Play as guest" choice (~/.fugue/qt/pager-guest), or no usable
//      identity for that key -> a guest.
//   3. No --account on any other run -> a guest; pager.json and the identity files are not read.
// A player launch is invoked as DreamSeeker (argv[0] dreamseeker / dreamseeker.exe) or has a DreamSeeker-grammar target
// (byond://..., host:port, +connect), and carries no --connect, --state-dir or test hook. Fugue's own runs
// are therefore guests unless --account names one, so a development run never reads a real HOME's identity.
// Rules 1 and 3 are this client's policy (stock DreamSeeker has one key.txt and no test modes); rule 2 mirrors
// DreamSeeker reading cfg/key.txt with no account on argv.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include "account/login.h"
#include "pager/dsargv.h"

namespace fugue::pager {

struct AccountRequest {
    bool accountGiven = false;  // --account was on the command line (even as "")
    std::string account;        // its value (the last one given)
    bool playerLaunch = false;  // rule 2 applies; false (the default) = rule 3, a guest without --account
    std::string why;            // a log line: what made it a player launch, or what did not
};

// The client's own --option names. asDreamSeeker: unknown --words are dropped, as DreamSeeker drops them (pager/dsargv.h).
FugueOptions clientOptions(bool asDreamSeeker);

// The account half of a client command line, parsed as app/main.cpp parses it. Fugue's options are read out of ds.fugue
// as QCommandLineParser reads them (`--name value`, `--name=value`, the last one wins). testHook: a dev-build FUGUE_AUTO_*
// hook is set (always false in a release build).
AccountRequest accountRequestFor(bool asDreamSeeker, const DsArgv& ds, const FugueOptions& opts, bool testHook);

struct AccountDecision {
    enum Kind { Guest, Account, Fail };
    Kind kind = Guest;
    std::string account;                         // Account / Fail: the key tried
    bool fromPager = false;                      // the key came from pager.json (rule 2), not argv
    std::optional<account::AccountLogin> login;  // Account: the minted 0x1a record (a bearer token: never log it)
    std::string reason;                          // a log line: why a guest, or why the run fails. Never a secret
};

// mint: identity -> 0x1a record. Default: account::mintLocalAccountLogin with an empty password (the client never reads one).
using MintFn = std::function<std::optional<account::AccountLogin>(const std::string& key, std::string* err)>;
AccountDecision decideAccount(const AccountRequest& req, const MintFn& mint = {});

}  // namespace fugue::pager
