// The mouse CHAIN: pointer press / motion / release on the map image -> the Command frames a stock DreamSeeker
// sends. Pure state machine (no Qt, no socket, no clock of its own): the caller supplies timestamps and the
// scene, and sends the returned bodies on the SESSION thread.
//
// Rules, as stock DreamSeeker 516 sends them:
//   press    MouseDown iff the world subscribed (hooks.down). If this press is the second half of a double-click
//            (same button, within kDblWindowMs of the previous press, within kDragThresholdPx of it), ONE extra
//            MouseDblClick frame follows the MouseDown - only if the world declared DblClick. An undeclared
//            DblClick is just a second ordinary click.
//   motion   while a button is held: nothing until the pointer is kDragThresholdPx away from the press (a smaller
//            movement is still a click, as stock DreamSeeker treats a pixel or two of jitter);
//            after that, one MouseDrag (sub 7, event 0x93) per motion IFF hooks.drag, naming the CURRENT over.
//   release  THREE outcomes: (1) no real movement -> MouseUp then Click (each iff subscribed);
//            (2) real drag, world wants MouseDrop -> MouseUp then MouseDrop, NO Click;
//            (3) real drag, world wants no drop -> MouseUp ALONE (DreamSeeker never falls back to Click).
//   buttons  left/middle always. The RIGHT button is answered locally (the verb popup) unless the map control declares
//            `right-click = true`; client.show_popup_menus = FALSE sends it like a left click. The flag is read twice: a
//            press with it FALSE sends MouseDown (the caller sets ChainEnv::rightClickToWire from the flag at that moment),
//            and the release decides the rest. A press that was sent finishes as any click (MouseUp, Click) whatever the flag
//            has become. A press that was not sent ends with MouseUp and Click, but no MouseDown (releaseUnpressed) when the
//            flag is FALSE at the release, and with the verb popup, at the release and only then, when it is still TRUE.
//   nothing under the pointer (hitAt fails): no frame for a press or a click; a drag step and the release that ends a
//            drag still report, with over null (dragOff): over a map's empty area (all of a map with no scene yet), another
//            control or window (ChainEnv::offMap), or outside every window.
//   wheel    one MouseWheel frame per notch (wheelFrames), iff the world declares client/MouseWheel (anywhere over the
//            map, the letterbox included, naming a null object when nothing is hit) or the atom under the pointer
//            carries the wheel flag (HitAtom::wheel). Not gated by the button hooks.
//   hover    HoverTracker: a sub-0x08 report when the atom under the pointer changes, and per new icon pixel over an atom
//            that takes MouseMove; nothing while a button is held (the caller's rule: MapWidget sends no hover then). A
//            popup swallows the motion over it, so when the verb popup closes the atom under the pointer is reported at once,
//            with right=1 while the right button is still down (PointerEvent::button = 3).
//   secondary map (HitScene::secondary): loc is the control's id as a string and screen-loc is prefixed with it - the
//            short id ("map2") for MouseDown, hover and the wheel, the full id ("main.map2") for MouseUp, Click and
//            DblClick and for a drag's over-location (a turf named by its cell: that turf). A drag's source location is
//            the source map's short id. A turf on another z than the default map's is named [07][turf id] everywhere.
// Deviation from DreamSeeker's drop path: the MouseUp that ends a drag is gated on hooks.up like every other MouseUp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/hittest.h"

