#include "skin/liveskin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>

#include "skin/winset.h"

namespace fugue::skin {
namespace {

bool ieq(const std::string& a, const std::string& b) { return lowerAscii(a) == lowerAscii(b); }

std::string dims(int w, int h) { return std::to_string(w) + "x" + std::to_string(h); }

// The winset keys stored as properties (applyPropLocked); macro, focus, parent, type and splitter have their own arms.
bool storedKey(const std::string& k) {
    static const char* keys[] = {"is-visible", "is-disabled", "is-checked", "right-click", "pos", "size", "left", "right", "text",
                                 "background-color", "text-color", "on-close", "title", "use-title", "style", "command",
                                 "icon-size", "zoom", "zoom-mode", "letterbox", "icon", "font-family", "font-size", "font-style",
                                 "tab-background-color", "tab-text-color", "prefix-color", "suffix-color"};
    for (const char* s : keys) if (k == s) return true;
    return false;
}

// What stock stores for a winset value that is empty (`pos`, `pos=` and `pos=""` alike): the property's own reading of "".
// MEASURED on DreamSeeker 516.1684 (tests/data/winsetempty-world); the two colours read back as #b47229 and paint that.
// A window or pane's pos and size are left as sent: stock answers them oddly (-1,-1 and 6x-1) and nothing here reads them.
const char* valueOfEmpty(const std::string& k, bool control) {
    if (k == "is-visible" || k == "is-disabled" || k == "is-checked") return "false";
    if (k == "size" && control) return "0x0";   // fill to the parent's far edge
    if (k == "pos" && control) return "-1,-1";
    if (k == "font-size" || k == "zoom") return "0";
    if (k == "background-color" || k == "text-color") return "#b47229";
    return "";
}

double clampFrac(double f) { return f < 0 ? 0 : (f > 1 ? 1 : f); }

std::string boolParam(bool b) { return b ? "true" : "false"; }

bool macroParam(const MacroEntry& e, const std::string& k, std::string& v) {
    if (k == "id") v = e.id;
    else if (k == "name") v = e.spec;
    else if (k == "command") v = e.command;
    else if (k == "is-disabled") v = boolParam(e.disabled);
    else return false;
    return true;
}

// winget `splitter` text. Stock keeps the value as a 32-bit float: a whole value prints bare ("50", "-10"), anything
// else with %f ("33.299999" after `splitter=33.3`).
std::string splitterText(float f) {
    char buf[64];
    if (f == std::floor(f) && std::fabs(f) < 1e15f) std::snprintf(buf, sizeof buf, "%lld", (long long)f);
    else std::snprintf(buf, sizeof buf, "%f", double(f));
    return buf;
}

// The global ("special") wingets: winget(client, null or "", name). Each answer is what stock DreamSeeker 516 returns.
//   focus            the "window.control" id that has the keyboard, or the main window's name when none has it.
//   windows          the non-pane windows in file order, hidden ones included, then the browse() popups.
//   panes            the is-pane windows in file order.
//   menu, macro      the menus and macro sets built so far. A set is built when a window names it, and kept after.
//   sound, hwmode    "true". num-lock, caps-lock, scroll-lock "false". dpi "1".
//   url              the address DreamSeeker dialed (as dialed, not the server's own).
//   screen-pos, screen-size   the monitor's work area.
//   browser-options  "" until a winset(null, "browser-options=...") sets devtools or find (applyPairLocked).
// Anything else (`nonesuch`, `id`, `type`, `size`, `pos`, `is-visible`, `*`, `mainwindow.size`) answers "", so a
// wildcard never enumerates this table, unless stockListTest turns the name into a list. The environment group is
// published by setClientEnv (the GUI's dpi and screen size, the session's url); the stock value is the default until
// it is set. `focus` likewise follows the GUI once it reports (setGuiFocus).
enum class Global { Focus, Windows, Panes, Menus, Macros, Env, BrowserOptions };
struct GlobalParam { const char* name; Global what; const char* envDefault; };
const GlobalParam kGlobalParams[] = {
    {"focus", Global::Focus, ""},
    {"browser-options", Global::BrowserOptions, ""},
    {"sound", Global::Env, "true"},     {"hwmode", Global::Env, "true"},   // the client draws every blend mode, so never software mode
    {"url", Global::Env, ""},
    {"num-lock", Global::Env, "false"}, {"caps-lock", Global::Env, "false"}, {"scroll-lock", Global::Env, "false"},
    {"dpi", Global::Env, "1"},
    {"screen-pos", Global::Env, ""},    {"screen-size", Global::Env, ""},
};

const GlobalParam* globalParam(const std::string& name) {
    for (const GlobalParam& g : kGlobalParams) if (name == g.name) return &g;
    return nullptr;
}

// Stock DreamSeeker 516 matches the window, pane, menu and macro lists without a clean name test: for each list name T
// it accepts T, or T plus "s", and the plural half does not check the prefix (so `panes` is the pane list). It also
// reads the two bytes past a short name's terminator from the block the name was built in, so an answer can depend on
// names asked before it. This reproduces that.
std::optional<Global> stockListTest(const std::string& n, const char (&block)[8]) {
    auto at = [&](size_t i) { return n.size() <= 7 ? block[i] : n[i]; };   // i <= 7 < n.size() for a long name
    struct Test { const char* t; size_t len; Global what; };
    static const Test tests[] = {{"window", 6, Global::Windows}, {"pane", 4, Global::Panes},
                                 {"menu", 4, Global::Menus},     {"macro", 5, Global::Macros}};
    for (const Test& t : tests)
        if ((n.compare(0, t.len, t.t, t.len) == 0 && at(t.len) == '\0') || (at(t.len) == 's' && at(t.len + 1) == '\0'))
            return t.what;
    return std::nullopt;
}

// Model of the block stock DreamSeeker builds each global-winget name in. Names of up to 15 letters share one block:
// the name's text and NUL overwrite its first bytes, and the previous occupant's tail survives past a short name's
// terminator (stockListTest reads it). Longer names live elsewhere. This is not every write path's rule.
void nameBlockWrite(char (&block)[8], const std::string& s) {
    if (s.size() <= 15) std::memcpy(block, s.c_str(), std::min<size_t>(s.size() + 1, sizeof block));
}
void nameBlockClobber(char (&block)[8]) { block[6] = block[7] = '\x01'; }   // neither 's' nor NUL

// Whether a control's `pos` / `size` is the same in any box (skin/layout.h anchor rules): pos when anchor1 is none or
// 0,0; size when the .dmf size is non-zero and the anchors do not stretch it.
bool boxFreeGeometry(const Elem* e, const std::string& param) {
    if (!e) return false;
    int64_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    bool a1 = e->point("anchor1", x1, y1), a2 = e->point("anchor2", x2, y2);
    if (param == "pos") return !a1 || (x1 == 0 && y1 == 0);
    int64_t w = 0, h = 0;
    if (!e->size("size", w, h) || w <= 0 || h <= 0) return false;
    return !a1 || !a2 || (x1 == x2 && y1 == y2);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// lookups (all caller-holds-mu_)
// ---------------------------------------------------------------------------------------------------------------

const Block* LiveSkin::windowLocked(const std::string& name) const {
    if (!file_ || name.empty()) return nullptr;
    const Block* hit = nullptr;
    for (const Block& b : file_->blocks)
        if (b.kind == "window" && ieq(b.name, name)) hit = &b;
    return hit;
}

// declaresLocked: a control the skin DECLARES, placed in the current layout or not.
const Elem* LiveSkin::declaresLocked(const std::string& name) const {
    if (!file_ || name.empty()) return nullptr;
    size_t dot = name.find('.');
    bool qualified = dot != std::string::npos;
    std::string win = qualified ? name.substr(0, dot) : "", shortName = qualified ? name.substr(dot + 1) : name;
    for (const Block& b : file_->blocks) {
        if (b.kind != "window") continue;
        if (qualified && !ieq(b.name, win)) continue;
        for (const Elem* e : b.controls())
            if (ieq(e->name, qualified ? shortName : name)) return e;
    }
    return nullptr;
}

std::string LiveSkin::controlTypeLocked(const std::string& target) const {
    if (const Control* c = tree_.byName(target)) return c->type;
    if (const Elem* e = declaresLocked(target)) return e->type();
    return "";
}

// A menu elem's bare name for `name`, or "" when it is not one. The datum-qualified spelling (`menu.textmode`) is
// accepted too.
std::string LiveSkin::menuElemLocked(const std::string& name) const {
    if (!file_ || name.empty()) return "";
    Menu m = menuFromFile(*file_);
    std::string bare = name;
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        if (!ieq(name.substr(0, dot), m.datum)) return "";
        bare = name.substr(dot + 1);
    }
    for (const MenuCat& c : m.cats) {
        if (!c.name.empty() && ieq(c.name, bare)) return c.name;
        for (const MenuItem& it : c.items)
            if (!it.name.empty() && ieq(it.name, bare)) return it.name;
    }
    return "";
}

bool LiveSkin::isMenuTarget(const std::string& name) const { return !menuElemLocked(name).empty(); }

LiveSkin::Kind LiveSkin::resolveLocked(const std::string& name) const {
    if (name.empty()) return Kind::Unknown;
    if (tree_.byName(name)) return Kind::Control;
    // A control the skin declares but the layout does not place is still a control.
    if (declaresLocked(name)) return Kind::Control;
    if (knownPopupLocked(name)) return Kind::Popup;
    if (windowLocked(name)) return Kind::Window;   // (E2) a skin window: macro / title / focus targets
    if (isMenuTarget(name)) return Kind::Menu;
    if (runtimeLocked(name)) return Kind::Runtime;
    size_t i;
    if (macroElemLocked(name, i)) return Kind::Macro;
    return Kind::Unknown;
}

// Which target kinds consume which winset key: stock DreamSeeker's matrix, plus the Window kind (a skin window's
// title, icon and pos).
bool LiveSkin::hasConsumerLocked(Kind kind, const WinsetPair& p) const {
    const std::string& k = p.key;
    if (k == "is-visible" || k == "pos" || k == "size") {
        if (kind == Kind::Control || kind == Kind::Popup || kind == Kind::Runtime) return true;
        // A skin window's `pos` is consumed by winget only. The main window's is its real screen place, which the GUI
        // publishes, so a winset of it is not stored.
        if (k == "pos" && kind == Kind::Window) return file_ && windowLocked(p.target) != findMainWindow(*file_);
        return false;
    }
    // A runtime control (runtime_): winget is the consumer.
    if (kind == Kind::Runtime) return k == "is-disabled" || k == "text";
    if (k == "is-disabled") return kind == Kind::Control || kind == Kind::Menu;
    // A menu item's is-checked is flipped by a click before its command runs, and the command reads it back in a
    // condition. Written by shell/skinwindow, consumed by menuLocked and winget.
    if (k == "is-checked") return (kind == Kind::Control && controlTypeLocked(p.target) == "BUTTON") || kind == Kind::Menu;
    // A MAP's scaling properties. Consumed by shell/skinwindow (MapWidget::setMapScale).
    if (k == "icon-size" || k == "zoom" || k == "zoom-mode" || k == "letterbox")
        return kind == Kind::Control && controlTypeLocked(p.target) == "MAP";
    if (k == "left" || k == "right") return kind == Kind::Control && controlTypeLocked(p.target) == "CHILD";
    if (k == "on-close" || k == "use-title") return kind == Kind::Popup;
    if (k == "title") return kind == Kind::Popup || kind == Kind::Window;
    // A window's `icon`, consumed by mainIcon() (skin/winicon.h) and by shell/skinwindow's setSkinIcon.
    if (k == "icon") return kind == Kind::Window;
    if (k == "background-color") return kind == Kind::Control || kind == Kind::Window;   // a window's fills its box (shell/skinwindow)
    if (k == "style") return kind == Kind::Control;
    if (k == "text-color") {
        std::string t = controlTypeLocked(p.target);
        // A MAP's is maptext's default colour (defaultMapStyle).
        return kind == Kind::Control && (t == "OUTPUT" || t == "BROWSER" || t == "INFO" || t == "INPUT" || t == "LABEL" || t == "MAP" || t == "BUTTON");
    }
    // An INFO's tab strip and stat rows (a Stat() row's name is the prefix, its value the suffix).
    if (k == "tab-background-color" || k == "tab-text-color" || k == "prefix-color" || k == "suffix-color")
        return kind == Kind::Control && controlTypeLocked(p.target) == "INFO";
    if (k == "text") {
        if (kind != Kind::Control) return false;
        std::string t = controlTypeLocked(p.target);
        return t == "BUTTON" || t == "LABEL" || t == "INPUT";
    }
    // A control's font, consumed by shell/skinwindow (shell/skinfont.h).
    if (k == "font-family" || k == "font-size" || k == "font-style") {
        std::string t = controlTypeLocked(p.target);
        return kind == Kind::Control && (t == "BUTTON" || t == "LABEL" || t == "INPUT" || t == "INFO");
    }
    if (k == "right-click") return kind == Kind::Control && controlTypeLocked(p.target) == "MAP";   // consumed by shell/skinwindow (map)
    // An INPUT's `command`: the bar's default command, shown as its sticky text (`!say "`). Consumed by shell/skinwindow.
    if (k == "command") return kind == Kind::Control && controlTypeLocked(p.target) == "INPUT";
    return false;
}

void LiveSkin::uncheckSiblingsLocked(const std::string& target) {
    if (!file_) return;
    // Unchecks every other button of the target's group (from the file) in the overlay.
    std::string group, selfWin, selfName;
    for (const Block& b : file_->blocks) {
        if (b.kind != "window") continue;
        for (const Elem* e : b.controls()) {
            if (e->type() != "BUTTON") continue;
            bool match = ieq(b.name + "." + e->name, target) || ieq(e->name, target);
            if (match && group.empty()) { group = e->str("group", ""); selfWin = b.name; selfName = e->name; }
        }
    }
    if (group.empty()) return;
    for (const Block& b : file_->blocks) {
        if (b.kind != "window") continue;
        for (const Elem* e : b.controls()) {
            if (e->type() != "BUTTON" || e->str("group", "") != group) continue;
            if (b.name == selfWin && e->name == selfName) continue;
            props_.set(b.name + "." + e->name, "is-checked", "false");
        }
    }
}

// applyProp. true = stored (a consumer exists), false = a gap.
bool LiveSkin::applyPropLocked(const WinsetPair& p) {
    Kind kind = resolveLocked(p.target);
    if (!hasConsumerLocked(kind, p)) return false;
    std::string target = p.target;
    bool mapKey = p.key == "icon-size" || p.key == "zoom" || p.key == "zoom-mode" || p.key == "letterbox";
    bool inputText = p.key == "text" && kind == Kind::Control && controlTypeLocked(p.target) == "INPUT";
    if (p.key == "left" || p.key == "right" || p.key == "is-checked" || p.key == "command" || mapKey || inputText) {
        // One canonical spelling for the keys layoutChild reads, so the freshest write wins whatever spelling it used.
        // is-checked and command are written by a click with the window-qualified name and by a bare winset alike, and
        // an INPUT's text by the player's typing (setInputText) and a winset alike.
        if (const Control* c = tree_.byName(p.target)) target = c->window + "." + c->name;
    }
    if (kind == Kind::Menu) target = menuElemLocked(p.target);   // one spelling: the bare elem name
    if (kind == Kind::Runtime) target = runtimeLocked(p.target)->name;
    if (kind == Kind::Window) target = windowLocked(p.target)->name;
    // A world-set place answers verbatim until the GUI reports where the window really went (setWindowPos).
    if (p.key == "pos" && (kind == Kind::Window || kind == Kind::Popup)) screenPos_.erase(lowerAscii(target));
    props_.set(target, p.key, p.value.empty() ? valueOfEmpty(p.key, kind == Kind::Control) : p.value);
    // zoom and icon-size are one property: setting one clears the other, or a later icon-size=64 would lose to an
    // earlier zoom=2 in shell/skinwindow's read.
    if (p.key == "zoom" || p.key == "icon-size") {
        const char* other = p.key == "zoom" ? "icon-size" : "zoom";
        props_.erase(target, other);
        if (const Control* c = tree_.byName(p.target)) props_.erase(c->name, other);
    }
    if (kind == Kind::Control && p.key == "is-checked" && winsetBool(p.value)) uncheckSiblingsLocked(p.target);
    if (kind == Kind::Menu && p.key == "is-checked" && winsetBool(p.value)) uncheckMenuSiblingsLocked(target);
    return true;
}

// Only an INPUT or a MAP can hold focus here, placed or not.
bool LiveSkin::setFocusLocked(const std::string& target, bool on) {
    const Control* c = tree_.byName(target);
    const Elem* e = c ? nullptr : declaresLocked(target);
    std::string type = c ? c->type : e ? e->type() : "";
    if (type != "INPUT" && type != "MAP") return false;
    if (on) { focus_ = target; focusSeq_++; }
    else if (focus_ == target) focus_.clear();
    guiFocusSet_ = false;   // the world's choice answers until the GUI reports where the keyboard really went
    return true;
}

void LiveSkin::relayoutLocked() {
    if (!file_) return;
    Rect cl = client_;
    if (!clientSet_) {
        int w = 640, h = 480;
        if (const Block* mw = findMainWindow(*file_))
            if (const Elem* m = mw->main()) {
                int64_t dw, dh;
                if (m->size("size", dw, dh) && dw > 0 && dh > 0) { w = int(dw); h = int(dh); }
            }
        cl = Rect{0, 0, w, h};
    }
    tree_ = layoutSkin(*file_, cl, splits_, &props_, /*inferSlots=*/false);   // stock's empty sides (setPaneShown)
    gen_++;
}

void LiveSkin::applyPairLocked(const WinsetPair& p, WinsetResult& r) {
    bool changed = false;
    // A client-as-a-whole parameter (winset(src, null, ...)): no `window.elem.` path, so not a WinsetPair target.
    if (!p.ok && lowerAscii(trimSpace(p.raw.substr(0, p.raw.find('=')))) == "browser-options") {
        // Stock DreamSeeker 516 reads browser-options as a comma-separated list: find = 2, devtools = 1, byondstorage = 4,
        // zoom = 8, refresh = 16. `+x` sets a bit, `-x` clears it, and an unsigned first item replaces the whole set.
        // Unknown items are ignored.
        static const std::pair<const char*, unsigned> bits[] = {{"find", 2}, {"devtools", 1}, {"byondstorage", 4},
                                                                {"zoom", 8}, {"refresh", 16}};
        bool first = true;
        size_t i = 0;
        const std::string& v = p.value;
        do {
            size_t j = v.find(',', i);
            std::string item = trimSpace(v.substr(i, j == std::string::npos ? std::string::npos : j - i));
            char sign = !item.empty() && (item[0] == '+' || item[0] == '-') ? item[0] : 0;
            if (first && !sign) browserOptions_ = 0;
            first = false;
            if (sign) item.erase(0, 1);
            for (const auto& b : bits)
                if (ieq(item, b.first)) browserOptions_ = sign == '-' ? browserOptions_ & ~b.second : browserOptions_ | b.second;
            i = j == std::string::npos ? v.size() + 1 : j + 1;
        } while (i <= v.size());
        // Still counted unhandled: only `find` reaches the webview (browserOptions(), shell/browserpane.h). The other
        // bits are remembered for winget and change nothing.
        r.unhandled.push_back(p.raw);
        return;
    }
    if (!p.ok) { r.unhandled.push_back(p.raw); return; }
    if ((p.key == "name" || p.key == "command" || p.key == "is-disabled" || p.key == "parent") && resolveLocked(p.target) == Kind::Macro) {
        if (applyMacroPairLocked(p)) { r.applied.push_back(p.target + "." + p.key); changed = true; }
        else r.unhandled.push_back(p.raw);
    } else if (storedKey(p.key)) {
        if (applyPropLocked(p)) { r.applied.push_back(p.target + "." + p.key); changed = true; }
        else r.unhandled.push_back(p.raw);
    } else if (p.key == "macro") {
        // The set is named on a WINDOW, so the target is not resolved; an unknown SET is refused (the
        // difference between "switch the keyboard" and "blank the keyboard").
        if (macros_.sets.count(p.value)) { macro_ = p.value; useMacroLocked(p.value); r.applied.push_back(p.target + ".macro"); changed = true; }
        else r.unhandled.push_back(p.raw);
    } else if (p.key == "focus") {
        if (setFocusLocked(p.target, winsetBool(p.value))) { r.applied.push_back(p.target + ".focus"); changed = true; }
        else r.unhandled.push_back(p.raw);
    } else if (p.key == "parent") {
        if (applyParentLocked(p)) { r.applied.push_back(p.target + ".parent"); changed = true; }
        else r.unhandled.push_back(p.raw);
    } else if (p.key == "type" && runtimeLocked(p.target)) {
        // The type of a control `parent=` just created. Stock answers it upper-cased (type=label answers "LABEL").
        for (RuntimeCtl& rc : runtime_) if (ieq(rc.name, runtimeLocked(p.target)->name)) rc.type = upperAscii(p.value);
        r.applied.push_back(p.target + ".type");
        changed = true;
    } else if (p.key == "splitter") {
        const Control* c = tree_.byName(p.target);
        char* end = nullptr;
        double pct = std::strtod(p.value.c_str(), &end);
        // Refused here, though stock DreamSeeker 516 takes a non-number as 0.
        if (!c || c->type != "CHILD" || p.value.empty() || *end) { r.unhandled.push_back(p.raw); return; }
        std::string path = c->window + "." + c->name;
        setSplitPctLocked(path, pct);
        r.applied.push_back(path);
        changed = true;
    } else {
        r.unhandled.push_back(p.raw);
    }
    // A pair can change the tree the NEXT pair resolves against (`left=` swaps a pane in, then a control inside
    // it is addressed): relayout between pairs, not only at the end.
    if (changed) relayoutLocked();
}

// The value is stored verbatim (winget answers 120 after `splitter=120`); only the layout clamps it.
void LiveSkin::setSplitPctLocked(const std::string& path, double pct) {
    splitPct_[path] = float(pct);
    splits_[path] = clampFrac(double(float(pct)) / 100);
}

bool LiveSkin::setSplitter(const std::string& path, double pct) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!file_) return false;
        bool known = false;
        for (const Splitter& sp : tree_.splitters) known = known || sp.path == path;
        if (!known) return false;
        setSplitPctLocked(path, pct);
        relayoutLocked();
    }
    notify();
    return true;
}

