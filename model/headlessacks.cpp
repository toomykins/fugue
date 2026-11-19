#include "model/headlessacks.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace fugue::model {

namespace {
constexpr size_t kNavAckCap = 4096;
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

bool machineBrowser(const std::string& w) { return hostlessBrowser(w) || w.rfind("tooltip", 0) == 0; }

bool hostlessBrowser(const std::string& w) { return w == "asset_cache_browser"; }

std::optional<std::string> navShimHref(const std::string& html) {
    static const char* guards[] = {"function",         "=>",         "if",          "for",   "while", "switch", "try",
                                   "addeventlistener", "settimeout", "setinterval", "onload"};
    static const std::regex script(R"(<script\b[^>]*>([\s\S]*?)</script>)", std::regex::icase);
    static const std::regex nav(R"(\blocation(?:\.href)?\s*=\s*(['"])([^'"]*)['"]\s*([\s\S]?))");
    for (auto it = std::sregex_iterator(html.begin(), html.end(), script); it != std::sregex_iterator(); ++it) {
        std::string body = (*it)[1].str();
        std::string lo = lower(body);
        bool guarded = false;
        for (const char* g : guards)
            if (lo.find(g) != std::string::npos) {
                guarded = true;
                break;
            }
        if (guarded) continue;
        std::smatch m;
        if (!std::regex_search(body, m, nav)) continue;
        std::string after = m[3].str();
        if (after == "+") continue;  // concatenation: the literal is a fragment, not an address
        std::string dest = m[2].str();
        if (dest.rfind("byond://", 0) == 0 || dest.rfind("?", 0) == 0) return dest;
    }
    return std::nullopt;
}

std::vector<std::string> HeadlessAcks::collect(const std::map<std::string, std::string>& windowHtml) {
    std::vector<std::string> out;
    if (!goonChatAcked_ && windowHtml.count("browseroutput")) {
        goonChatAcked_ = true;
        out.push_back("?_src_=chat&proc=doneLoading");
    }
    for (const auto& [name, html] : windowHtml)
        if (auto href = navAck(name, html)) out.push_back(*href);
    return out;
}

std::optional<std::string> HeadlessAcks::navAck(const std::string& window, const std::string& html) {
    if (!machineBrowser(window)) return std::nullopt;
    auto href = navShimHref(html);
    if (!href || navAcked_.count(*href)) return std::nullopt;
    if (navAcked_.size() >= kNavAckCap) navAcked_.clear();  // job ids only grow: forgetting the answered ones is free
    navAcked_.insert(*href);
    return href;
}

}  // namespace fugue::model
