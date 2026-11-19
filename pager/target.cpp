#include "pager/target.h"
#include "pager/text.h"

namespace fugue::pager {
namespace {
// Splits host:port; false on any malformed input (parseTarget reports every kind the same way).
bool splitHostPort(const std::string& hp, std::string* host, std::string* port) {
    size_t i = hp.rfind(':');
    if (i == std::string::npos) return false;
    size_t j, k;
    if (!hp.empty() && hp[0] == '[') {
        size_t end = hp.find(']');
        if (end == std::string::npos) return false;
        if (end + 1 == hp.size()) return false;
        if (end + 1 != i) return false;
        *host = hp.substr(1, end - 1);
        j = 1;
        k = end + 1;
    } else {
        *host = hp.substr(0, i);
        if (host->find(':') != std::string::npos) return false;
        j = 0;
        k = i;
    }
    if (hp.find('[', j) != std::string::npos) return false;
    if (hp.find(']', k) != std::string::npos) return false;
    *port = hp.substr(i + 1);
    return true;
}
bool cutPrefixFold(std::string& s, const char* prefix) {
    size_t n = std::string_view(prefix).size();
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = char(a + 32);
        if (a != b) return false;
    }
    s.erase(0, n);
    return true;
}
bool allDigitsDotted(const std::string& s) {
    for (char c : s)
        if (c != '.' && (c < '0' || c > '9')) return false;
    return true;
}
}  // namespace

std::optional<std::string> parseTarget(std::string_view in, std::string* err) {
    auto fail = [&](std::string m) {
        if (err) *err = std::move(m);
        return std::nullopt;
    };
    std::string s = trimSpace(in);
    if (s.empty()) return fail("no server address given");
    if (cutPrefixFold(s, "+connect")) s = trimSpace(s);
    cutPrefixFold(s, "byond://");
    if (auto i = s.find_first_of("#?"); i != std::string::npos) s.erase(i);
    if (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return fail("no server address given");
    std::string host, port;
    if (!splitHostPort(s, &host, &port)) {
        size_t dots = 0;
        for (char c : s) dots += c == '.';
        if (dots == 1 && s.find_first_of(":/") == std::string::npos && !allDigitsDotted(s))
            return fail(goQuote(s) +
                        " is a hub WORLD reference (byond://Key.World), not an address \xe2\x80\x94 "
                        "Fugue does not look hub worlds up by name. Use the server's host:port");
        return fail(goQuote(s) + " has no port \xe2\x80\x94 a BYOND address is host:port (e.g. play.example.org:7777)");
    }
    if (host.empty()) return fail(goQuote(s) + " has no host");
    long long n;
    if (!goAtoi(port, &n) || n < 1 || n > 65535) return fail(goQuote(s) + " has a bad port " + goQuote(port));
    if (host.find(':') != std::string::npos) return "[" + host + "]:" + port;
    return host + ":" + port;
}

}  // namespace fugue::pager