void LiveSkin::accountLocked(const std::string& control, const WinsetResult& r) {
    stats_.applied += r.applied.size();
    for (const std::string& u : r.unhandled) {
        stats_.unhandled.push_back(control + ": " + u);
        if (stats_.unhandled.size() > 256) stats_.unhandled.erase(stats_.unhandled.begin());
    }
}

WinsetResult LiveSkin::winsetLocked(const std::string& control, const std::string& props, bool urlEncoded) {
    WinsetResult r;
    // `parent=` creates the control the rest of the same winset configures, wherever it sits in the list (vg's tooltips
    // send type, size and pos before parent). So the parent pairs are applied first.
    std::vector<WinsetPair> pairs = splitWinset(control, props, urlEncoded);
    auto isParent = [](const WinsetPair& p) { return p.ok && p.key == "parent"; };
    std::vector<bool> consumed(pairs.size());
    for (size_t i = 0; i < pairs.size(); i++)
        if (isParent(pairs[i]) && !createMacroLocked(pairs, i, consumed, r)) applyPairLocked(pairs[i], r);
    for (size_t i = 0; i < pairs.size(); i++)
        if (!isParent(pairs[i]) && !consumed[i]) applyPairLocked(pairs[i], r);
    accountLocked(control, r);
    return r;
}

