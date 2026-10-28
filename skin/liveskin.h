// Live skin state: the parsed .dmf, the winset overlay and the layout they imply, as one thread-safe,
// value-owning object. Pure C++ (no Qt, no socket), so the session thread and the GUI thread
// share the same code.
//
// Threading: the session thread applies winsets and answers wingets against this object's own layout. The GUI
// only reports the client size (setClient). The GUI thread takes a snapshot whenever generation() moves. One
// mutex, held only for in-memory work; the change callback runs outside it.
//
// The winset/winget rules follow stock DreamSeeker 516: which key has a consumer on which target kind, the
// left/right canonical spelling, radio-group unchecking, focus only on INPUT/MAP. A macro= of an unknown set is refused
// here; stock answers that name and binds no keys. Winsets and wingets are answered locally, never sent as a Topic.
// A lone name answers the bare value, a ';' list answers k=v in query order (an unknown name as `k=`), and a flagged
// empty-prop query is `type`.
// A `<X>.*` control is answered by wingetWildcard; any other wildcard control is not answerable.
//
// Extensions beyond stock, each labelled where it is implemented. None overrides a value stock answers:
//   (E1) a control the skin declares but the layout does not place still answers type, is-visible and is-disabled.
//   (E2) a window block answers type=MAIN, size/inner-size, macro (the active set) and the MAIN element's file rows.
//   (E3) a placed control's file-stated row (text, anchor1, ...) answers when nothing stock answers first.
//   (E4) a winset that arrives before the skin is buffered and replayed on load.
//
// Stock behaviour also modelled: the null-control globals, "<X>.*" wildcards, `id` everywhere, `pos` (pane-local for a
// pane, the file pos for a hidden window or an unplaced control), every window's `dpi`, an unplaced control's file
// `size`, window places, runtime-created controls and macros, and the GUI's focus. A side no window is stated for stays empty
// until the GUI shows a pane whose on-show claims it (setPaneShown, skin/layout.h inferSlots).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "skin/dmf.h"
#include "skin/layout.h"
#include "skin/macro.h"
#include "skin/menu.h"
#include "skin/props.h"
#include "skin/winset.h"

namespace fugue::skin {

// One consistent copy of everything a painter needs.
struct SkinSnapshot {
    std::shared_ptr<const File> file;
    uint64_t fileId = 0;          // bumps on every load(): a different id means "rebuild the widgets"
    uint64_t generation = 0;      // bumps on every state change
    Rect client;
    Tree tree;
    PropStore props;
    MacroTable macros;            // with the macros a winset created or changed
    uint64_t macroGen = 0;        // bumps when a winset changes a macro set's bindings
    std::string macro;            // the active macro set name
    std::string focus;            // last control a winset focused ("window.name" or as addressed), "" = none
    uint64_t focusSeq = 0;        // bumps each time a winset sets focus (even to the same control)
    Menu menu;                    // with the live is-disabled overlay applied
    std::string mainWindow;       // the main window's block name
    // The controls a winset `parent=` created (LiveSkin::runtime_), in creation order: name, parent window (a skin window's
    // block name or a browse() popup's name), type (upper-case, "" until a `type=` arrives). Their pos / size / is-visible
    // are in `props` under `name`. shell/skinwindow builds a MAP one inside its popup (vg's camera console, tgui ByondUi).
    struct RuntimeControl { std::string name, parent, type; };
    std::vector<RuntimeControl> runtime;
};

struct WinsetResult {
    std::vector<std::string> applied, unhandled;
};

class LiveSkin {
public:
    // Parses and resets all live state (overlay, splits, focus). Returns "" or the parse error (the previous
    // skin stays). Buffered pre-skin winsets are replayed.
    std::string load(const std::string& dmf);
    bool loaded() const;
    bool loadedFrom(const std::string& dmf) const;   // the loaded skin was parsed from exactly this text
    // True once a skin AND a real client size are known: the point at which a winget can be answered from a
    // laid-out tree rather than held ("not yet" is not "no").
    bool ready() const;
    void setClient(int w, int h);

