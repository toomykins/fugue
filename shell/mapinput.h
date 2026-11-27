#pragma once
#include <QMargins>
#include <QMetaType>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <cstdint>
#include <optional>
#include <vector>
#include "proto/enc_c2s_0002_input.h"
#include "shell/mapsprite.h"

namespace fugue::model { struct World; }
namespace fugue::shell {
struct VisibilityFrame;
class CompositionPlan;
// client.dir: the map is drawn with this world direction up. Tiles and pixel offsets turn, icons stay upright on the
// bottom-left of their tile and show their direction turned the same way; transforms and the HUD do not turn, and the
// HUD is laid out on the turned view. SOUTH, EAST and WEST turn; anything else draws as NORTH (stock 516.1689,
// stock-parity-world client-dir-*).
struct MapTurn {
    int dir = 1;
    explicit MapTurn(int clientDir) : dir(clientDir == 2 || clientDir == 4 || clientDir == 8 ? clientDir : 1) {}
    bool turned() const { return dir != 1; }
    // The presented rectangle of an unturned view and back: the same centre, sides swapped for EAST and WEST.
    QRect rect(QRect r) const {
        return dir == 4 || dir == 8 ? QRect(r.x() + (r.width() - r.height()) / 2, r.y() + (r.height() - r.width()) / 2,
                                            r.height(), r.width()) : r;
    }
    // A point (pixel corner, y down) of the unturned view `world` in the presented view, and back.
    QPoint point(QPoint p, QRect world) const {
        const QPoint q = p - world.topLeft(), o = rect(world).topLeft();
        return o + (dir == 2 ? QPoint(world.width() - q.x(), world.height() - q.y()) :
                    dir == 4 ? QPoint(q.y(), world.width() - q.x()) :
                    dir == 8 ? QPoint(world.height() - q.y(), q.x()) : q);
    }
    QPoint back(QPoint p, QRect world) const {
        const QPoint q = p - rect(world).topLeft();
        return world.topLeft() + (dir == 2 ? QPoint(world.width() - q.x(), world.height() - q.y()) :
                                  dir == 4 ? QPoint(world.width() - q.y(), q.x()) :
                                  dir == 8 ? QPoint(q.y(), world.height() - q.x()) : q);
    }
    // The bottom-left corner of an icon whose unturned bottom-left corner is `p`, on a grid of `tile` pixels.
    QPoint bottomLeft(QPoint p, QRect world, int tile) const {
        return point(p + (dir == 2 ? QPoint(tile, -tile) : dir == 4 ? QPoint(0, -tile) : dir == 8 ? QPoint(tile, 0) : QPoint()),
                     world);
    }
    // The direction an icon shows for its world direction.
    int icon(int d) const {
        if (dir == 1) return d;
        static constexpr int south[4] = {2, 1, 8, 4}, east[4] = {8, 4, 1, 2}, west[4] = {4, 8, 2, 1};
        const int* to = dir == 2 ? south : dir == 4 ? east : west;
        int out = 0;
        for (int bit = 0; bit < 4; ++bit) if (d & (1 << bit)) out |= to[bit];
        return out;
    }
};
struct MapGeometry {
    QRect region; // world tiles, zero-based, y increases north
    QSize world;
    int tile = 0, z = 0;
    uint64_t session = 0;
    // Whole tiles the scene has around the view, for screen objects placed outside it (ScreenLoc::outside).
    QMargins hud;
    int dir = 1; // client.dir as MapTurn draws it; `region` and the sprites' cells stay world tiles
    // Worker unified-scene evidence. When present, turf hits require a rendered
    // sprite; a lit cell alone must not manufacture a background target.
    std::shared_ptr<const VisibilityFrame> visibility;
    bool operator==(const MapGeometry&) const = default;
};
struct RelayHit;
struct PlaneMousePath;
struct MapPointer {
    // Released: a button release that names nothing and sends nothing; it only clears the hover record, as an Up does.
    enum Kind { Down, Up, Click, DoubleClick, Wheel, Hover, Leave, Drag, Drop, Released } kind = Click;
    proto::enc::ClickCmd click;
    int z = 0;
    uint64_t session = 0;
    // Optional GUI surface lifetime fence, independent of the model/session.
    // Destruction or rebinding of a MAP invalidates already queued input too.
    std::optional<std::weak_ptr<const uint8_t>> surface;
    uint16_t atomKey = 0;
    bool haveAtomKey = false;
    bool screen = false; // immutable rendered HUD hit; worker validates HUD membership/key
    bool filteredPlane = false; // admitted stock-filter original-source endpoint
    bool masterFallback = false; // only stock-verified Down/Up/Click, never menu ownership
    bool boundedFilterInput = false; // default MAP only; also gates independent menu rectangles
    bool fromDefaultMap = false; // picked on, or leaving, the default MAP: its hover names feed the status bar
    std::shared_ptr<const VisHit> visHit;
    std::shared_ptr<const RelayHit> relayHit;
    std::shared_ptr<const PlaneMousePath> planeMousePath;
    uint32_t imageId = 0;
    uint64_t imageIdentity = 0, targetIdentity = 0, hudIdentity = 0;
    QSize turfDimensions;
    QPoint scenePixel;
    QRect renderedSource; // viewport pivot/clipping for plane-surface input
    // The view in the scene: world atoms take the pointer only there, screen objects anywhere. Null: everywhere.
    QRect worldView;
    bool insideScene = false;
    // Implicitly shared last-rendered evidence for bounded worker stacked menus.
    // Never consult a newer prepared scene when expanding a right-click.
    std::shared_ptr<const VisibilityFrame> visibility;
    MapSprites renderedScene;
    std::shared_ptr<const CompositionPlan> composition;
    // Press-time rendered identity/location. Never replace it with a later pick.
    std::shared_ptr<const MapPointer> source;
    bool backgroundHidden = false; // no new press/hover, but a null gesture destination with coordinates
    bool outside = false; // grabbed pointer released outside this MAP
    bool dragCell = false; // outside every window, straight from the map: click's screen cell is the last one dragged over
    bool closedPopup = false; // Down: the right press that closed the verb menu, consumed whatever the flag
    QPoint global;
    float dx = 0, dy = 0;
    // A hover pick over the stat panel (shell/statview.h statHoverPointer): click.over the row's atom or null over
    // the panel's empty space, click.locStr the selected tab's name, click.control the INFO control. infoIcon: the row
    // draws an icon (params carry icon-x/icon-y); infoAppid: the row's appearance, for the hover gate.
    bool info = false, infoIcon = false;
    uint32_t infoAppid = 0;
};
struct RelayHit {
    MapPointer producer, consumer;
    QString name;
    std::shared_ptr<const std::vector<uint32_t>> consumerPath;
    // Plane-backed sources retain the selected master and the member supplying
    // original ink. Endpoint snapshots contain no second scene or nested relay.
    std::optional<int> plane;
    std::shared_ptr<const MapPointer> ink;
    uint32_t producerAppearance = 0, consumerAppearance = 0, inkAppearance = 0;
};
// Bounded viewport PASS_MOUSE route to original world/HUD ink. Flat endpoints
// avoid recursive scene ownership; worker revalidates every hop and typed leaf.
struct PlaneMousePath {
    struct Hop {
        MapPointer master, relay;
        QString name;
        int plane = 0;
        uint32_t masterAppearance = 0, relayAppearance = 0;
    };
    std::vector<Hop> hops;
    uint32_t leafAppearance = 0;
};
// Logical widget coordinates; scale and letterboxing match the renderer. Input
// uses the geometry of the last rendered frame, never the worker's newer eye.
std::optional<MapPointer> mapPointerAt(QPointF position, QSizeF widgetSize, QSize view,
                                     QRect source, const MapGeometry& geometry,
                                     const QString& control, bool defaultMap, const MapSprites& sprites = {});
// Explicit last-rendered destination and submitted plan. The optional plan-less
// form is for synthetic callers/tests and assumes renderable float surfaces;
// MapWidget always supplies its successfully submitted plan.
std::optional<MapPointer> mapPointerAtPresentation(QPointF position, QRectF destination, QSize view,
                                     QRect source, const MapGeometry& geometry,
                                     const QString& control, bool defaultMap, const MapSprites& sprites = {},
                                     std::shared_ptr<const CompositionPlan> composition = {});
// Rectangle stack, topmost first, deduplicated by named atom, at most 128 entries.
// Each returned reference must still pass worker lifetime validation.
QVector<MapPointer> mapPointerStack(const MapPointer& pointer);
// What a click on a sprite names (MapPointer::click.over): the turf of its cell, a copied turf, or its atom.
proto::enc::AtomRef mapSpriteRef(const MapSprite& sprite);
Bytes mapPointerBody(const MapPointer& pointer);
bool mapPointerWanted(MapPointer::Kind kind, const proto::enc::MouseHooks& hooks, bool wheel);
// Worker routing only; Open still requires surface/session and per-candidate
// validation. Popup gestures never also dispatch ordinary mouse hooks.
enum class MapPopupAction { None, Open, Consume };
// client.show_popup_menus is read at the right press and again at its release. pressSent: this gesture's press went to
// the wire, so its release finishes as MouseUp and Click whatever the flag is by then.
MapPopupAction mapPopupAction(const MapPointer&, bool popupMenus, bool pressSent = false);
// Whether a button release clears the hover record (MapHover::released), given what mapPopupAction made of it. Any
// release does, a click's, a drop's or a cancelled gesture's, but not a right release the verb-menu path takes: with
// client.show_popup_menus on, the record outlives a right click, whether a menu opens or not (stock 516.1689,
// tests/data/hover-world extramenu mescsame, xboth rfloorleave).
inline bool mapReleaseForgetsHover(MapPopupAction popup) { return popup == MapPopupAction::None; }
// What a verb menu closing reports (stock 516.1689, tests/data/hover-world extramenu and round3; 516.1684
// popupmenu-world part C). The pointer is picked again at once, after Escape and after a pick from the menu alike:
// nothing if it is where the record has it, a move report elsewhere on the same atom, an over report anywhere else.
// While the right button is held (a right press elsewhere closed the menu) the report carries right=1, while the middle
// button is held (the menu took its press and stayed open, then Escape closed it) middle=1. While the left button is
// held (a left press closed it) nothing: that press reaches the map as a press, and its release clears the record.
// With the pointer over no map (overMap false: outside the window, over its background) nothing either, and the
// record is kept: the next move onto the map is weighed against what it was over before the menu opened.
enum class MapMenuClosed { Pick, PickRight, PickMiddle, Nothing };
inline MapMenuClosed mapMenuClosed(bool leftHeld, bool rightHeld, bool middleHeld, bool overMap) {
    return leftHeld || !overMap ? MapMenuClosed::Nothing
           : rightHeld  ? MapMenuClosed::PickRight
           : middleHeld ? MapMenuClosed::PickMiddle
                        : MapMenuClosed::Pick;
}

// Worker-only validation and hover subscription state. No model access in the presenter.
bool mapPointerCurrent(const MapPointer&, const model::World&, uint64_t session);
// What a pointer names in the model, for hover flags, menus and menu picks alike: a vis_contents member is its own
// atom or its own turf (on its own z), a HUD hit its HUD record, anything else the atom or the cell on the map's z.
// Says nothing about lifetime; mapPointerCurrent does.
struct MapPointerTarget {
    enum Kind { Atom, Hud, Turf } kind = Atom;
    uint32_t atom = 0; // Atom: the World::atoms key (registry bit set for a mob). Hud: the HUD ref
    std::array<uint16_t,3> cell{}; // Turf: zero-based
    std::optional<uint32_t> appid; // absent: the model no longer holds it
};
std::optional<MapPointerTarget> mapPointerTarget(const MapPointer&, const model::World&);
bool sameMapTarget(const MapPointer&, const MapPointer&);
// The name the main window's status bar shows for a hover pick: the plain name of what it names. Empty over a nameless
// atom or the map's empty space, and for anything but a Hover (a Leave: the pointer left the map or is on the letterbox).
std::string mapHoverName(const MapPointer&, const model::World&);
// The hover reports of one session, as stock DreamSeeker 516.1689 makes them (tests/data/hover-world). The client keeps
// one record of what the pointer was last over: the atom, the control, the secondary map's id and the pixel inside the
// screen tile. A pick that names another atom, control or map is an "over" report; one that only moves the pixel inside
// the tile, over an atom, is a "move" report; anything else (another icon-x/icon-y under a still pointer, a whole-tile
// jump inside one big atom) reports nothing. Both are the same message: the server runs MouseExited and MouseEntered
// when it names another atom than the last one it got, and MouseMove when it names the same one.
class MapHover {
public:
    // Stock picks the real pointer at most once per tick of about 1/64 s while it moves, whatever the world's tick_lag
    // (measured at 0.25, 0.5 and 10 ds) and with the system timer at 0.5 ms: Windows merges the pointer's moves between
    // the times stock reads them, so only the pointer's last position in a tick is picked, weighed against the record
    // as the tick found it. Stock does not hold reports itself: moves posted to its window, which Windows does not
    // merge, are each reported even a millisecond apart (tests/data/hover-world pacepost). Here the picks of one tick of
    // the steady clock are merged the same way, and the report goes out at the end of the tick.
    static constexpr int64_t kPeriodNs = 15'625'000;
    struct Gate {
        bool clientHover = false, clientMove = false; // the MouseSubs bits: any client hover proc, client/MouseMove
        uint32_t flags = 0;      // appearance flags of what the pick names
        uint32_t sentFlags = 0;  // of what the last report that went out named
    };
    // A pick of the pointer over a map or the stat panel (Hover) or the pointer leaving one (Leave), at nowNs on any
    // steady clock. Every map control and the stat panel share the record, and any control's leave clears it, whichever
    // control the record names. A stat panel pick (MapPointer::info) is never a move report: stock reports no pixel
    // step within a row, even with client/MouseMove (tests/data/hover-world panel.json). A stale pick (current false)
    // changes nothing. Gate::sentFlags are those of last(). A pick in the same tick as the picks before it (nothing
    // else between) stands for them: they are undone, with any report they queued.
    void update(const MapPointer&, bool current, const Gate&, int64_t nowNs);
    // A button released, by a click, a drop or a cancelled gesture: the record is cleared, so the next pick is an over
    // report even on the same atom, and a leave before any pick sends nothing.
    void released() { record_.reset(); run_.reset(); }
    // The next waiting report once its time has come (empty before, or when none waits). A report is due at the end
    // of the tick it was queued in, and those of one tick together: call again until it returns nothing.
    Bytes due(int64_t nowNs);
    std::optional<int64_t> dueAt() const { return dueNs_.empty() ? std::nullopt : std::optional<int64_t>(dueNs_.front()); }
    // What the last report that went out named (a Hover pointer), or null before any and after one naming no atom.
    const MapPointer* sent() const { return sent_ ? &*sent_ : nullptr; }
    // The same for the newest report, waiting or gone out: what the server will have been told last.
    const MapPointer* last() const;
private:
    struct Record {
        proto::enc::AtomRef over;
        std::string control, map;
        int px = 0, py = 0;
    };
    void queue(const MapPointer&, int64_t nowNs);
    std::optional<Record> record_;
    // The picks of the current tick: the record and Gate::sentFlags as the first found them, and whether the last
    // queued the report at the back of waiting_.
    struct Run {
        int64_t tick = 0;
        std::optional<Record> record;
        uint32_t sentFlags = 0;
        bool queued = false;
    };
    std::optional<Run> run_;
    // In order. A leave goes out after a report still waiting (tests/data/hover-world posted, and extra m2jump, where
    // the map left behind says so after the second map has named its atom).
    std::vector<MapPointer> waiting_;
    std::optional<MapPointer> sent_;
    std::vector<int64_t> dueNs_; // of each waiting report: the end of the tick it was queued in
};
// One verb of the right-click menu, under its atom's entry (group). icon: the picture beside the atom's name
// (shell/mapmenu.h mapMenuIcon), the same for every item of a group; null when the entry shows none.
struct MapMenuItem { int group = 0; QString atom, label; QImage icon; };
struct MapMenu { quint64 token = 0; QPoint global; QVector<MapMenuItem> items; };
}
Q_DECLARE_METATYPE(fugue::shell::MapMenu)
Q_DECLARE_METATYPE(fugue::shell::MapPointer)