// A condition is tested against what a winget would answer NOW (each clause sees the clauses before it applied).
// A BUTTON nobody has checked answers no is-checked at all; DreamSeeker's default for it is false. Booleans compare
// as booleans (`true` = `TRUE` = `1`), anything else case-insensitively.
bool LiveSkin::conditionLocked(const WinsetPair& c) const {
    std::string have;
    if (c.key == "macro") have = macro_;   // the ACTIVE set, not the file's
    else if (!wingetLocked(c.target, c.key, have) && c.key == "is-checked") have = "false";
    auto isBoolWord = [](const std::string& v) {
        std::string l = lowerAscii(trimSpace(v));
        return l == "true" || l == "false" || l == "on" || l == "off" || l == "1" || l == "0";
    };
    if (isBoolWord(have) && isBoolWord(c.value)) return winsetBool(have) == winsetBool(c.value);
    return ieq(trimSpace(have), trimSpace(c.value));
}

WinsetResult LiveSkin::clientWinset(const std::string& body) {
    if (!hasConditional(body)) return winset("", body);   // the plain form takes the ordinary winset path
    WinsetResult r;
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!file_) return r;   // a button or macro cannot fire before there is a skin
        r = clientWinsetLocked(body);
        if (r.applied.empty()) return r;
    }
    notify();
    return r;
}

