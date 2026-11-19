#include "skin/winicon.h"
#include "skin/dmf.h"

namespace fugue::skin {

IconRef parseIconRef(const std::string& value) {
    IconRef r;
    std::string v = trimSpace(value);
    if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    if (v.empty()) return r;
    // [0xTTNNNNNN]: a DM reference; type 0x0c is a resource, NNNNNN its ordinal in the ResourceList (winicon.h).
    if (v.size() > 4 && v.front() == '[' && v.back() == ']' && (v[1] == '0') && (v[2] == 'x' || v[2] == 'X')) {
        uint64_t n = 0;
        bool ok = v.size() <= 4 + 8;
        for (size_t i = 3; ok && i + 1 < v.size(); i++) {
            char c = v[i];
            int d = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
            if (d < 0)
                ok = false;
            else
                n = n * 16 + uint64_t(d);
        }
        if (ok && (n >> 24) == 0x0c) {
            r.kind = IconRef::Ordinal;
            r.ordinal = uint32_t(n & 0xffffff);
        }
        return r;  // another reference type names no file: no icon
    }
    r.kind = IconRef::Name;
    r.name = v;
    return r;
}

std::string IconResolver::key(const std::string& name) {
    std::string k = lowerAscii(name);
    for (char& c : k)
        if (c == '\\') c = '/';
    return k;
}

static bool imageFile(const std::string& k) {
    size_t d = k.rfind('.');
    if (d == std::string::npos) return false;
    std::string e = k.substr(d + 1);
    return e == "png" || e == "dmi" || e == "ico" || e == "bmp" || e == "jpg" || e == "jpeg" || e == "gif";
}

void IconResolver::noteResource(uint32_t id, const std::string& name) {
    std::string k = key(name.empty() && env_.nameOf ? env_.nameOf(id) : name);
    if (!imageFile(k)) return;
    byName_[k] = id;
    if (size_t sl = k.rfind('/'); sl != std::string::npos) byName_.emplace(k.substr(sl + 1), id);  // a full name wins
}

uint32_t IconResolver::idFor(const IconRef& r) const {
    uint32_t id = 0;
    if (r.kind == IconRef::Ordinal) {
        if (!env_.idAt || !env_.idAt(r.ordinal, id)) return 0;  // 0-based: [0xc000003] is the 4th entry
        return id;
    }
    if (r.kind != IconRef::Name) return 0;
    std::string k = key(r.name);
    auto it = byName_.find(k);
    if (it == byName_.end())
        if (size_t sl = k.rfind('/'); sl != std::string::npos) it = byName_.find(k.substr(sl + 1));
    return it == byName_.end() ? 0 : it->second;
}

bool IconResolver::update(const std::string& value, Out& out) {
    if (value == want_ && resolved_) return false;
    bool changed = value != want_;
    want_ = value;
    IconRef r = parseIconRef(value);
    uint32_t id = idFor(r);
    std::string b;
    bool got = id != 0 && env_.stored && env_.stored(id) && env_.read && env_.read(id, b) && !b.empty();
    if (id != 0 && !got && !asked_[id]) {
        asked_[id] = true;
        wanted_.push_back(id);
    }
    resolved_ = r.kind == IconRef::None || got;
    if (!got && !changed) return false;
    out.value = value;
    out.name = r.kind == IconRef::Name ? r.name : value;
    out.bytes = got ? std::move(b) : std::string();
    out.none = r.kind == IconRef::None;
    return true;
}

}  // namespace fugue::skin
