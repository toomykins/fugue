#include "shell/browsestore.h"
#include <algorithm>
#include <utility>

namespace fugue::shell {

std::string BrowseRscStore::baseName(const std::string& s) {
    size_t p = s.find_last_of("/\\");
    return p == std::string::npos ? s : s.substr(p + 1);
}

bool BrowseRscStore::blind(const std::string& name) {
    size_t d = name.rfind('.');
    if (d == std::string::npos) return false;
    std::string e = name.substr(d + 1);
    for (auto& c : e) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    // Game-only icon/skin resources stay outside the browser cache. Media files
    // must be available to HTML audio/video, regardless of native mixer support.
    static const char* skip[] = {"dmi", "dmf"};
    return std::any_of(std::begin(skip), std::end(skip), [&](const char* s) { return e == s; });
}

void BrowseRscStore::declare(const std::string& name, uint32_t id) {
    std::lock_guard<std::mutex> g(mu_);
    if (name.empty()) {
        if (!byId_.count(id)) anonPending_.insert(id);
        return;
    }
    nameToId_[name] = id;
}

void BrowseRscStore::put(uint32_t id, const std::string& name, const std::string& bytes) {
    std::lock_guard<std::mutex> g(mu_);
    anonPending_.erase(id);
    if (blind(name) || bytes.size() > (32u << 20)) return;
    byId_[id] = bytes;
    if (!name.empty()) {
        idName_[id] = name;
        byBase_[baseName(name)] = id;
    }
}

void BrowseRscStore::alias(const std::string& aliasName, uint32_t id) {
    std::lock_guard<std::mutex> g(mu_);
    if (!aliasName.empty()) nameToId_[aliasName] = id;
}

bool BrowseRscStore::get(const std::string& name, std::string& bytes) const {
    std::lock_guard<std::mutex> g(mu_);
    auto hit = [&](uint32_t id) {
        auto b = byId_.find(id);
        if (b == byId_.end() || b->second.empty()) return false;
        bytes = b->second;
        return true;
    };
    if (auto n = nameToId_.find(name); n != nameToId_.end() && hit(n->second)) return true;
    if (auto n = nameToId_.find(baseName(name)); n != nameToId_.end() && hit(n->second)) return true;
    for (const std::string& k : {name, baseName(name)})
        if (auto n = byBase_.find(k); n != byBase_.end() && hit(n->second)) return true;
    return false;
}

BrowseRscStore::Wait BrowseRscStore::waitFor(const std::string& name) const {
    if (blind(name)) return Wait::No;
    std::lock_guard<std::mutex> g(mu_);
    for (const std::string& k : {name, baseName(name)}) {
        auto n = nameToId_.find(k);
        if (n != nameToId_.end() && !byId_.count(n->second)) return Wait::Announced;  // announced, still in flight
        if (n != nameToId_.end()) return Wait::No;                                    // announced and here: no wait
    }
    return anonPending_.empty() ? Wait::No : Wait::Unnamed;
}

size_t BrowseRscStore::files() const {
    std::lock_guard<std::mutex> g(mu_);
    return byId_.size();
}

void BrowseRscStore::wantIcon(const std::string& key) {
    std::lock_guard<std::mutex> g(mu_);
    if (icons_.count(key) || !iconsAsked_.insert(key).second) return;
    iconsWanted_.push_back(key);
}

std::vector<std::string> BrowseRscStore::takeWantedIcons() {
    std::lock_guard<std::mutex> g(mu_);
    return std::exchange(iconsWanted_, {});
}

void BrowseRscStore::putIcon(const std::string& key, std::string png) {
    std::lock_guard<std::mutex> g(mu_);
    if (icons_.size() >= 4096) icons_.clear();  // a page asks again for what it still shows
    icons_[key] = std::move(png);
    iconsAsked_.erase(key);
}

bool BrowseRscStore::icon(const std::string& key, std::string& png) const {
    std::lock_guard<std::mutex> g(mu_);
    const auto it = icons_.find(key);
    if (it == icons_.end()) return false;
    png = it->second;
    return true;
}

}  // namespace fugue::shell