WinsetResult LiveSkin::clientWinsetLocked(const std::string& body) {
    if (!hasConditional(body)) return winsetLocked("", body);
    WinsetResult r;
    for (const WinsetClause& cl : parseClientWinset(body)) {
        bool pick = !cl.conditional || (conditionLocked(cl.cond) != cl.negate);
        for (const WinsetPair& p : pick ? cl.then : cl.otherwise) applyPairLocked(p, r);
    }
    accountLocked("", r);
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// winget
// ---------------------------------------------------------------------------------------------------------------

bool LiveSkin::wingetLocked(const std::string& id, const std::string& param, std::string& out) const {
    const Control* c = tree_.byName(id);
    if (!c) {
        if (const RuntimeCtl* rc = runtimeLocked(id)) return runtimeWingetLocked(*rc, param, out);
        // A popup's / the main window's real place on the screen (setWindowPos) beats what the world last asked for.
        if (param == "pos" || param == "outer-pos") {
            const Block* w = windowLocked(id);
            bool isMain = w && file_ && findMainWindow(*file_) == w;
            if (isMain || (!w && knownPopupLocked(id))) {
                auto sp = screenPos_.find(isMain ? std::string() : lowerAscii(id));
                if (sp != screenPos_.end()) { out = std::to_string(sp->second.first) + "," + std::to_string(sp->second.second); return true; }
                if (isMain) return false;   // never shown yet: no plausible guess
            }
            if (param == "outer-pos") return wingetLocked(id, "pos", out);   // outer-pos equals pos
        }
        // A target with no placed control may still be one the world configured (a browse popup, a runtime window).
        if (props_.get(id, param, out)) return true;
        // (E1) a control the skin declares but the layout does not place.
        if (const Elem* e = declaresLocked(id)) {
            if (param == "type") { out = e->type(); return true; }
            if (param == "id") { out = e->name; return true; }
            std::string win;   // find its window for the overlay's qualified spelling
            for (const Block& b : file_->blocks)
                for (const Elem* x : b.controls()) if (x == e) win = b.name;
            if (props_.controlProp(win, e->name, param, out)) return true;
            if (param == "is-visible") { out = boolParam(e->boolv("is-visible", true)); return true; }
            if (param == "is-disabled") { out = boolParam(e->boolv("is-disabled", false)); return true; }
            // pos and size come from the file only when no box would change them (boxFreeGeometry); otherwise the
            // answer is refused rather than guessed.
            if ((param == "pos" || param == "size") && !boxFreeGeometry(e, param)) return false;
            if (const std::string* v = e->get(param)) { out = *v; return true; }
            return false;
        }
        // A menu item: what a click or a winset wrote, else the file's is-checked / is-disabled.
        if (std::string mn = menuElemLocked(id); !mn.empty()) {
            if (param == "id") { out = mn; return true; }
            if (props_.get(mn, param, out)) return true;
            Menu m = menuFromFile(*file_);
            for (const MenuCat& c : m.cats)
                for (const MenuItem& it : c.items)
                    if (it.name == mn && (param == "is-checked" || param == "is-disabled")) {
                        out = boolParam(param == "is-checked" ? it.checked : it.disabled);
                        return true;
                    }
            return false;
        }
        size_t mi;
        if (const MacroSet* ms = macroElemLocked(id, mi)) {
            if (param == "type") { out = "MACRO"; return true; }
            return macroParam(ms->entries[mi], param, out);
        }
        // (E2) a skin window.
        if (const Block* w = windowLocked(id)) {
            bool isMain = file_ && findMainWindow(*file_) == w;
            const Elem* m = w->main();
            if (props_.get(w->name, param, out)) return true;
            if (param == "type") { out = "MAIN"; return true; }
            if (param == "id") { out = w->name; return true; }
            if (param == "dpi") { out = envLocked("dpi"); return true; }   // every window answers it
            if (isMain && (param == "size" || param == "inner-size")) { out = dims(tree_.client.dx(), tree_.client.dy()); return true; }
            if (isMain && param == "macro") { out = macro_; return true; }
            if (isMain && param == "is-visible") { out = "true"; return true; }
            // pos: a pane's is 0,0. Another window's is its .dmf pos while it is unshown. The main window's is its place on
            // the SCREEN, a GUI fact this object does not hold, so it stays refused.
            if (param == "pos") {
                if (isMain) return false;
                if (m && m->boolv("is-pane", false)) { out = "0,0"; return true; }
            }
            // A pane's size is the box it is hosted in; an unshown pane's is its .dmf size.
            if (param == "size" && m && m->boolv("is-pane", false))
                for (const Control& hc : tree_.controls) {
                    if (hc.window != w->name || hc.box.empty()) continue;
                    out = dims(hc.box.dx(), hc.box.dy());
                    return true;
                }
            if (m) if (const std::string* v = m->get(param)) { out = *v; return true; }
            return false;
        }
        // A browse() popup is a window (see setPopups): visible while the model has it open and the player has not
        // closed it.
        if (knownPopupLocked(id)) {
            if (param == "type") { out = "MAIN"; return true; }
            if (param == "dpi") { out = envLocked("dpi"); return true; }
            if (param == "is-visible") {
                bool open = false;
                for (const std::string& p : popups_) open = open || ieq(p, id);
                bool hidden = false;
                for (const std::string& p : hiddenPopups_) hidden = hidden || ieq(p, id);
                out = boolParam(open && !hidden);
                return true;
            }
        }
        return false;
    }
    // First, what the laid-out tree says: a stored intent never beats the layout.
    if (param == "size") { out = dims(c->full.dx(), c->full.dy()); return true; }   // not what the pane clips it to
    if (param == "type") { out = c->type; return true; }
    if (param == "is-visible") { out = boolParam(c->visible); return true; }
    if (param == "is-disabled") { out = boolParam(c->disabled); return true; }
    if (param == "is-default") { out = boolParam(c->isDefault); return true; }
    if (param == "id") { out = c->name; return true; }   // the bare elem name ("map" for mapwindow.map)
    // A CHILD's splitter: what a winset or a drag stored, else the file's value, else stock's default of 50.
    if (param == "splitter" && c->type == "CHILD") {
        auto sp = splitPct_.find(c->window + "." + c->name);
        if (sp != splitPct_.end()) { out = splitterText(sp->second); return true; }
        if (c->elem) { out = splitterText(float(c->elem->floatv("splitter", kDefaultSplitter))); return true; }
    }
    // Second, what the world itself set.
    if (props_.controlProp(c->window, c->name, param, out)) return true;
    // pos is pane-local: the laid-out corner relative to the box its window was laid out in. A stored winset value
    // answers verbatim; an empty (off-pane) rect falls through to the file.
    if (param == "pos" && !c->rect.empty()) { out = std::to_string(c->full.x0 - c->box.x0) + "," + std::to_string(c->full.y0 - c->box.y0); return true; }
    // (E3) THIRD, what the .dmf states for this control.
    if (c->elem) if (const std::string* v = c->elem->get(param)) { out = *v; return true; }
    return false;
}

bool LiveSkin::winget(const std::string& control, const std::string& param, std::string& out) const {
    std::lock_guard<std::mutex> g(mu_);
    if (!file_) return false;
    return wingetLocked(control, param, out);
}

std::string LiveSkin::wingetParams(const std::string& control, const std::string& params) const {
    std::lock_guard<std::mutex> g(mu_);
    nameBlockWriteControlLocked(params);
    std::string out = file_ ? wingetAnswer(params, [&](const std::string& k, std::string& v) {
        return controlFocusLocked(control, k, v) || wingetLocked(control, k, v) || buttonUncheckedLocked(control, k, v);
    }) : "";
    // A lone property's answer is written into the name block (nameBlockWrite); a multi-property answer is not.
    if (wingetParamNames(params).size() == 1) nameBlockWrite(nameBlock_, out);
    return out;
}

// From the FILE, not the laid-out tree: a headless session (no client size, no layout) measures text too.
std::string LiveSkin::defaultMapIdLocked() const {
    if (!file_) return "";
    std::vector<std::string> maps;
    for (const Block& b : file_->blocks)
        if (b.kind == "window")
            for (const Elem* e : b.controls())
                if (e->type() == "MAP") maps.push_back(b.name + "." + e->name);
    if (maps.empty()) return "";
    std::string v;
    for (const std::string& id : maps)
        if (wingetLocked(id, "is-default", v) && v == "true") return id;
    return maps.front();
}

std::string LiveSkin::defaultMapId() const {
    std::lock_guard<std::mutex> g(mu_);
    return defaultMapIdLocked();
}

std::vector<std::string> LiveSkin::secondaryMaps() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out;
    if (file_) {
        const std::string def = defaultMapIdLocked();
        for (const Block& b : file_->blocks)
            if (b.kind == "window")
                for (const Elem* e : b.controls())
                    if (e->type() == "MAP" && b.name + "." + e->name != def) out.push_back(e->name);
    }
    for (const RuntimeCtl& r : runtime_) if (r.type == "MAP") out.push_back(r.name);
    return out;
}