namespace fugue::render {

struct PointerEvent {
    int imgX = 0, imgY = 0;  // pixel inside the rendered map image (top-left origin) - what the hit test takes
    int winX = 0, winY = 0;  // screen pixels, the same frame for every control - only differences matter (drag
                             // threshold, double-click radius)
    int button = 1;          // X11 numbering: 1 left, 2 middle, 3 right
    proto::enc::Mods mods;
    int64_t tMs = 0;  // any monotonic millisecond clock
};

struct ChainEnv {
    const HitScene* scene = nullptr;
    proto::enc::MouseHooks hooks = proto::enc::allMouseHooks();
    bool rightClickToWire = false;
    std::string control = "mapwindow.map";
    bool clientWheel = true;   // the MouseSubs second flags word's client/MouseWheel bit (true until MouseSubs arrives)
    bool clientHover = false;  // MouseSubs: client/MouseEntered, MouseExited or MouseMove
    bool clientMove = false;   // MouseSubs second word: client/MouseMove
    // A move or release over no map control: scene is unused and control is the control or window there ("" outside
    // every window).
    bool offMap = false;
};

// "main.map2" -> "map2".
std::string shortId(const std::string& control);
// A command named on a secondary map (env.scene->secondary): locStr and screenLocMap become the control's full or short
// id. A no-op on the default map.
void onSecondary(proto::enc::ClickCmd& c, const ChainEnv& env, bool fullId);
// "left" / "middle" / "right", or "" for the wheel and unknown buttons.
const char* buttonName(int x11Button);
// Naming -> the encoder's command (button/mods/control filled in).
proto::enc::ClickCmd clickCmdFrom(const Naming& n, int x11Button, const proto::enc::Mods& mods,
                                  const std::string& control);

// A wheel notch at e (imgX/imgY may lie outside the image: over the letterbox the cell is extrapolated, as stock does).
// e.button is the button held during the notch (0 none); deltaY > 0 is away from the user (stock: +120 per notch).
std::vector<Bytes> wheelFrames(const PointerEvent& e, const ChainEnv& env, int deltaX, int deltaY);

// What stock DreamSeeker 516 reports while the pointer moves over a map control with no button held. ONE tracker for all
// the map controls. Every report is the same sub-0x08 frame (proto::enc::encodeHover); the server tells Entered/Exited from
// Move by comparing it with the previous one. Over an atom O (null over the letterbox) on control C:
//   O or C differs from the last   sent iff O wants hover (client hover proc, or appearance flag 0x800) or the last O did.
//   O is the last O on C           sent iff this is pointer motion, O takes MouseMove (client/MouseMove, or flag 0x80000),
//                                  O is not null, and the params changed (one report per new icon pixel, unthrottled).
//   leave (any control)            the encodeHoverLeave frame iff the last O wanted hover; the next O is then new.
//   forget (a button press)        the next O is new, judged against whether the last O wanted hover.
class HoverTracker {
public:
    // motion=false: the picture changed under a still pointer, so only a change of atom is reported.
    std::vector<Bytes> at(const PointerEvent& e, const ChainEnv& env, bool motion = true);
    std::vector<Bytes> leave();
    void forget();

private:
    bool have_ = false, wanted_ = false;
    proto::enc::AtomRef over_;
    std::string control_, params_;
};

class MouseChain {
public:
    static constexpr int kDblWindowMs = 400;
    static constexpr int kDragThresholdPx = 4;

    // True when this button would go on the wire at all (false: right button without right-click=true).
    static bool wired(int button, const ChainEnv& env) {
        return button == 3 ? env.rightClickToWire : (button == 1 || button == 2);
    }

    std::vector<Bytes> press(const PointerEvent& e, const ChainEnv& env);
    std::vector<Bytes> move(const PointerEvent& e, const ChainEnv& env);
    std::vector<Bytes> release(const PointerEvent& e, const ChainEnv& env);
    // The release of a press that was not sent (the right button with client.show_popup_menus TRUE) when the flag is FALSE
    // at the release: MouseUp and Click for the atom there, with no MouseDown before them.
    std::vector<Bytes> releaseUnpressed(const PointerEvent& e, const ChainEnv& env);

    bool held() const { return held_; }
    bool dragging() const { return dragging_; }
    // What the last call decided, for logs and tests: "down" "dbl" "drag" "click" "drop" "move-only" "idle" "nohit" "unwired".
    const std::string& note() const { return note_; }

private:
    bool held_ = false, dragging_ = false;  // dragging_: the pointer left the press radius
    int button_ = 0, pressWinX_ = 0, pressWinY_ = 0;
    // Drag state: the pressed target, captured at press.
    bool dragActive_ = false, dragMoved_ = false;
    proto::enc::AtomRef src_, srcLoc_;
    std::string srcControl_, srcLocStr_;
    proto::enc::ClickCmd pressCmd_;          // what the press named, for the frames of a drag that ends naming nothing
    int pressTileW_ = 32, pressTileH_ = 32;  // the pressed scene's tile size
    bool cellOk_ = false;                    // the screen cell of the last drag step over a map (the press's to begin)
    int cellX_ = 0, cellY_ = 0;
    std::vector<Bytes> dragOff(const PointerEvent& e, const ChainEnv& env, bool drop);
    // Double-click detector: last press position, button and time.
    bool haveLast_ = false;
    int lastBtn_ = 0, lastWinX_ = 0, lastWinY_ = 0;
    int64_t lastAt_ = 0;
    std::string note_;
};

}  // namespace fugue::render