    // urlEncoded: a WORLD winset (0x00a8, skin/skinfeed), whose values DreamSeeker url-decodes (skin/winset.h splitWinset).
    WinsetResult winset(const std::string& control, const std::string& props, bool urlEncoded = false);
    // A skin-initiated `.winset "..."` body (button / macro / menu command): the same as winset("", body), except
    // that DreamSeeker's conditional form (`cond ? a b : c`, skin/winset.h parseClientWinset) is EVALUATED against
    // the live state, clause by clause. Only the client command has that form; a wire winset never does.
    WinsetResult clientWinset(const std::string& body);
    // One property. false = refuse (the caller answers "").
    bool winget(const std::string& control, const std::string& param, std::string& out) const;
    // DM's verbatim params string, answered in DreamSeeker's shape (skin/winset.h wingetAnswer: one name -> the bare
    // value, several -> k=v;k=v with every name listed and the values encoded).
    std::string wingetParams(const std::string& control, const std::string& params) const;
    // A player's drag of a CHILD's divider: `path` is the CHILD's "window.elem" (skin::Splitter::path), `pct` the
    // percentage DreamSeeker computes for the drop (shell/skinwindow.cpp SplitterHandle). It lands in the same state a
    // `winset splitter=` writes, so winget / a later winset / the layout all see one value. false = no such splitter.
    bool setSplitter(const std::string& path, double pct);
    // winexists(): the control's TYPE, "" when there is no such control/window/popup.
    std::string wingetType(const std::string& control) const;
    // Whether output() text sent to this control prints in the chat (model::Session::setTextTarget). Stock DreamSeeker
    // prints it only into an OUTPUT control: not into a BROWSER, LABEL, MAP or window, nor a control that does not exist
    // (a winclone'd window's browser too). With no skin loaded yet, everything prints. No winexists()
    // side effect.
    bool printsOutput(const std::string& control) const;
    // The default MAP control's `style` (what a winset last set, else the .dmf's). Stock lays out MeasureText() with
    // this sheet: MeasureText(t, null, w) after `winset map style=S` answers MeasureText(t, S, w) (render/dsmaptext.h).
    // The is-default map, else the first map; "" with no skin or no map. A text-color the control sets (not "none") leads
    // the sheet as a `body` colour rule: stock draws maptext that names no colour in it, else white.
    std::string defaultMapStyle() const;
    // The MAP controls a winset `parent=` created (SkinSnapshot::runtime), by name: the secondary maps the session thread
    // assembles a scene for (render/scene.h assembleSecondaryMap).
    std::vector<std::string> runtimeMaps() const;
    // The default MAP control's "window.elem": the is-default one, else the first; "" with no skin or no map.
    std::string defaultMapId() const;
    // Every MAP control but the default one, by the name a screen_loc prefix uses: the .dmf's other MAPs (their elem
    // name) and runtimeMaps(). The secondary maps the session thread assembles.
    std::vector<std::string> secondaryMaps() const;

