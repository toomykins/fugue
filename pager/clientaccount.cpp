#include "pager/clientaccount.h"
#include "account/store.h"
#include "pager/launch.h"

namespace fugue::pager {

FugueOptions clientOptions(bool asDreamSeeker) {
    FugueOptions o;
    o.value = {"connect", "state-dir", "rsc-dir", "size", "account", "identity-file", "audio"};
    o.flag = {"no-pacing", "hub", "help", "help-all"};  // help / help-all: QCommandLineParser::addHelpOption
    o.keepUnknown = !asDreamSeeker;
    return o;
}

AccountRequest accountRequestFor(bool asDreamSeeker, const DsArgv& ds, const FugueOptions& opts, bool testHook) {
    AccountRequest r;
    bool connect = false, stateDir = false;
    for (size_t i = 0; i < ds.fugue.size(); i++) {
        const std::string& a = ds.fugue[i];
        if (a.size() <= 2 || a[0] != '-' || a[1] != '-')
            continue;  // a value parseDsArgv already paired with its option
        const size_t eq = a.find('=', 2);
        const std::string name = a.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
        std::string value;
        if (eq != std::string::npos)
            value = a.substr(eq + 1);
        else if (opts.value.count(name) && i + 1 < ds.fugue.size())
            value = ds.fugue[++i];  // QCommandLineParser takes the next one whatever it is
        if (name == "account") {
            r.accountGiven = true;
            r.account = value;
        } else if (name == "connect")
            connect = true;
        else if (name == "state-dir")
            stateDir = true;
    }
    const bool dsTarget = !ds.target.empty() && !connect;
    // Fugue's own --connect is always a guest, even under the dreamseeker name: no real DreamSeeker launch carries it.
    if (connect)
        r.why = "--connect";
    else if (!asDreamSeeker && !dsTarget)
        r.why = "no DreamSeeker target";
    else if (stateDir)
        r.why = "--state-dir";
    else if (testHook)
        r.why = "a test hook";
    else {
        r.playerLaunch = true;
        r.why = asDreamSeeker ? "invoked as DreamSeeker" : "a DreamSeeker target";
    }
    return r;
}

AccountDecision decideAccount(const AccountRequest& req, const MintFn& mintFn) {
    MintFn mint = mintFn ? mintFn : [](const std::string& k, std::string* e) {
        return account::mintLocalAccountLogin(k, "", "", e);
    };
    AccountDecision d;
    std::string key;
    if (req.accountGiven) {
        key = req.account;
        if (key.empty()) {
            d.reason = "--account \"\": a guest";
            return d;
        }
    } else if (!req.playerLaunch) {
        d.reason = "no --account and not a DreamSeeker-style player launch (" + req.why +
                   "): a guest; the pager's account is not read";
        return d;
    } else {
        std::string perr;
        auto pc = account::loadConfig(&perr);
        if (!pc) {
            d.reason = "pager config unreadable (" + perr + "): a guest";
            return d;
        }
        if (pc->account.empty()) {
            d.reason = "nobody is signed in through the pager: a guest";
            return d;
        }
        if (guestRemembered()) {
            d.reason = "the pager's \"Play as guest\" choice: a guest";
            return d;
        }
        key = pc->account;
        d.fromPager = true;
    }
    d.account = key;
    std::string aerr;
    if (auto al = mint(key, &aerr)) {
        d.kind = AccountDecision::Account;
        d.login = std::move(al);
        return d;
    }
    if (d.fromPager) {
        d.reason = "signed-in account " + key + " has no usable identity (" + aerr + "): a guest";
        return d;  // kind stays Guest
    }
    d.kind = AccountDecision::Fail;
    d.reason = aerr;
    return d;
}

}  // namespace fugue::pager