std::string LiveSkin::defaultMapStyle() const {
    std::lock_guard<std::mutex> g(mu_);
    std::string pick = defaultMapIdLocked();
    if (pick.empty()) return "";
    std::string out, color;
    wingetLocked(pick, "style", out);
    // The control's text-color is maptext's default colour; a `body` rule in its style still beats it.
    if (wingetLocked(pick, "text-color", color) && !color.empty() && lowerAscii(color) != "none")
        out = "body{color:" + color + "}" + out;
    return out;
}

// The skin first: a browse() into a name the skin declares lands in that control and answers its own type. Only then a
// popup, which is a window: "MAIN" (see setPopups).
std::string LiveSkin::wingetType(const std::string& control) const {
    std::lock_guard<std::mutex> g(mu_);
    nameBlockClobber(nameBlock_);   // winexists() changes the global-winget name block
    return typeLocked(control);
}

bool LiveSkin::printsOutput(const std::string& control) const {
    std::lock_guard<std::mutex> g(mu_);
    if (!file_) return true;
    std::string t = typeLocked(control);
    return t == "OUTPUT";
}

std::string LiveSkin::typeLocked(const std::string& control) const {
    if (file_) {
        if (const Control* c = tree_.byName(control)) return c->type;
        if (const Elem* e = declaresLocked(control)) return e->type();
        if (windowLocked(control)) return "MAIN";
        if (const RuntimeCtl* rc = runtimeLocked(control)) return rc->type;   // winexists("rtlab") -> "LABEL"
        size_t mi;
        if (macroElemLocked(control, mi) || builtMacroSetLocked(control)) return "MACRO";
    }
    if (knownPopupLocked(control)) return "MAIN";
    return "";
}

const LiveSkin::RuntimeCtl* LiveSkin::runtimeLocked(const std::string& name) const {
    if (name.empty()) return nullptr;
    for (const RuntimeCtl& r : runtime_)
        if (ieq(r.name, name) || ieq(r.parent + "." + r.name, name)) return &r;
    return nullptr;
}

// `parent=<window>` on a name the skin does not declare creates (or re-parents) a runtime control; `parent=none`,
// `parent=` or `parent=null` destroys one (see runtime_). Re-parenting a file control is not modelled and is refused.
bool LiveSkin::applyParentLocked(const WinsetPair& p) {
    if (!file_ || p.target.empty() || p.target.find_first_of("*?") != std::string::npos) return false;
    std::string v = lowerAscii(p.value);
    bool destroy = v.empty() || v == "none" || v == "null";
    if (const RuntimeCtl* rc = runtimeLocked(p.target)) {
        std::string name = rc->name;
        if (destroy) {
            runtime_.erase(runtime_.begin() + (rc - runtime_.data()));
            props_.eraseTarget(name);
            return true;
        }
        std::string parent = parentWindowLocked(p.value);
        if (parent.empty()) return false;
        for (RuntimeCtl& r : runtime_) if (r.name == name) r.parent = parent;
        return true;
    }
    if (destroy && destroyCloneLocked(p.target)) return true;
    if (destroy || tree_.byName(p.target) || declaresLocked(p.target) || windowLocked(p.target) || knownPopupLocked(p.target)) return false;
    std::string parent = parentWindowLocked(p.value);
    if (parent.empty()) return false;
    std::string name = p.target;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) name = name.substr(dot + 1);
    props_.eraseTarget(name);
    runtime_.push_back({name, parent, ""});
    return true;
}

// What a `parent=` value names: a skin window (its block name), else a browse() popup (the name as given; see runtime_),
// else "" (refused).
std::string LiveSkin::parentWindowLocked(const std::string& v) const {
    if (const Block* w = windowLocked(v)) return w->name;
    if (knownPopupLocked(v)) return v;
    return {};
}

bool LiveSkin::runtimeWingetLocked(const RuntimeCtl& r, const std::string& param, std::string& out) const {
    if (param == "id") { out = r.name; return true; }
    if (param == "type") { out = r.type; return true; }
    if (param == "parent") { out = r.parent; return true; }
    if (param == "runtime") { out = "true"; return true; }
    if (props_.get(r.name, param, out)) {
        if (param == "is-visible" || param == "is-disabled") out = boolParam(winsetBool(out));   // is-visible=0 answers "false"
        return true;
    }
    if (param == "is-visible") { out = "true"; return true; }   // created without is-visible
    if (param == "is-disabled") { out = "false"; return true; }
    return false;
}

void LiveSkin::setWindowPos(const std::string& window, int x, int y) {
    std::lock_guard<std::mutex> g(mu_);
    screenPos_[lowerAscii(window)] = {x, y};
}

void LiveSkin::setPaneShown(const std::string& window) {
    {
        std::lock_guard<std::mutex> g(mu_);
        const Block* b = file_ ? windowLocked(window) : nullptr;
        if (!b || !shownPanes_.insert(lowerAscii(b->name)).second) return;
        for (const Control& c : tree_.controls) if (c.window == b->name) return;   // already placed: its on-show is not run here
        WinsetResult r;
        for (const Elem& e : b->elems) {
            const std::string* os = e.get("on-show");
            std::string body;
            if (os && winsetBody(*os, body) && !body.empty()) {
                WinsetResult one = clientWinsetLocked(body);
                r.applied.insert(r.applied.end(), one.applied.begin(), one.applied.end());
            }
        }
        if (r.applied.empty()) return;
    }
    notify();
}

// A control's focus answers whether it has the keyboard: where the GUI last reported it, else the world's last winset.
bool LiveSkin::controlFocusLocked(const std::string& id, const std::string& param, std::string& out) const {
    const Control* c = param == "focus" ? tree_.byName(id) : nullptr;
    if (!c) return false;
    const std::string& f = guiFocusSet_ ? guiFocus_ : focus_;
    out = boolParam(!f.empty() && tree_.byName(f) == c);
    return true;
}

// A BUTTON nobody has checked answers is-checked=false.
bool LiveSkin::buttonUncheckedLocked(const std::string& id, const std::string& param, std::string& out) const {
    const Control* c = param == "is-checked" ? tree_.byName(id) : nullptr;
    if (!c || c->type != "BUTTON") return false;
    out = "false";
    return true;
}

void LiveSkin::setInputText(const std::string& id, const std::string& text) {
    std::lock_guard<std::mutex> g(mu_);
    if (const Control* c = tree_.byName(id); c && c->type == "INPUT") props_.set(c->window + "." + c->name, "text", text);
}

void LiveSkin::setGuiFocus(const std::string& id) {
    std::lock_guard<std::mutex> g(mu_);
    guiFocus_ = id;
    guiFocusSet_ = true;
}

