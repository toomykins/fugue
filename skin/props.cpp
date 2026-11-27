#include "skin/props.h"

#include "skin/dmf.h"
#include "skin/winset.h"

namespace fugue::skin {

bool winsetBool(std::string_view v) {
    std::string l = lowerAscii(trimSpace(v));
    return l == "true" || l == "1" || l == "on" || l == "yes";
}

namespace {
std::string norm(const std::string& s) { return lowerAscii(trimSpace(s)); }
}  // namespace

void PropStore::set(const std::string& target, const std::string& key, const std::string& val) {
    std::string t = norm(target), k = norm(key);
    if (t.empty() || k.empty()) return;
    p_[t][k] = {val, ++revision_};
}

void PropStore::erase(const std::string& target, const std::string& key) {
    auto t = p_.find(norm(target));
    if (t == p_.end()) return;
    t->second.erase(norm(key));
    if (t->second.empty()) p_.erase(t);
}

void PropStore::eraseTarget(const std::string& target) { p_.erase(norm(target)); }

bool PropStore::get(const std::string& target, const std::string& key, std::string& out) const {
    auto t = p_.find(norm(target));
    if (t == p_.end()) return false;
    auto k = t->second.find(norm(key));
    if (k == t->second.end()) return false;
    out = k->second.text;
    return true;
}

uint64_t PropStore::revision(const std::string& target, const std::string& key) const {
    auto t = p_.find(norm(target));
    if (t == p_.end()) return 0;
    auto k = t->second.find(norm(key));
    return k == t->second.end() ? 0 : k->second.revision;
}

bool PropStore::controlProp(const std::string& window, const std::string& name, const std::string& key,
                            std::string& out) const {
    if (!window.empty() && get(window + "." + name, key, out)) return true;
    return !name.empty() && get(name, key, out);
}

bool PropStore::controlBool(const std::string& window, const std::string& name, const std::string& key,
                            bool& out) const {
    std::string v;
    if (!controlProp(window, name, key, v)) return false;
    out = winsetBool(v);
    return true;
}

bool PropStore::controlPoint(const std::string& window, const std::string& name, const std::string& key, long long& a,
                             long long& b) const {
    std::string v;
    return controlProp(window, name, key, v) && parsePair(v, ",", a, b);
}

bool PropStore::controlSize(const std::string& window, const std::string& name, const std::string& key, long long& a,
                            long long& b) const {
    std::string v;
    return controlProp(window, name, key, v) && parsePair(v, "x,", a, b);
}

bool PropStore::has(const std::string& target) const {
    auto t = p_.find(norm(target));
    return t != p_.end() && !t->second.empty();
}

std::vector<std::string> PropStore::targets() const {
    std::vector<std::string> out;
    for (const auto& [t, m] : p_) out.push_back(t);
    return out;
}

}  // namespace fugue::skin
