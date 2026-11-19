// skin/macro.h - `.dmf` macro specs and macro sets (the keybind tables).
//
// The DM Reference "macros (skin)" entry: the macro `name` is a key combination as it would appear in the macro editor,
//   such as CTRL+E, Space+REP or Alt+Shift+F1. It is not case-specific and modifiers may go anywhere, so the spec is
//   parsed into a set rather than string-compared. A space separates tokens as `+` does. `+REP` repeats while held; `+UP` fires on release; `Any` matches any
//   key, and UP is its only documented modifier.
// `Any` fires for every key press, modifier keys included, before the key's own binding. Of the Any specs whose
//   modifiers are all held, the one with the most wins, Alt before Ctrl before Shift; Any+REP answers a press like Any
//   and never repeats. A release fires plain Any+UP whatever is held, and an Any+UP with modifiers never fires.
// Real skins bind both `F2` and `F2+REP` (baystation12), and `WEST` and `WEST+UP` (vg). So +REP and +UP are part of a
//   binding's identity, not flags on one binding.
// A key's own binding falls back to fewer modifiers in the same order; the first combination with an enabled binding
//   claims the key. Its non-REP binding answers a press (else its REP one), and the hold repeats only if that
//   combination has an enabled REP binding. Each repeat runs the first REP binding the modifiers held then reach. A
//   release looks up the +UP bindings the same way, by the modifiers held at the press.
// A modifier key (SHIFT, CTRL, ALT) matches only its bare binding, so a spec such as `CTRL+SHIFT` never fires.
// Not stated by any document: an all-modifier spec (vg's `CTRL+SHIFT`) reads its last modifier as the key; a second key
//   token is dropped; the aliases ENTER, ESC, BACKSPACE/BKSP, INS and DEL fold to RETURN, ESCAPE, BACK, INSERT and
//   DELETE; the key is upper-cased.
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "skin/dmf.h"

namespace fugue::skin {

struct MacroKey {
    std::string key;  // upper-cased canonical name: "A", "F11", "NORTH", "ANY"
    bool ctrl = false, shift = false, alt = false, up = false;
    bool operator==(const MacroKey& o) const {
        return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt && up == o.up;
    }
    bool operator<(const MacroKey& o) const;
    std::string toString() const;  // CTRL+SHIFT+ALT+KEY[+UP]
};

// Returns false when the spec names no key (empty, or only REP/UP tokens); rep is still set then.
bool parseMacroName(const std::string& spec, MacroKey& k, bool& rep);

struct MacroEntry {
    std::string id, spec, command;
    MacroKey key;
    bool rep = false, disabled = false;
    bool runtime = false;  // created by a winset `parent=<set>`
};

struct MacroSet {
    std::string name;
    std::vector<MacroEntry> entries;
    std::map<std::pair<MacroKey, bool>, size_t> byKey;  // (combination, rep) -> entries index

    const MacroEntry* lookup(const MacroKey& k, bool repeat) const;
    // The key's own binding that CLAIMS it, disabled or not (lookup() is find() minus a disabled binding).
    const MacroEntry* find(const MacroKey& k, bool repeat) const;
    // The Any binding that answers k (held modifiers in k's ctrl/shift/alt), disabled or not.
    const MacroEntry* findAny(const MacroKey& k, bool repeat) const;
    void reindex();  // rebuild byKey from entries; of two entries with one combination the later wins
};

std::vector<MacroKey> heldCombos(const MacroKey& k);
// At each combination the first of `layers` to bind it owns it, even with a disabled binding.
struct HeldMatch {
    const MacroEntry* press = nullptr;
    const MacroEntry* rep = nullptr;
};
HeldMatch matchHeld(std::initializer_list<const MacroSet*> layers, const MacroKey& k);  // the combination that claims k
const MacroEntry* lookupHeld(std::initializer_list<const MacroSet*> layers, const MacroKey& k, bool repeat);
// Stock's listing order for a set's macros (a `<set>.*` winget): by key code, then by modifiers.
bool listedBefore(const MacroEntry& a, const MacroEntry& b);

// The key name an Any macro's [[*]] carries: "NORTH" -> "North", "SHIFT" -> "Shift", "F1" and "A" unchanged.
std::string anyKeyName(const std::string& key);
// The name a winset-made macro answers: "x rep" -> "X+REP", "shift+ctrl+k" -> "CTRL+SHIFT+K", "any+up" -> "Any+UP".
std::string macroSpecName(const MacroKey& k, bool rep);

struct MacroTable {
    std::map<std::string, MacroSet> sets;  // later datum of the same name replaces the earlier
    std::string active;                    // the MAIN window's `macro`, or the sole set; "" when none
};

MacroTable resolveMacros(const File& f);
// One macro datum's entries (the rule resolveMacros applies per set: a later duplicate binding wins, in place).
MacroSet macroSetFromBlock(const Block& b);

}  // namespace fugue::skin
