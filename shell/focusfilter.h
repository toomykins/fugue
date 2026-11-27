// FocusFilter: the app-level key router.
//   map / output / info focused -> every key goes to macro routing
//   input focused               -> the key's own binding fires unless the text field takes the key (inputBarFires
//                                  in focusfilter.cpp), and Any fires only for keys that neither type nor edit
//   browser focused             -> the page owns its keys, except the client-menu shortcut
// A matched macro is emitted as macroFired(), an Any macro first; the app routes its command (app/inputrouter: client
// commands such as `.north`, `.winset` macro-set swaps, and everything else as a command line for the session). A key
// no macro binds does nothing, arrows included: stock DreamSeeker walks only through a binding, such as the default
// skin's `NORTH+REP` = `.north`.
//
// Reroute guard: the filter never reposts a key. It dispatches directly, acts only on the event delivered to the
// focus widget (so the window-to-widget double delivery is handled once), and ignores keys that arrive while it is
// dispatching (inDispatch_).
#pragma once
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <string>
#include <vector>
#include "skin/macro.h"
#include "skin/usermacros.h"

namespace fugue::shell {

class SkinWindow;

enum class Zone { Map, Output, Info, Input, Browser, Other };
const char* zoneName(Zone z);

// "CTRL+SHIFT+A" style spec for a key event (the macro naming of skin/macro.h), without +UP/+REP. A bare Shift, Ctrl or
// Alt is "" unless modifierKeys, which names it as a key of its own ("SHIFT", "CTRL+SHIFT").
std::string macroSpecFor(const QKeyEvent* e, bool modifierKeys = false);

class FocusFilter : public QObject {
    Q_OBJECT
public:
    explicit FocusFilter(QObject* parent = nullptr);
    void setMacros(const skin::MacroSet& set) {
        macros_ = set;
        have_ = true;
    }
    // The player's own lists (Client > Macros..., skin/usermacros.h): consulted only for keys the skin's set leaves unbound.
    void setUserMacros(const skin::MacroSet& game, const skin::MacroSet& global);
    // Client-only shortcut: .clientmenu press bindings in the user's game list, else global list,
    // else CTRL+M. These take priority over game macros, including Any/Any+UP.
    void setClientWindow(SkinWindow* window);
    static Zone zoneOf(QObject* o);  // walks parents for a "fugueZone" property, BrowserPane or QLineEdit
    bool eventFilter(QObject* o, QEvent* e) override;
    uint64_t fired() const { return fired_; }
    // Fires the release of every key this filter routed a press for and has not seen released. Called on application
    // deactivation, since a key released in another window never reaches the filter.
    void releaseHeld(const char* why);
    // A held key's REP binding repeats on the client's own clock: first 1.75 world ticks after the press, then once a
    // client frame, at most every 100 ms. A press that ran the key's non-REP binding starts repeating at the keyboard's
    // first auto-repeat instead. Only the newest held key whose hold repeats does.
    void setFrameTiming(qint64 tickNs, qint64 frameNs);
    qint64 nowMs() const { return clock_.elapsed(); }
    void repeatTick(qint64 nowMs);  // the repeat timer's tick
signals:
    void macroFired(const QString& anyKey, const QString& command, bool up);

private:
    skin::MacroSet macros_, userGame_, userGlobal_;
    QPointer<SkinWindow> clientWindow_;
    std::vector<skin::MacroKey> clientMenuKeys_;
    QSet<int> clientMenuHeld_;
    bool clientMenuEvent(QObject* receiver, QKeyEvent* event);
    bool have_ = false, inDispatch_ = false;
    uint64_t fired_ = 0;
    // Key to what its routed press saw. A release matches the modifiers its press carried, and is routed in the press's
    // zone even when it lands elsewhere, such as a popup menu or a dialog (see eventFilter).
    struct Held {
        Qt::KeyboardModifiers mods;
        Zone zone;
        qint64 downMs = 0;
        bool claimed = false, typed = false, awaitRepeat = false;
    };
    QHash<int, Held> pressMods_;
    std::vector<int> repeating_;  // held keys whose hold repeats, the newest last
    Qt::KeyboardModifiers modsNow_;
    QElapsedTimer clock_;
    QTimer repTimer_;
    qint64 tickMs_ = 100;
    enum class Ev { Press, AutoRepeat, Repeat, Release };
    bool route(Zone z, int key, Qt::KeyboardModifiers mods, Ev ev, const std::string& spec, bool emptyBar = true);
    void startRepeat(int key);
};

}  // namespace fugue::shell
