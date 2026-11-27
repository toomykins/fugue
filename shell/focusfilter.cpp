#include "shell/focusfilter.h"
#include "shell/paste.h"
#include <QApplication>
#include <algorithm>
#include <QKeySequence>
#include <QLineEdit>
#include <cstdio>
#include "shell/browserpane.h"
#include "shell/skinwindow.h"
#include <QMenu>

namespace fugue::shell {

const char* zoneName(Zone z) {
    switch (z) {
        case Zone::Map: return "map";
        case Zone::Output: return "output";
        case Zone::Info: return "info";
        case Zone::Input: return "input";
        case Zone::Browser: return "browser";
        default: return "other";
    }
}

std::string macroSpecFor(const QKeyEvent* e, bool modifierKeys) {
    std::string s;
    auto m = e->modifiers();
    if (m & Qt::ControlModifier) s += "CTRL+";
    if (m & Qt::ShiftModifier) s += "SHIFT+";
    if (m & Qt::AltModifier) s += "ALT+";
    std::string k;
    switch (e->key()) {
        case Qt::Key_Up: k = "NORTH"; break;
        case Qt::Key_Down: k = "SOUTH"; break;
        case Qt::Key_Left: k = "WEST"; break;
        case Qt::Key_Right: k = "EAST"; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: k = "RETURN"; break;
        case Qt::Key_Escape: k = "ESCAPE"; break;
        case Qt::Key_Backspace: k = "BACK"; break;
        case Qt::Key_Space: k = "SPACE"; break;
        case Qt::Key_Tab:
        case Qt::Key_Backtab: k = "TAB"; break;
        case Qt::Key_Delete: k = "DELETE"; break;
        case Qt::Key_Insert: k = "INSERT"; break;
        case Qt::Key_Home: k = "NORTHWEST"; break;  // stock names the navigation keys by compass point
        case Qt::Key_End: k = "SOUTHWEST"; break;
        case Qt::Key_PageUp: k = "NORTHEAST"; break;
        case Qt::Key_PageDown: k = "SOUTHEAST"; break;
        case Qt::Key_Clear: k = "CENTER"; break;  // numpad 5 without Num Lock
        default: {
            if (e->key() == Qt::Key_Meta) return "";
            if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt) {
                if (!modifierKeys) return "";
                k = e->key() == Qt::Key_Shift ? "SHIFT" : e->key() == Qt::Key_Control ? "CTRL" : "ALT";
                break;
            }
            k = QKeySequence(e->key()).toString().toUpper().toStdString();
        }
    }
    return s + k;
}

FocusFilter::FocusFilter(QObject* parent) : QObject(parent) {
    setUserMacros({}, {});
    clock_.start();
    repTimer_.setTimerType(Qt::PreciseTimer);
    repTimer_.setInterval(100);
    connect(&repTimer_, &QTimer::timeout, this, [this] { repeatTick(nowMs()); });
}

void FocusFilter::setClientWindow(SkinWindow* window) { clientWindow_ = window; }

void FocusFilter::setUserMacros(const skin::MacroSet& game, const skin::MacroSet& global) {
    userGame_ = game;
    userGlobal_ = global;
    clientMenuKeys_.clear();
    for (const auto* set : {&userGame_, &userGlobal_}) {
        for (const auto& entry : set->entries) {
            const auto& key = entry.key;
            if (entry.command != ".clientmenu" || entry.rep || key.up || entry.disabled || key.key == "ANY" ||
                key.key == "CTRL" || key.key == "SHIFT" || key.key == "ALT" || set->lookup(key, false) != &entry)
                continue;
            clientMenuKeys_.push_back(key);
        }
        if (!clientMenuKeys_.empty()) break;
    }
    if (clientMenuKeys_.empty()) {
        skin::MacroKey key;
        bool repeat = false;
        skin::parseMacroName("CTRL+M", key, repeat);
        clientMenuKeys_.push_back(key);
    }
}