    // winget(client, null or "", params): the client-wide ("special") wingets, answered as stock DreamSeeker 516
    // answers them (the table is in liveskin.cpp kGlobalParams). Which name reads the windows,
    // panes, menu or macro list follows stock's own matcher (liveskin.cpp stockListTest), so the answer depends on
    // the names asked before it. The shape rules are skin/winset.h wingetParamNames: one name gives its bare value
    // ("" for a name DreamSeeker does not know), several give k=v;k=v with every name listed (an unknown one as
    // `k=`) and the values wingetEncode'd. Answers even with no skin. `extra` answers a name this table does not
    // hold (SkinFeed's disclosure answer, skinfeed.h). wingetWildcard never consults it, so an enumeration cannot
    // reach it.
    using ParamLookup = std::function<bool(const std::string&, std::string&)>;
    std::string wingetGlobal(const std::string& params, const ParamLookup& extra = {}) const;
    // winget(client, "<X>.*", params): X (case-insensitive) names a skin window, a browse() popup, or a menu or macro
    // set DreamSeeker has instantiated (see usedMenus_). It enumerates X itself (windows, popups and menus; a macro
    // set has no entry of its own), then its named elements in file order as `X.param=v` / `X.elem.param=v`, each
    // element listing only the params it has, values encoded. The k=v form is always used, even for one param.
    // Anything else (`*`, `main*`, `*.map`, `win.ctl.*`, a menu no window has named yet) is an unknown control.
    std::string wingetWildcard(const std::string& control, const std::string& params) const;
    // The global wingets the skin cannot know (the GUI / session publish them; see kGlobalParams for which and the
    // stock defaults used until they do): "dpi", "screen-pos", "screen-size", "url", "sound", "hwmode", the locks.
    void setClientEnv(const std::string& name, std::string value);
    // The GUI's half of a window's `pos` (and `outer-pos`, which stock answers identically): where the window really
    // is on screen, its frame's top-left. `window` "" is the main window. Stock answers a window's real place: the
    // main window and popups are refused until the GUI has published a place, a skin window answers its .dmf `pos`
    // once shown and the winset value after a move (kept while hidden). A world winset of a popup's pos answers
    // verbatim until the GUI republishes.
    void setWindowPos(const std::string& window, int x, int y);
    // The GUI's keyboard focus as DreamSeeker spells it for winget(null, "focus"): "window.control" for a skin
    // control, "<popup>.browser" for a popup's page, "" for the main window itself (the answer is then its name,
    // "mainwindow"). A winset `focus` overrides it until the GUI reports again.
    void setGuiFocus(const std::string& id);
    // The GUI's half of an INPUT's text: what the player has typed. winget answers it, and a winset replaces it.
    void setInputText(const std::string& id, const std::string& text);
    // The GUI really has content for this pane window now (the map has its first frame, or the statpanels their
    // first tab: a statpanel or a verb panel). Stock runs a pane's on-show when it shows the pane, and only that
    // fills a split side the skin leaves empty (skin/layout.h inferSlots). The on-show runs here through the same
    // evaluator as a skin button's .winset (clientWinset), once per pane per skin, and only for a pane the layout
    // does not already place (a stated side's pane ran its on-show at load).
    void setPaneShown(const std::string& window);

    // Names of open browse() windows, published by the session thread: a winset addressed to one is a "popup" target.
    // Stock DreamSeeker 516 treats a browse() popup as a window: winexists() and winget(,"type") answer "MAIN", never
    // "BROWSER". That answer belongs to a skin `browser` control; a `browserwindow` is MAIN. vg's tgui_window.dm keys
    // is_browser = winexists(...) == "BROWSER" on this, and only a non-browser registers on-close="uiclose ...".
    // Closing a popup (the player's close box, or browse(null)) hides it: winexists stays "MAIN", is-visible reads
    // "false", and every property the world set, on-close included, survives into the next browse() of the same id.
    // A winset to an id that was never browse()d is dropped. So a name stays a popup for the rest of the session
    // once it has been one; only its visibility changes.
    void setPopups(std::set<std::string> names);
    // The GUI's half of is-visible: a popup the player closed is hidden until the world browse()s it again.
    void setPopupShown(const std::string& name, bool shown);
    bool isPopup(const std::string& name) const;

    // winclone(src, "window", name) (s2c 0x00b1): a blank window the world fills. Stock DreamSeeker answers winexists
    // "MAIN" and is-visible "false" until a winshow / is-visible winset, takes size, on-close and title like a popup, and
    // destroys it on `parent=none`. Its page is what output() sends to one of its controls (vg's html_interface builds
    // "browser_<ref>.browser" in one). A clone from a skin window or pane is not modelled.
    struct CloneWindow {
        std::string name, html, size, pos;   // size / pos: what the world set, "" when it set none
        bool hasHtml = false, shown = false;
        uint64_t pageSeq = 0;                // bumps on every page
    };
    void cloneWindow(const std::string& name);
    // output(html, "<clone>.<control>"): false when `control` is not inside a clone.
    bool setClonePage(const std::string& control, const std::string& html);
    std::vector<CloneWindow> cloneWindows() const;
    // What the GUI must do for the clones now: open (or reload) a shown clone that holds a page it was not last sent,
    // close one that is hidden, closed by the player or destroyed. `open` maps a clone to the page seq the GUI holds.
    struct CloneAction { bool open; std::string name, params, html; };
    std::vector<CloneAction> cloneActions(std::map<std::string, uint64_t>& open) const;

