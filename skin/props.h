// skin/props.h - the live property overlay: what a `winset` has said about a control, on top of the .dmf.
//
// Targets and keys are matched case-insensitively and trimmed; a control is addressed by EITHER its window-qualified
// spelling ("rpane.textb") or its bare elem name ("textb"), the window-qualified one winning on read; a malformed
// pair/bool value reads as "not set" so the file's own answer survives (a bad winset never collapses a control).
// `parsePair` reads `%2c` as the comma it encodes (skin/winset.h). The store is a plain value (copyable); LiveSkin
// (skin/liveskin.h) owns the one that matters and hands the GUI a COPY, so no reader ever races a writer.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fugue::skin {

// `true`/`1`/`on`/`yes` (any case) are set; anything else, empty included, is not.
bool winsetBool(std::string_view v);

class PropStore {
public:
    void set(const std::string& target, const std::string& key, const std::string& val);
    bool get(const std::string& target, const std::string& key, std::string& out) const;
    // Every accepted set is a write, even if its text is unchanged. Copies retain these stamps; no event log.
    uint64_t revision(const std::string& target, const std::string& key) const;
    uint64_t revision() const { return revision_; }
    void erase(const std::string& target, const std::string& key);
    void eraseTarget(const std::string& target);  // every key of one target (a destroyed runtime control)
    // window-qualified spelling first, then the bare elem name.
    bool controlProp(const std::string& window, const std::string& name, const std::string& key,
                     std::string& out) const;
    bool controlBool(const std::string& window, const std::string& name, const std::string& key, bool& out) const;
    bool controlPoint(const std::string& window, const std::string& name, const std::string& key, long long& a,
                      long long& b) const;  // "A,B"
    bool controlSize(const std::string& window, const std::string& name, const std::string& key, long long& a,
                     long long& b) const;  // "AxB" or "A,B"
    bool has(const std::string& target) const;
    std::vector<std::string> targets() const;
    bool empty() const { return p_.empty(); }
    void clear() { p_.clear(); }

private:
    struct Value {
        std::string text;
        uint64_t revision;
    };
    std::map<std::string, std::map<std::string, Value>> p_;
    uint64_t revision_ = 0;  // do not reuse stamps after clear()/erase()
};

}  // namespace fugue::skin