bool FocusFilter::clientMenuEvent(QObject* receiver, QKeyEvent* event) {
    const int key = event->key();
    // Consume the whole key lifetime, even if focus/modifiers/configuration changed while the popup was open.
    if (clientMenuHeld_.contains(key)) {
        if (event->type() == QEvent::KeyRelease && !event->isAutoRepeat()) clientMenuHeld_.remove(key);
        event->accept();
        return true;
    }
    if (!clientWindow_ || event->type() == QEvent::KeyRelease) return false;
    auto* widget = qobject_cast<QWidget*>(receiver);
    if (!widget || QApplication::activeModalWidget()) return false;
    bool inClient = widget->window() == clientWindow_;
    for (QObject* p = widget; p; p = p->parent())
        if (p == clientWindow_->clientMenu()) {
            inClient = true;
            break;
        }
    if (!inClient || event->modifiers().testFlag(Qt::MetaModifier)) return false;
    skin::MacroKey chord;
    bool repeat = false;
    if (!skin::parseMacroName(macroSpecFor(event), chord, repeat) ||
        std::find(clientMenuKeys_.begin(), clientMenuKeys_.end(), chord) == clientMenuKeys_.end())
        return false;
    event->accept();
    if (event->type() == QEvent::KeyPress && !event->isAutoRepeat()) {
        clientMenuHeld_.insert(key);
        clientWindow_->toggleClientMenu();
    }
    return true;
}

void FocusFilter::setFrameTiming(qint64 tickNs, qint64 frameNs) {
    tickMs_ = tickNs > 0 ? tickNs / 1'000'000 : 100;
    qint64 frame = frameNs > 0 ? frameNs / 1'000'000 : tickMs_;
    repTimer_.setInterval(int(std::min<qint64>(frame, 100)));
}

void FocusFilter::startRepeat(int key) {
    repeating_.erase(std::remove(repeating_.begin(), repeating_.end(), key), repeating_.end());
    repeating_.push_back(key);
    if (!repTimer_.isActive()) repTimer_.start();
}

void FocusFilter::repeatTick(qint64 now) {
    while (!repeating_.empty() && !pressMods_.contains(repeating_.back())) repeating_.pop_back();
    if (repeating_.empty()) {
        repTimer_.stop();
        return;
    }
    int key = repeating_.back();
    const Held h = pressMods_.value(key);
    if (now - h.downMs < tickMs_ * 7 / 4) return;
    QKeyEvent synth(QEvent::KeyPress, key, modsNow_);
    route(h.zone, key, modsNow_, Ev::Repeat, macroSpecFor(&synth, true));
}

// The input bar's keys (stock DreamSeeker 516): whether a key's own binding fires there, or the text field takes it.
// A key that types takes its release too; any other key the field took releases as if pressed with no modifiers.
static bool typesText(int key) {
    switch (key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Insert:
        case Qt::Key_Delete:
        case Qt::Key_Space:
        case Qt::Key_Backspace:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
        case Qt::Key_Escape:
        case Qt::Key_Pause: return false;
        default: return key < Qt::Key_F1 || key > Qt::Key_F35;
    }
}

static bool inputBarFires(int key, Qt::KeyboardModifiers mods, bool empty) {
    const bool ctrl = mods & Qt::ControlModifier, alt = mods & Qt::AltModifier;
    switch (key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Insert: return !ctrl;
        case Qt::Key_Delete: return !ctrl && empty;
        case Qt::Key_Space:
        case Qt::Key_Backspace:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt: return empty;
        case Qt::Key_Z:
        case Qt::Key_C:
        case Qt::Key_X:
        case Qt::Key_V: return alt && !ctrl;  // undo, copy, cut, paste
        default: return !typesText(key) || ctrl || alt;
    }
}

Zone FocusFilter::zoneOf(QObject* o) {
    // A client dialog that takes key names as text (the macro editor, shell/macrodialog.h) owns its keys, so typing F1
    // into its Key field does not fire the skin's F1.
    for (QObject* p = o; p; p = p->parent())
        if (p->property("fugueZone").toString() == "other") return Zone::Other;
    for (; o; o = o->parent()) {
        if (qobject_cast<BrowserPane*>(o)) return Zone::Browser;
        if (o->property("fugueZone").toString() == "map") return Zone::Map;
        if (qobject_cast<QLineEdit*>(o)) return Zone::Input;
        QVariant z = o->property("fugueZone");
        if (z.isValid()) return z.toString() == "output" ? Zone::Output : Zone::Info;
    }
    return Zone::Other;
}

// The macro a release fires is named by the key and the modifiers its press carried, so a CTRL+A+UP binding still
// fires when Ctrl is released first.
static std::string releaseSpecFor(const QKeyEvent* k, Qt::KeyboardModifiers pressMods) {
    std::string base = macroSpecFor(k, true);
    size_t plus = base.rfind('+');
    std::string key = plus == std::string::npos ? base : base.substr(plus + 1);
    if (k->key() == Qt::Key_Plus) key = "+";
    return std::string((pressMods & Qt::ControlModifier) ? "CTRL+" : "") +
           ((pressMods & Qt::ShiftModifier) ? "SHIFT+" : "") + ((pressMods & Qt::AltModifier) ? "ALT+" : "") + key;
}