    SkinSnapshot snapshot() const;
    uint64_t generation() const;
    // winset(null, "browser-options=...") as DreamSeeker keeps it: devtools 1, find 2, byondstorage 4, zoom 8, refresh 16.
    // The browser panes read bit 2 to decide whether Ctrl+F opens their find bar (shell/browserpane.h).
    unsigned browserOptions() const;
    // The MAIN window's `icon` as it stands: a winset's value, else the .dmf's (unquoted by the parser); "" = none.
    // skin/winicon.h parseIconRef reads it.
    std::string mainIcon() const;
    std::string onSizeCommand() const;     // MAIN on-size verbatim ("" none)
    // Called (outside the lock, from whichever thread mutated) after any state change. Safe to (re)set at any time.
    void setOnChange(std::function<void()> cb) { std::lock_guard<std::mutex> g(cbMu_); onChange_ = std::move(cb); }

    // Gap accounting (winsets applied and winsets unhandled).
    struct Stats { uint64_t applied = 0; std::vector<std::string> unhandled; };
    Stats stats() const;

private:
    enum class Kind { Unknown, Control, Popup, Menu, Window, Runtime, Macro };
    // ---- all *Locked: caller holds mu_
    void relayoutLocked();
    Kind resolveLocked(const std::string& name) const;
    const Elem* declaresLocked(const std::string& name) const;
    const Block* windowLocked(const std::string& name) const;
    std::string controlTypeLocked(const std::string& target) const;
    bool hasConsumerLocked(Kind kind, const WinsetPair& p) const;
    bool applyPropLocked(const WinsetPair& p);
    bool setFocusLocked(const std::string& target, bool on);
    void uncheckSiblingsLocked(const std::string& target);
    bool wingetLocked(const std::string& control, const std::string& param, std::string& out) const;
    std::string defaultMapIdLocked() const;
    bool controlFocusLocked(const std::string& id, const std::string& param, std::string& out) const;
    bool buttonUncheckedLocked(const std::string& id, const std::string& param, std::string& out) const;
    Menu menuLocked() const;
    bool isMenuTarget(const std::string& name) const;
    std::string menuElemLocked(const std::string& name) const;
    void uncheckMenuSiblingsLocked(const std::string& name);
    WinsetResult winsetLocked(const std::string& control, const std::string& props, bool urlEncoded = false);
    void applyPairLocked(const WinsetPair& p, WinsetResult& r);
    void accountLocked(const std::string& control, const WinsetResult& r);
    bool conditionLocked(const WinsetPair& cond) const;
    void setSplitPctLocked(const std::string& path, double pct);
    bool globalLocked(const std::string& name, std::string& out) const;
    void nameBlockWriteControlLocked(const std::string& params) const;
    std::string envLocked(const std::string& name) const;
    std::string windowListLocked(bool panes) const;
    void useMacroLocked(const std::string& set);
    // Runtime-created controls (winset `parent=`): see runtime_.
    struct RuntimeCtl { std::string name, parent, type; };
    const RuntimeCtl* runtimeLocked(const std::string& name) const;
    std::string typeLocked(const std::string& control) const;
    bool applyParentLocked(const WinsetPair& p);
    std::string parentWindowLocked(const std::string& v) const;
    bool runtimeWingetLocked(const RuntimeCtl& r, const std::string& param, std::string& out) const;
    WinsetResult clientWinsetLocked(const std::string& body);   // clientWinset, caller holds mu_
    // Macro elements: see usedMacros_.
    const MacroSet* builtMacroSetLocked(const std::string& name) const;
    const MacroSet* macroElemLocked(const std::string& name, size_t& index) const;
    bool applyMacroPairLocked(const WinsetPair& p);
    bool createMacroLocked(const std::vector<WinsetPair>& pairs, size_t parent, std::vector<bool>& consumed, WinsetResult& r);

