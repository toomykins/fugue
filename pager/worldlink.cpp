#include "pager/worldlink.h"
#include "pager/clientaccount.h"
#include "pager/dsargv.h"
#include "pager/target.h"

namespace fugue::pager {

std::optional<std::vector<std::string>> worldLinkArgs(const std::vector<std::string>& args,
                                                      const std::vector<bool>& hostTaken, bool asDreamSeeker,
                                                      const std::string& url, std::string* err) {
    std::optional<std::string> t = parseTarget(url, err);
    if (!t) return std::nullopt;
    const FugueOptions fo = clientOptions(asDreamSeeker);
    const DsArgv ds = parseDsArgv(args, fo, hostTaken);
    auto taken = [&](size_t j) { return j < hostTaken.size() && hostTaken[j]; };
    std::vector<std::string> out;
    std::vector<bool> outTaken;
    auto keep = [&](size_t j, std::string v) {
        out.push_back(std::move(v));
        outTaken.push_back(taken(j));
    };
    bool connect = false;
    for (size_t j = 0; j < args.size(); j++) {
        const std::string& a = args[j];
        if (taken(j)) {
            keep(j, a);
            continue;
        }
        if (int(j) == ds.targetArg) {
            if (a == "+connect") keep(j++, a);
            keep(j, "byond://" + *t);
        } else if (a.size() > 2 && a[0] == '-' && a[1] == '-') {
            const size_t eq = a.find('=');
            const std::string name = a.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
            const bool withNext =
                eq == std::string::npos && fo.value.count(name) && j + 1 < args.size() && !taken(j + 1);
            if (name == "connect")
                connect = true;
            else
                keep(j, a);
            if (withNext) {
                j++;
                if (name != "connect") keep(j, args[j]);
            }
        } else if (a[0] == '+' && j + 1 < args.size()) {
            keep(j, a);
            j++;
            keep(j, args[j]);
        } else {
            keep(j, a);
        }
    }
    if (connect || ds.targetArg < 0) {
        out.push_back(connect || !asDreamSeeker ? "--connect=" + *t : "byond://" + *t);
        outTaken.push_back(false);
    }
    const AccountRequest was = accountRequestFor(asDreamSeeker, ds, fo, false);
    const AccountRequest now = accountRequestFor(asDreamSeeker, parseDsArgv(out, fo, outTaken), fo, false);
    if (was.playerLaunch != now.playerLaunch || was.accountGiven != now.accountGiven || was.account != now.account) {
        if (err) *err = "the new command line would not play as this one (" + was.why + " / " + now.why + ")";
        return std::nullopt;
    }
    if (!asDreamSeeker) out.insert(out.begin(), "client");
    return out;
}

}  // namespace fugue::pager