bool FocusFilter::eventFilter(QObject* o, QEvent* ev) {
    if (ev->type() == QEvent::ApplicationDeactivate) {
        clientMenuHeld_.clear();
        releaseHeld("application deactivated");
        return false;
    }
    if (ev->type() != QEvent::KeyPress && ev->type() != QEvent::KeyRelease && ev->type() != QEvent::ShortcutOverride)
        return false;
    if (inDispatch_) return false;  // reroute guard: keys during our own dispatch
    auto* k = static_cast<QKeyEvent*>(ev);
    // Before Qt shortcuts, game macros and page/editor delivery. No QShortcut registration to lose on rebuild.
    if (clientMenuEvent(o, k)) return true;
    if (ev->type() == QEvent::ShortcutOverride) return false;
    modsNow_ = k->modifiers();
    const Qt::KeyboardModifier own = k->key() == Qt::Key_Shift     ? Qt::ShiftModifier
                                     : k->key() == Qt::Key_Control ? Qt::ControlModifier
                                     : k->key() == Qt::Key_Alt     ? Qt::AltModifier
                                                                   : Qt::NoModifier;
    modsNow_.setFlag(own, ev->type() == QEvent::KeyPress);
    // A release belongs to its press, wherever it lands. Movement is a held-key state machine on the server (a press
    // starts a move, the release stops it), so a routed press without a routed release leaves the mob walking. A popup
    // takes the keyboard, so a key released while a menu is open reaches the menu, not the focus widget. The release
    // is therefore routed in the zone its press was routed in, whether it reaches the focus widget or the active
    // popup. The pressMods_ entry is taken on the first copy, so the double delivery still fires it once.
    if (ev->type() == QEvent::KeyRelease && !k->isAutoRepeat() && pressMods_.contains(k->key()) &&
        (o == QApplication::focusWidget() ||
         (QApplication::activePopupWidget() && o == QApplication::activePopupWidget()))) {
        Held h = pressMods_.take(k->key());
        if (h.typed) return false;
        Zone here = o == QApplication::focusWidget() ? zoneOf(o) : Zone::Other;
        bool routed = route(h.zone, k->key(), k->modifiers(), Ev::Release, releaseSpecFor(k, h.mods));
        return routed && here != Zone::Browser && here != Zone::Other;
    }
    if (o != QApplication::focusWidget()) return false;  // reroute guard: only the focus widget's copy
    Zone z = zoneOf(o);
    if (z == Zone::Browser || z == Zone::Other) {  // the page owns its keys
        std::string sp = macroSpecFor(k);
        if (ev->type() == QEvent::KeyPress && !sp.empty())
            std::fprintf(stderr, "[focus] zone=%s key=%s -> page owns it (not routed)\n", zoneName(z), sp.c_str());
        return false;
    }
    // A paste chord in a paste-wired editor belongs to the editor (shell/paste.h), so a skin binding on CTRL+V does not take it.
    if (ev->type() == QEvent::KeyPress && z == Zone::Input && isPasteWired(qobject_cast<QWidget*>(o)) &&
        isPasteChord(k))
        return false;
    std::string spec = macroSpecFor(k, true);
    if (spec.empty() || spec.back() == '+') return false;
    // An auto-repeat release is not a release. X11 auto-repeat sends release and press pairs while a key is held, and
    // Qt marks both isAutoRepeat(). Routing the repeat's release as +UP would stop held-key movement on every repeat,
    // so only the final, real release (isAutoRepeat() false) is a key-up.
    if (ev->type() == QEvent::KeyRelease && k->isAutoRepeat()) return true;
    if (ev->type() == QEvent::KeyRelease) return route(z, k->key(), k->modifiers(), Ev::Release, spec);
    if (k->isAutoRepeat()) {  // a key a macro took stays taken; REP bindings repeat on repTimer_
        auto h = pressMods_.find(k->key());
        if (h != pressMods_.end() && h->awaitRepeat) {
            h->awaitRepeat = false;
            h->downMs = nowMs();
            startRepeat(k->key());
            route(h->zone, k->key(), k->modifiers(), Ev::Repeat, spec);
        }
        return (h != pressMods_.end() && h->claimed) || route(z, k->key(), k->modifiers(), Ev::AutoRepeat, spec);
    }
    pressMods_[k->key()] = Held{k->modifiers(), z, nowMs()};
    auto* bar = qobject_cast<QLineEdit*>(o);
    return route(z, k->key(), k->modifiers(), Ev::Press, spec, !bar || bar->text().isEmpty());
}