const MacroSet* LiveSkin::builtMacroSetLocked(const std::string& name) const {
    for (const std::string& u : usedMacros_) {
        auto it = macros_.sets.find(u);
        if (ieq(u, name) && it != macros_.sets.end()) return &it->second;
    }
    return nullptr;
}

// `id` or `set.id`, in a built set.
const MacroSet* LiveSkin::macroElemLocked(const std::string& name, size_t& index) const {
    size_t dot = name.find('.');
    std::string set = dot == std::string::npos ? "" : name.substr(0, dot), id = dot == std::string::npos ? name : name.substr(dot + 1);
    if (id.empty()) return nullptr;
    for (const std::string& u : usedMacros_) {
        const MacroSet* ms = set.empty() || ieq(set, u) ? builtMacroSetLocked(u) : nullptr;
        if (!ms) continue;
        for (size_t i = 0; i < ms->entries.size(); i++)
            if (ieq(ms->entries[i].id, id)) { index = i; return ms; }
    }
    return nullptr;
}

bool LiveSkin::applyMacroPairLocked(const WinsetPair& p) {
    size_t i;
    MacroSet& set = const_cast<MacroSet&>(*macroElemLocked(p.target, i));
    MacroEntry& e = set.entries[i];
    if (p.key == "command") e.command = p.value;
    else if (p.key == "is-disabled") e.disabled = winsetBool(p.value);
    else if (p.key == "name") {
        MacroKey k;
        bool rep;
        if (!parseMacroName(p.value, k, rep)) return false;
        e.key = k;
        e.rep = rep;
        e.spec = e.runtime ? macroSpecName(k, rep) : p.value;
        set.reindex();
    } else {
        std::string v = lowerAscii(trimSpace(p.value));
        if (v.empty() || v == "none" || v == "null") {
            if (!e.runtime) return false;
            set.entries.erase(set.entries.begin() + long(i));
            set.reindex();
        } else if (!ieq(v, set.name)) {
            return false;
        }
    }
    macroGen_++;
    return true;
}

// A winset's `parent=<macro set>` for a name nothing holds yet, with that name's name / command / is-disabled pairs.
bool LiveSkin::createMacroLocked(const std::vector<WinsetPair>& pairs, size_t parent, std::vector<bool>& consumed, WinsetResult& r) {
    const WinsetPair& p = pairs[parent];
    if (p.target.find_first_of("*?") != std::string::npos || resolveLocked(p.target) != Kind::Unknown) return false;
    MacroSet* set = const_cast<MacroSet*>(builtMacroSetLocked(trimSpace(p.value)));
    if (!set) return false;
    MacroEntry ent;
    ent.id = p.target.substr(p.target.rfind('.') + 1);
    ent.runtime = true;
    bool keyed = false, command = false, disabled = false;
    r.applied.push_back(p.target + ".parent");
    for (size_t j = 0; j < pairs.size(); j++) {
        const WinsetPair& q = pairs[j];
        if (j == parent || !q.ok || !ieq(q.target, p.target)) continue;
        if (q.key == "name") {
            keyed = parseMacroName(q.value, ent.key, ent.rep);
            ent.spec = keyed ? macroSpecName(ent.key, ent.rep) : q.value;
        } else if (q.key == "command") {
            ent.command = q.value;
            command = true;
        } else if (q.key == "is-disabled") {
            ent.disabled = winsetBool(q.value);
            disabled = true;
        } else {
            continue;
        }
        consumed[j] = true;
        r.applied.push_back(q.target + "." + q.key);
    }
    auto it = keyed ? set->byKey.find({ent.key, ent.rep}) : set->byKey.end();
    if (it != set->byKey.end()) {
        MacroEntry& old = set->entries[it->second];
        if (command) old.command = ent.command;
        if (disabled) old.disabled = ent.disabled;
    } else {
        set->entries.push_back(ent);
        set->reindex();
    }
    macroGen_++;
    relayoutLocked();
    return true;
}

void LiveSkin::useMacroLocked(const std::string& set) {
    if (!file_) return;
    const Block* b = file_->macro(set);
    if (!b) for (const Block& d : file_->blocks) if (d.kind == "macro" && ieq(d.name, set)) b = &d;
    if (b && std::find(usedMacros_.begin(), usedMacros_.end(), b->name) == usedMacros_.end()) usedMacros_.push_back(b->name);
}

void LiveSkin::setClientEnv(const std::string& name, std::string value) {
    std::lock_guard<std::mutex> g(mu_);
    env_[name] = std::move(value);
}

std::string LiveSkin::envLocked(const std::string& name) const {
    auto it = env_.find(name);
    if (it != env_.end()) return it->second;
    const GlobalParam* gp = globalParam(name);
    return gp ? gp->envDefault : "";
}

// The window and pane lists (reached through stockListTest): skin windows in file order (a re-declared name once), then the popups.
std::string LiveSkin::windowListLocked(bool panes) const {
    std::vector<std::string> names;
    auto add = [&](const std::string& n) {
        for (const std::string& x : names) if (ieq(x, n)) return;
        names.push_back(n);
    };
    if (file_)
        for (const Block& b : file_->blocks) {
            if (b.kind != "window") continue;
            const Elem* m = b.main();
            if ((m && m->boolv("is-pane", false)) == panes) add(b.name);
        }
    // A browse() popup is a window (setPopups); one into a name the skin declares is that control, not a window.
    if (!panes)
        for (const std::string& p : knownPopups_)
            if (!declaresLocked(p) && !windowLocked(p) && !tree_.byName(p)) add(p);
    std::string out;
    for (const std::string& n : names) out += (out.empty() ? "" : ";") + n;
    return out;
}

// A control winget's property names pass through the name block too; its control id does not.
void LiveSkin::nameBlockWriteControlLocked(const std::string& params) const {
    for (const std::string& n : wingetParamNames(params)) nameBlockWrite(nameBlock_, n);
}

bool LiveSkin::globalLocked(const std::string& name, std::string& out) const {
    nameBlockWrite(nameBlock_, name);   // each name is a fresh heap string
    const GlobalParam* gp = globalParam(name);
    Global what;
    if (gp && (name == "url" || name == "focus" || name == "dpi")) what = gp->what;   // tested before the lists
    else if (std::optional<Global> list = stockListTest(name, nameBlock_)) what = *list;
    else if (gp) what = gp->what;
    else return false;
    switch (what) {
    case Global::Focus: {
        out.clear();
        if (guiFocusSet_ && !guiFocus_.empty()) { out = guiFocus_; return true; }   // where the keyboard really is (setGuiFocus)
        const Control* c = guiFocusSet_ || focus_.empty() ? nullptr : tree_.byName(focus_);
        if (c && c->visible) out = c->window + "." + c->name;
        else if (const Elem* e = c || guiFocusSet_ ? nullptr : declaresLocked(focus_)) {   // an unplaced control (setFocusLocked)
            for (const Block& b : file_->blocks)
                if (b.kind == "window")
                    for (const Elem* x : b.controls()) if (x == e) out = b.name + "." + e->name;
        }
        if (out.empty())
            if (const Block* mw = file_ ? findMainWindow(*file_) : nullptr) out = mw->name;
        return true;
    }
    case Global::Windows: out = windowListLocked(false); return true;
    case Global::Panes: out = windowListLocked(true); return true;
    case Global::Menus:
    case Global::Macros: {
        out.clear();
        for (const std::string& n : what == Global::Menus ? usedMenus_ : usedMacros_) out += (out.empty() ? "" : ";") + n;
        return true;
    }
    case Global::Env: out = envLocked(name); return true;
    case Global::BrowserOptions:
        // Stock reports only devtools (bit 1) and find (bit 2), in that order. byondstorage, zoom and refresh are accepted by
        // the winset but never answered.
        out.clear();
        if (browserOptions_ & 1) out = "devtools";
        if (browserOptions_ & 2) out += out.empty() ? "find" : ",find";
        return true;
    }
    return false;
}

std::string LiveSkin::wingetGlobal(const std::string& params, const ParamLookup& extra) const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> names = wingetParamNames(params);
    std::string out = wingetAnswer(params, [&](const std::string& n, std::string& v) {
        return globalLocked(n, v) || (extra && extra(n, v));
    });
    // A lone name's answer is written into the name block, except `url`'s; a lone name of 16 or more letters clobbers it.
    if (names.size() == 1 && names[0] != "url") nameBlockWrite(nameBlock_, out);
    if (names.size() == 1 && names[0].size() >= 16) nameBlockClobber(nameBlock_);
    return out;
}

