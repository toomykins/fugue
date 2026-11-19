// skin/winicon.h - the main window's `icon` parameter: which resource a skin or winset names as the window icon.
//
// Stock DreamSeeker 516 forms:
//   * `icon = 'red.png'` in the .dmf: the main window's icon is red.png, scaled to the icon size. DreamMaker stores the
//     file in the .rsc under exactly the quoted name, so the compiled skin keeps the quoted text verbatim.
//   * `icon = ""` (or no icon): BYOND's own atom icon. Stock's other windows keep that one even when the skin sets one.
//   * a world winset changes it live. `winset(src, "mainwindow", "icon='blue.png'")` arrives as `icon='blue.png'`;
//     `list("icon" = 'green.png')` arrives as `icon=%5b0xc000003%5d`, i.e. `[0xc000003]`: 0x0c (a resource reference)
//     and the resource's ordinal in the ResourceList. `icon=` (empty) restores BYOND's icon.
//   * a .dmi icon shows its first state's first frame.
//
// No name before the file: the ResourceList carries only [id u32][kind u8] per entry, and a file's name first appears
// in its Resource offer, with its bytes. So a skin's `icon = 'icons\\ss13_64.png'` cannot be mapped to an id, and
// cannot be moved up the download queue, until that file is offered. A warm join knows it at once, from the store. Only
// a file reference (`[0xc0000NN]`) names an id up front; IconResolver::takeWanted() hands that to
// ResourcePump::prioritize.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace fugue::skin {

struct IconRef {
    enum Kind { None, Name, Ordinal } kind = None;
    std::string name;      // Name: the file name as the .rsc stores it (quotes off)
    uint32_t ordinal = 0;  // Ordinal: index into the ResourceList
};

// Parses an `icon` value: "" / `''` -> None; `[0xc0000NN]` -> Ordinal NN; `'file.png'` or a bare `file.png` -> Name.
IconRef parseIconRef(const std::string& value);

// The main window's icon, resolved to bytes (app/sessionworker drives it; shell/skinwindow setSkinIcon shows it). Pure:
// the resource list and the store are reached through Env, so it runs without a session.
//   noteResource(): every image file this run reports, by lower-cased name with '/' and '\\' alike and by basename, so
//     `icons\\ss13_64.png` finds its bytes whichever of the skin and the file arrives first. A nameless report (a warm
//     join reports a cached file that way) is looked up with Env::nameOf.
//   update(value): call every tick with LiveSkin::mainIcon(). True when the icon should be (re)set: the value changed,
//     or the file it names has just arrived (then `bytes` holds it). Until it resolves it re-tries every call.
//   takeWanted(): an id the icon needs that is in the list but not stored yet, ONCE per id (ResourcePump::prioritize).
class IconResolver {
public:
    struct Env {
        std::function<bool(uint32_t ordinal, uint32_t& id)> idAt;   // the ResourceList; false = no such ordinal (yet)
        std::function<bool(uint32_t id)> stored;                    // stored this run (bytes readable)
        std::function<bool(uint32_t id, std::string& bytes)> read;  // the verified bytes
        std::function<std::string(uint32_t id)> nameOf;  // the store's recorded name ("" unknown); may be unset
    };
    explicit IconResolver(Env env) : env_(std::move(env)) {}
    void noteResource(uint32_t id, const std::string& name);
    struct Out {
        std::string value;  // the raw value (for the log)
        std::string name;   // a Name ref's file name, else the raw value
        std::string bytes;  // "" = no icon, or not here yet
        bool none = false;  // the value names no icon
    };
    bool update(const std::string& value, Out& out);
    std::vector<uint32_t> takeWanted() {
        std::vector<uint32_t> v;
        v.swap(wanted_);
        return v;
    }
    bool resolved() const { return resolved_; }
    // The id the current value names (0 = not known yet).
    uint32_t idFor(const IconRef& r) const;

private:
    static std::string key(const std::string& name);
    Env env_;
    std::map<std::string, uint32_t> byName_;
    std::string want_ = "\x01";  // never a real value: the first update() resolves (and announces) the icon
    bool resolved_ = true;
    std::vector<uint32_t> wanted_;
    std::map<uint32_t, bool> asked_;
};

}  // namespace fugue::skin