void FocusFilter::releaseHeld(const char* why) {
    // A key released while another application has the keyboard never reaches this filter, so a held walk key would
    // stay down on the server. Release everything still held, as if let go now.
    auto held = pressMods_;
    pressMods_.clear();
    repeating_.clear();
    modsNow_ = Qt::NoModifier;
    for (auto it = held.cbegin(); it != held.cend(); ++it) {
        QKeyEvent synth(QEvent::KeyRelease, it.key(), Qt::NoModifier);
        std::fprintf(stderr, "[focus] %s: releasing held key %s\n", why, macroSpecFor(&synth, true).c_str());
        route(it.value().zone, it.key(), it.value().mods, Ev::Release, releaseSpecFor(&synth, it.value().mods));
    }
}

bool FocusFilter::route(Zone z, int key, Qt::KeyboardModifiers mods, Ev ev, const std::string& spec, bool emptyBar) {
    const bool up = ev == Ev::Release, repeat = ev == Ev::Repeat || ev == Ev::AutoRepeat;
    if (spec.empty()) return false;
    bool own = ev != Ev::AutoRepeat, any = !repeat;
    if (z == Zone::Input) {
        // Any fires there only for a key that neither types nor edits, and not while Ctrl is held.
        bool fkey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
        bool nav = (key >= Qt::Key_Home && key <= Qt::Key_PageDown) || key == Qt::Key_Insert || key == Qt::Key_Escape ||
                   key == Qt::Key_Tab || key == Qt::Key_Pause;
        if (ev == Ev::Press && !inputBarFires(key, mods, emptyBar)) {
            own = false;
            if (auto h = pressMods_.find(key); h != pressMods_.end()) {
                h->typed = typesText(key);
                h->mods = Qt::NoModifier;
            }
        }
        if (ev == Ev::Repeat && !inputBarFires(key, mods, emptyBar))
            own = false;  // a held key pauses while Ctrl is down
        any = any && !(mods & Qt::ControlModifier) && (fkey || nav);
        if (!own && !any) return false;
    }
    skin::MacroKey mk;
    bool rep = false;
    if (!skin::parseMacroName(spec, mk, rep)) return false;
    mk.up = up;
    const bool modifierKey = key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt;  // never repeats
    if (modifierKey) mk.ctrl = mk.shift = mk.alt = false;
    skin::MacroKey held = mk;
    held.ctrl = (mods & Qt::ControlModifier) || key == Qt::Key_Control;
    held.shift = (mods & Qt::ShiftModifier) || key == Qt::Key_Shift;
    held.alt = (mods & Qt::AltModifier) || key == Qt::Key_Alt;
    const skin::MacroSet* set = have_ ? &macros_ : nullptr;
    // The skin's set first; the player's lists only for a key it leaves unbound (skin/usermacros.h).
    const skin::MacroEntry* anyMe = any ? skin::lookupLayeredAny(set, userGame_, userGlobal_, held, false) : nullptr;
    const skin::MacroEntry* ownMe = nullptr;
    if (own && ev == Ev::Press) {
        skin::HeldMatch m = skin::matchLayered(set, userGame_, userGlobal_, mk);
        ownMe = m.press ? m.press : m.rep;
        auto h = pressMods_.find(key);
        if (h != pressMods_.end()) {
            h->claimed = ownMe;
            h->awaitRepeat = m.rep && m.press && !modifierKey;
        }
        if (m.rep && !m.press && !modifierKey) startRepeat(key);
    } else if (own) {
        ownMe = skin::lookupLayered(set, userGame_, userGlobal_, mk, repeat);
    }
    if (up) repeating_.erase(std::remove(repeating_.begin(), repeating_.end(), key), repeating_.end());
    if (!anyMe && !ownMe) {
        if (ev != Ev::Repeat)
            std::fprintf(stderr, "[focus] zone=%s key=%s%s -> no macro (passes on)\n", zoneName(z), spec.c_str(),
                         up ? "+UP" : "");
        return false;
    }
    inDispatch_ = true;
    for (const skin::MacroEntry* me : {anyMe, ownMe}) {
        if (!me) continue;
        fired_++;
        std::string name = me == anyMe ? skin::anyKeyName(mk.key) : "";
        std::fprintf(stderr, "[focus] zone=%s key=%s%s -> MACRO \"%s\"\n", zoneName(z), spec.c_str(), up ? "+UP" : "",
                     me->command.c_str());
        emit macroFired(QString::fromStdString(name), QString::fromStdString(me->command), up);
    }
    inDispatch_ = false;
    return ownMe || z != Zone::Input;  // Any leaves a command-bar key to the text field
}

}  // namespace fugue::shell