std::string LiveSkin::wingetWildcard(const std::string& control, const std::string& params) const {
    std::lock_guard<std::mutex> g(mu_);
    nameBlockWriteControlLocked(params);   // a wildcard's answer is not written to the name block
    std::vector<std::string> names = wingetParamNames(params);
    if (names.empty()) return "";
    // One enumerated element: its answer-key prefix and its lookup (both under mu_).
    struct Item { std::string key; std::function<bool(const std::string&, std::string&)> get; };
    std::vector<Item> items;
    std::string base;
    bool wild = control.size() > 2 && control.compare(control.size() - 2, 2, ".*") == 0;
    if (wild) base = control.substr(0, control.size() - 2);
    if (wild && !base.empty() && base.find_first_of("*?.") == std::string::npos && file_) {
        if (const Block* w = windowLocked(base)) {
            std::string wn = w->name;
            items.push_back({wn, [this, wn](const std::string& k, std::string& v) { return wingetLocked(wn, k, v); }});
            // The window lookup ignores case but the control prefix does not, so `MainWindow.*` lists the window alone.
            for (const Elem* e : w->controls()) {
                if (wn != base || e->name.empty()) continue;
                std::string path = wn + "." + e->name;
                items.push_back({path, [this, path](const std::string& k, std::string& v) { return wingetLocked(path, k, v); }});
            }
            // Then the runtime controls parented to it, in creation order. A wildcard does not list `runtime`, though a
            // direct winget answers runtime=true.
            for (const RuntimeCtl& rc : runtime_) {
                if (wn != base || !ieq(rc.parent, wn)) continue;
                std::string nm = rc.name;
                items.push_back({wn + "." + nm, [this, nm](const std::string& k, std::string& v) {
                    const RuntimeCtl* r = runtimeLocked(nm);
                    return k != "runtime" && r && runtimeWingetLocked(*r, k, v);
                }});
            }
        } else if (knownPopupLocked(base) && !declaresLocked(base)) {
            std::string pn = base;
            for (const std::string& p : knownPopups_) if (ieq(p, base)) pn = p;
            items.push_back({pn, [this, pn](const std::string& k, std::string& v) {
                if (k == "id") { v = pn; return true; }
                return wingetLocked(pn, k, v);
            }});
            // A popup holds one control, `browser`.
            items.push_back({pn + ".browser", [](const std::string& k, std::string& v) {
                if (k == "id") { v = "browser"; return true; }
                if (k == "type") { v = "BROWSER"; return true; }
                if (k == "is-visible") { v = "true"; return true; }
                return false;
            }});
        } else {
            for (const Block& b : file_->blocks) {
                if ((b.kind != "menu" && b.kind != "macro") || !ieq(b.name, base)) continue;
                const std::vector<std::string>& used = b.kind == "menu" ? usedMenus_ : usedMacros_;
                if (std::find(used.begin(), used.end(), b.name) == used.end()) continue;
                bool menu = b.kind == "menu";
                items.clear();   // a re-declared datum: the last one wins (File::macro)
                if (!menu) {
                    const MacroSet& ms = macros_.sets.at(b.name);
                    std::vector<size_t> order(ms.entries.size());
                    for (size_t i = 0; i < order.size(); i++) order[i] = i;
                    std::stable_sort(order.begin(), order.end(), [&ms](size_t x, size_t y) { return listedBefore(ms.entries[x], ms.entries[y]); });
                    for (size_t i : order) {
                        if (ms.entries[i].id.empty()) continue;   // an unnamed macro is not enumerated
                        items.push_back({b.name + "." + ms.entries[i].id, [&ms, i](const std::string& k, std::string& v) {
                            return macroParam(ms.entries[i], k, v);
                        }});
                    }
                    continue;
                }
                if (menu) {
                    // The menu itself, with the defaults stock answered for it (menu.is-checked=false, menu.command=, ...).
                    std::string mn = b.name;
                    items.push_back({mn, [mn](const std::string& k, std::string& v) {
                        if (k == "id") { v = mn; return true; }
                        if (k == "is-checked" || k == "can-check" || k == "is-disabled") { v = "false"; return true; }
                        if (k == "name" || k == "command" || k == "category" || k == "group") { v.clear(); return true; }
                        return false;
                    }});
                }
                for (const Elem& e : b.elems) {
                    if (e.name.empty()) continue;   // an unnamed menu item or macro is not enumerated
                    const Elem* ep = &e;
                    items.push_back({b.name + "." + e.name, [this, ep, menu](const std::string& k, std::string& v) {
                        if (k == "id") { v = ep->name; return true; }
                        if (menu && props_.get(ep->name, k, v)) return true;   // a click / winset on the item
                        if ((menu && k == "is-checked") || k == "is-disabled") { v = boolParam(ep->boolv(k, false)); return true; }
                        const std::string* r = ep->get(k);
                        if (!r) return false;
                        v = *r;
                        return true;
                    }});
                }
            }
        }
    }
    if (items.empty()) return wingetAnswer(params, [](const std::string&, std::string&) { return false; });   // an unknown control
    std::string out;
    for (const Item& it : items)
        for (const std::string& n : names) {
            std::string v;
            if (!it.get(n, v)) continue;
            if (!out.empty()) out.push_back(';');
            out += it.key + "." + n + "=" + wingetEncode(v);
        }
    return out;
}

bool LiveSkin::knownPopupLocked(const std::string& name) const {
    for (const std::string& p : knownPopups_) if (ieq(p, name)) return true;
    return false;
}

bool LiveSkin::isPopup(const std::string& name) const {
    std::lock_guard<std::mutex> g(mu_);
    return knownPopupLocked(name);
}

// ---------------------------------------------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------------------------------------------

std::string LiveSkin::load(const std::string& dmf) {
    ParseResult pr = parseDmf(dmf);
    if (!pr.ok) return "dmf parse error line " + std::to_string(pr.errLine) + ": " + pr.error;
    {
        std::lock_guard<std::mutex> g(mu_);
        file_ = std::make_shared<const File>(std::move(pr.file));
        text_ = dmf;
        fileId_++;
        props_.clear();
        splits_.clear();
        splitPct_.clear();
        macros_ = resolveMacros(*file_);
        macro_ = macros_.active;
        focus_.clear();
        usedMenus_.clear();
        usedMacros_.clear();
        runtime_.clear();
        for (const Clone& c : clones_) knownPopups_.erase(c.name);
        clones_.clear();
        for (const Block& b : file_->blocks) {
            const Elem* m = b.kind == "window" ? b.main() : nullptr;
            if (!m) continue;
            std::string mn = m->str("menu", ""), mc = m->str("macro", "");
            for (const Block& d : file_->blocks)
                if (d.kind == "menu" && !mn.empty() && ieq(d.name, mn) &&
                    std::find(usedMenus_.begin(), usedMenus_.end(), d.name) == usedMenus_.end()) usedMenus_.push_back(d.name);
            if (!mc.empty()) useMacroLocked(mc);
        }
        if (!macro_.empty()) useMacroLocked(macro_);   // the set this client made active (macro.h: the sole set when none is named)
        relayoutLocked();
        // Run the on-show of the window that fills each slot the skin states. An empty side waits for its pane to be shown
        // (setPaneShown).
        shownPanes_.clear();
        std::vector<std::string> occupants;
        for (const Slot& s : tree_.slotList) if (!s.chosen.empty()) occupants.push_back(s.chosen);
        for (const std::string& wn : occupants) {
            const Block* b = file_->window(wn);
            if (!b) continue;
            for (const Elem& e : b->elems) {
                const std::string* os = e.get("on-show");
                std::string params;
                if (os && onShowParams(*os, params) && !params.empty()) winsetLocked("", params);
            }
        }
        relayoutLocked();
        // (E4) winsets that arrived before the skin.
        std::vector<PendingWinset> pend;
        pend.swap(pending_);
        for (const PendingWinset& w : pend) winsetLocked(w.control, w.props, w.urlEncoded);
    }
    notify();
    return "";
}

