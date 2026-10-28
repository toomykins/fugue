#include "pager/dsargv.h"
#include <algorithm>

namespace fugue::pager {
namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }
bool eqNoCase(const std::string& a, const char* b) {   // ASCII case-insensitive equality, as DreamSeeker's flag match
    size_t i = 0;
    for (; i < a.size() && b[i]; i++)
        if (lower(a[i]) != lower(b[i])) return false;
    return i == a.size() && !b[i];
}
}  // namespace

DsArgv parseDsArgv(const std::vector<std::string>& args, const FugueOptions& fo, const std::vector<bool>& hostTaken) {
    DsArgv d;
    const size_t argc = args.size() + 1;   // argv[0] and Qt's arguments, which DreamSeeker never had removed
    auto taken = [&](size_t j) { return j < hostTaken.size() && hostTaken[j]; };
    for (size_t j = 0; j < args.size(); j++) {
        if (taken(j)) continue;            // consumed by Qt: DreamSeeker counts it, Fugue's argv does not
        const std::string& a = args[j];
        const size_t i = j + 1;            // DS's index into argv
        if (a.size() > 2 && a[0] == '-' && a[1] == '-') {   // a Fugue option: not part of DreamSeeker's grammar
            std::string name = a.substr(2);
            const size_t eq = name.find('=');
            const std::string bare = eq == std::string::npos ? name : name.substr(0, eq);
            if (fo.value.count(bare) || fo.flag.count(bare) || fo.keepUnknown) {
                d.fugue.push_back(a);
                if (eq == std::string::npos && fo.value.count(bare) && j + 1 < args.size() && !taken(j + 1)) d.fugue.push_back(args[++j]);
                continue;
            }
            d.dropped.push_back(a);        // invoked as DreamSeeker: an unknown --word is one more unknown '-' argument
            continue;
        }
        if (!a.empty() && a[0] == '-') {
            if (eqNoCase(a, "-trace")) d.trace = true;
            else if (eqNoCase(a, "-pager")) d.pager = true;
            else if (eqNoCase(a, "-trusted")) d.security = 0;
            else if (eqNoCase(a, "-safe")) d.security = 1;
            else if (eqNoCase(a, "-ultrasafe")) d.security = 2;
            else if (eqNoCase(a, "-isserver") || eqNoCase(a, "-is-server")) d.isServer = true;
            else if (a == "-debug") d.debug = true;
            else d.dropped.push_back(a);
            continue;
        }
        if (!a.empty() && a[0] == '+') {
            // Every '+' argument consumes the next one; only `+connect` with one more argument after it uses it.
            if (a == "+connect" && i + 2 < argc && !taken(j + 1)) {
                const std::string& v = args[j + 1];
                d.target = v.compare(0, 8, "byond://") == 0 ? v : "byond://" + v;
                d.targetArg = int(j);
            } else {
                d.dropped.push_back(a);
                if (j + 1 < args.size() && !taken(j + 1)) d.dropped.push_back(args[j + 1]);
            }
            j++;
            continue;
        }
        if (d.target.empty()) { d.target = a; d.targetArg = int(j); }
        else d.dropped.push_back(a);
    }
    return d;
}

std::vector<bool> takenByHost(const std::vector<const char*>& before, const std::vector<const char*>& after) {
    const std::set<const char*> kept(after.begin(), after.end());
    std::vector<bool> t(before.size());
    for (size_t j = 0; j < before.size(); j++) t[j] = !kept.count(before[j]);
    return t;
}

bool invokedAsDreamSeeker(const std::string& argv0) {
    size_t s = argv0.find_last_of("/\\");
    std::string base = s == std::string::npos ? argv0 : argv0.substr(s + 1);
    return eqNoCase(base, "dreamseeker") || eqNoCase(base, "dreamseeker.exe");
}

Mode chooseMode(const std::string& argv0, const std::vector<std::string>& args, bool release) {
    Mode m;
    m.asDreamSeeker = invokedAsDreamSeeker(argv0);
    m.mode = (release && !m.asDreamSeeker) ? "pager" : "client";
    m.rest = args;
    if (!m.asDreamSeeker && !m.rest.empty() && (m.rest[0] == "pager" || m.rest[0] == "client" || m.rest[0] == "version" || m.rest[0] == "webcheck")) {
        m.mode = m.rest[0];
        m.rest.erase(m.rest.begin());
    }
    // --version is read in every mode, as the only argument when invoked as DreamSeeker (which has no such flag).
    const bool onlyArg = m.rest.size() == 1 && m.rest[0] == "--version";
    if (m.asDreamSeeker ? onlyArg : std::find(m.rest.begin(), m.rest.end(), "--version") != m.rest.end()) m.mode = "version";
    return m;
}

NoTarget noTargetAction(size_t dsArgc, const DsArgv& d) {
    if (dsArgc <= 1) return NoTarget::OpenLocation;
    return d.fugue.empty() ? NoTarget::QuietExit : NoTarget::Usage;
}

}  // namespace fugue::pager
