#include "shell/browsestore.h"
#include <algorithm>

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
    static const char* skip[] = {"dmi", "ogg", "wav", "mp3", "mid", "midi", "mod",
                                 "xm",  "it",  "s3m", "dmf", "mp4", "webm", "ogv"};
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

}  // namespace fugue::shell