bool LiveSkin::loaded() const { std::lock_guard<std::mutex> g(mu_); return bool(file_); }
bool LiveSkin::loadedFrom(const std::string& dmf) const { std::lock_guard<std::mutex> g(mu_); return file_ && text_ == dmf; }
bool LiveSkin::ready() const { std::lock_guard<std::mutex> g(mu_); return file_ && clientSet_; }

void LiveSkin::setClient(int w, int h) {
    {
        std::lock_guard<std::mutex> g(mu_);
        Rect r{0, 0, w, h};
        if (clientSet_ && client_ == r) return;
        client_ = r;
        clientSet_ = true;
        relayoutLocked();
    }
    notify();
}

WinsetResult LiveSkin::winset(const std::string& control, const std::string& props, bool urlEncoded) {
    WinsetResult r;
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!file_) {
            if (pending_.size() < 512) pending_.push_back({control, props, urlEncoded});
            return r;
        }
        r = winsetLocked(control, props, urlEncoded);
        if (r.applied.empty()) return r;
    }
    notify();
    return r;
}

// A menu radio group: checking one item unchecks the others in its group, as a grouped BUTTON does. An item sharing
// the checked one's name is left alone, since a name may be declared twice (vg's icon64 is both 64x64 and 48x48).
void LiveSkin::uncheckMenuSiblingsLocked(const std::string& name) {
    if (!file_) return;
    Menu m = menuFromFile(*file_);
    std::string group;
    for (const MenuCat& c : m.cats)
        for (const MenuItem& it : c.items)
            if (group.empty() && ieq(it.name, name)) group = it.group;
    if (group.empty()) return;
    for (const MenuCat& c : m.cats)
        for (const MenuItem& it : c.items)
            if (!it.name.empty() && it.group == group && !ieq(it.name, name)) props_.set(it.name, "is-checked", "false");
}

void LiveSkin::setPopups(std::set<std::string> names) {
    std::lock_guard<std::mutex> g(mu_);
    knownPopups_.insert(names.begin(), names.end());
    popups_ = std::move(names);
}

void LiveSkin::setPopupShown(const std::string& name, bool shown) {
    std::lock_guard<std::mutex> g(mu_);
    if (cloneLocked(name)) props_.set(name, "is-visible", shown ? "true" : "false");
    if (shown) hiddenPopups_.erase(name);
    else hiddenPopups_.insert(name);
}

LiveSkin::Clone* LiveSkin::cloneLocked(const std::string& name) {
    for (Clone& c : clones_) if (ieq(c.name, name)) return &c;
    return nullptr;
}

bool LiveSkin::destroyCloneLocked(const std::string& name) {
    Clone* c = cloneLocked(name);
    if (!c) return false;
    std::string n = c->name;
    clones_.erase(clones_.begin() + (c - clones_.data()));
    for (auto it = knownPopups_.begin(); it != knownPopups_.end();) it = ieq(*it, n) ? knownPopups_.erase(it) : std::next(it);
    hiddenPopups_.erase(n);
    props_.eraseTarget(n);
    screenPos_.erase(lowerAscii(n));
    for (auto it = runtime_.begin(); it != runtime_.end();) it = ieq(it->parent, n) ? runtime_.erase(it) : std::next(it);
    return true;
}

void LiveSkin::cloneWindow(const std::string& name) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!file_ || name.empty()) return;
        destroyCloneLocked(name);
        clones_.push_back({name, "", false, 0});
        knownPopups_.insert(name);
        props_.set(name, "is-visible", "false");
        props_.set(name, "size", "640x480");
        props_.set(name, "can-close", "true");
        props_.set(name, "statusbar", "true");
    }
    notify();
}

bool LiveSkin::setClonePage(const std::string& control, const std::string& html) {
    std::lock_guard<std::mutex> g(mu_);
    size_t dot = control.find('.');
    if (dot == std::string::npos) return false;
    Clone* c = cloneLocked(control.substr(0, dot));
    if (!c) return false;
    c->html = html;
    c->hasHtml = true;
    c->pageSeq++;
    return true;
}

std::vector<LiveSkin::CloneWindow> LiveSkin::cloneWindows() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<CloneWindow> out;
    for (const Clone& c : clones_) {
        CloneWindow w{c.name, c.html, "", "", c.hasHtml, false, c.pageSeq};
        std::string v;
        w.shown = props_.get(c.name, "is-visible", v) && winsetBool(v);
        props_.get(c.name, "size", w.size);
        props_.get(c.name, "pos", w.pos);
        out.push_back(std::move(w));
    }
    return out;
}

std::vector<LiveSkin::CloneAction> LiveSkin::cloneActions(std::map<std::string, uint64_t>& open) const {
    std::vector<CloneAction> out;
    std::set<std::string> alive;
    for (const CloneWindow& c : cloneWindows()) {
        alive.insert(c.name);
        auto it = open.find(c.name);
        if (c.shown && c.hasHtml) {
            if (it != open.end() && it->second == c.pageSeq) continue;
            open[c.name] = c.pageSeq;
            std::string params = "window=" + c.name;
            if (!c.size.empty()) params += ";size=" + c.size;
            if (!c.pos.empty()) params += ";pos=" + c.pos;
            out.push_back({true, c.name, std::move(params), c.html});
        } else if (it != open.end()) {
            out.push_back({false, c.name, "", ""});
            open.erase(it);
        }
    }
    for (auto it = open.begin(); it != open.end();) {
        if (alive.count(it->first)) { ++it; continue; }
        out.push_back({false, it->first, "", ""});
        it = open.erase(it);
    }
    return out;
}

Menu LiveSkin::menuLocked() const {
    if (!file_) return {};
    Menu m = menuFromFile(*file_);
    std::string v;
    for (MenuCat& c : m.cats) {
        if (!c.name.empty() && props_.get(c.name, "is-disabled", v)) c.disabled = winsetBool(v);
        for (MenuItem& it : c.items) {
            if (!it.name.empty() && props_.get(it.name, "is-disabled", v)) it.disabled = winsetBool(v);
            if (!it.name.empty() && it.canCheck && props_.get(it.name, "is-checked", v)) it.checked = winsetBool(v);
        }
    }
    return m;
}

SkinSnapshot LiveSkin::snapshot() const {
    std::lock_guard<std::mutex> g(mu_);
    SkinSnapshot s;
    s.file = file_;
    s.fileId = fileId_;
    s.generation = gen_;
    s.client = tree_.client;
    s.tree = tree_;
    s.props = props_;
    s.macros = macros_;
    s.macroGen = macroGen_;
    s.macro = macro_;
    s.focus = focus_;
    s.focusSeq = focusSeq_;
    s.menu = menuLocked();
    if (file_) if (const Block* mw = findMainWindow(*file_)) s.mainWindow = mw->name;
    for (const RuntimeCtl& r : runtime_) s.runtime.push_back({r.name, r.parent, r.type});
    return s;
}

std::vector<std::string> LiveSkin::runtimeMaps() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out;
    for (const RuntimeCtl& r : runtime_) if (r.type == "MAP") out.push_back(r.name);
    return out;
}

uint64_t LiveSkin::generation() const { std::lock_guard<std::mutex> g(mu_); return gen_; }
unsigned LiveSkin::browserOptions() const { std::lock_guard<std::mutex> g(mu_); return browserOptions_; }

std::string LiveSkin::mainIcon() const {
    std::lock_guard<std::mutex> g(mu_);
    if (!file_) return {};
    const Block* w = findMainWindow(*file_);
    if (!w) return {};
    std::string v;
    if (props_.get(w->name, "icon", v)) return v;
    const Elem* m = w->main();
    return m ? m->str("icon", "") : std::string();
}

std::string LiveSkin::onSizeCommand() const {
    std::lock_guard<std::mutex> g(mu_);
    return file_ ? onSize(*file_) : std::string();
}

void LiveSkin::notify() const {
    std::function<void()> cb;
    { std::lock_guard<std::mutex> g(const_cast<std::mutex&>(cbMu_)); cb = onChange_; }
    if (cb) cb();
}

LiveSkin::Stats LiveSkin::stats() const { std::lock_guard<std::mutex> g(mu_); return stats_; }

}  // namespace fugue::skin