    mutable std::mutex mu_;
    std::shared_ptr<const File> file_;
    std::string text_;   // the .dmf text file_ was parsed from
    uint64_t fileId_ = 0, gen_ = 0;
    Rect client_;
    bool clientSet_ = false;
    Tree tree_;
    PropStore props_;
    std::map<std::string, double> splits_;
    std::map<std::string, float> splitPct_;   // "window.elem" -> the splitter value as winset/a drag stored it (winget)
    MacroTable macros_;
    std::string macro_, focus_;
    uint64_t focusSeq_ = 0;
    std::set<std::string> popups_;          // open in the model now
    std::set<std::string> knownPopups_;     // ever browse()d this session (DreamSeeker hides, never destroys, a popup)
    std::set<std::string> hiddenPopups_;    // the player closed it and the world has not re-browsed it
    struct Clone { std::string name, html; bool hasHtml = false; uint64_t pageSeq = 0; };
    std::vector<Clone> clones_;             // winclone()d windows, also in knownPopups_; visibility is the `is-visible` prop
    Clone* cloneLocked(const std::string& name);
    bool destroyCloneLocked(const std::string& name);
    bool knownPopupLocked(const std::string& name) const;
    struct PendingWinset { std::string control, props; bool urlEncoded; };
    std::vector<PendingWinset> pending_;   // winsets that arrived before the skin, replayed on load (E4)
    std::map<std::string, std::string> env_;   // setClientEnv
    unsigned browserOptions_ = 0;              // winset(null, "browser-options=..."): devtools 1, find 2, ... (applyPairLocked)
    // The first 8 text bytes of the heap block DreamSeeker builds each global-winget name in, as the names, control
    // wingets and lone answers this client has seen wrote them (liveskin.cpp nameBlockWrite). Zero = never used.
    mutable char nameBlock_[8] = {};
    std::map<std::string, std::pair<int, int>> screenPos_;   // setWindowPos, keyed lower-case ("" = the main window)
    std::string guiFocus_;                     // setGuiFocus
    bool guiFocusSet_ = false;
    // Stock DreamSeeker 516 creates a control for `winset(src, "rtlab", "parent=extrawin;type=label;...")`: winexists
    // answers its type, "extrawin.*" lists it after the window's file controls as `extrawin.rtlab.*`, and a direct winget
    // answers id, type, pos, size, parent, runtime=true and is-visible (true unless set). `parent=none`, `parent=` and
    // `parent=null` each destroy it. vg's tooltips are such controls in mapwindow, and clearAll() finds them only through
    // winget(owner, "mapwindow.*", "id"). The parent may also be a browse() popup, which is how tgui's ByondUi places a
    // map inside its window; `parent=` removes it. Kept in creation order; winget and winexists answer them, and a MAP
    // one is built as a widget (SkinSnapshot::runtime).
    std::vector<RuntimeCtl> runtime_;
    std::set<std::string> shownPanes_;         // setPaneShown (lower-case)
    // The menus and macro sets stock DreamSeeker has built, in build order. winget(null,"macro") and "menu" list only the
    // sets some window has named, and a "<set>.*" wildcard enumerates nothing before that. Seeded at load from every
    // window's `menu` / `macro`, grown by a `macro` winset (this client does not consume a `menu` winset).
    // A built set's macros are elements: winexists answers MACRO, and a winset of `name`, `command` or `is-disabled`
    // changes the binding. `winset(src, "id", "parent=<set>;name=...;command=...")` adds one to a built set (a set not
    // built yet drops it); when the set already binds that combination, that binding takes the command and keeps its id.
    // `parent=none`, `parent=null` or `parent=` removes a macro made that way; a .dmf macro cannot be removed.
    std::vector<std::string> usedMenus_, usedMacros_;
    uint64_t macroGen_ = 0;
    Stats stats_;
    std::mutex cbMu_;                       // guards onChange_ only (set from the GUI thread, read from any mutating thread)
    std::function<void()> onChange_;
    void notify() const;
};

}  // namespace fugue::skin
