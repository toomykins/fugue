#include "render/mousechain.h"

#include <algorithm>

namespace fugue::render {
namespace enc = proto::enc;

const char* buttonName(int b) { return b == 1 ? "left" : b == 2 ? "middle" : b == 3 ? "right" : ""; }

enc::ClickCmd clickCmdFrom(const Naming& n, int x11Button, const enc::Mods& mods, const std::string& control) {
    enc::ClickCmd c;
    c.sub = enc::kMouseClick;
    c.over = n.overKind == 3   ? enc::turfRef(n.overX, n.overY)
             : n.overKind == 7 ? enc::AtomRef{enc::RefTurfId, n.overRef, 0, 0}
             : n.overKind == 2 ? enc::mobRef(n.overRef)
                               : enc::objRef(n.overRef);
    c.loc = n.locNull ? enc::nullRef() : enc::turfRef(n.locX, n.locY);
    c.control = control;
    c.iconX = n.iconX;
    c.iconY = n.iconY;
    c.visX = n.visX;
    c.visY = n.visY;
    c.hasVis = n.hasVis;
    c.screenX = n.screenX;
    c.screenY = n.screenY;
    c.screenPX = n.screenPX;
    c.screenPY = n.screenPY;
    c.button = buttonName(x11Button);
    c.mods = mods;
    return c;
}

std::string shortId(const std::string& control) {
    size_t dot = control.rfind('.');
    return dot == std::string::npos ? control : control.substr(dot + 1);
}

void onSecondary(enc::ClickCmd& c, const ChainEnv& env, bool fullId) {
    if (!env.scene || !env.scene->secondary) return;
    c.locStr = c.screenLocMap = fullId ? env.control : shortId(env.control);
}

namespace {
int64_t distSq(int ax, int ay, int bx, int by) { return int64_t(ax - bx) * (ax - bx) + int64_t(ay - by) * (ay - by); }

bool commandAt(const PointerEvent& e, const ChainEnv& env, enc::ClickCmd& cmd) {
    if (!env.scene) return false;
    Naming n;
    if (!nameMouse(*env.scene, e.imgX, e.imgY, n)) return false;
    cmd = clickCmdFrom(n, e.button, e.mods, env.control);
    return true;
}
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The screen cell of an image pixel that names nothing (the letterbox), extrapolated past the view.
void cellOf(const HitScene& s, int imgX, int imgY, int& sx, int& spx, int& sy, int& spy) {
    int fromBottom = s.pixH() - 1 - imgY;
    int tx = floorDiv(imgX, s.tileW), ty = floorDiv(fromBottom, s.tileH);
    sx = tx + 1 + s.screenX0;
    spx = imgX - tx * s.tileW + 1;
    sy = ty + 1 + s.screenY0;
    spy = fromBottom - ty * s.tileH + 1;
}

bool sameRef(const enc::AtomRef& a, const enc::AtomRef& b) {
    return a.kind == b.kind && (a.kind == enc::RefTurf ? a.x == b.x && a.y == b.y : a.ref == b.ref);
}
}  // namespace

std::vector<Bytes> wheelFrames(const PointerEvent& e, const ChainEnv& env, int deltaX, int deltaY) {
    std::vector<Bytes> out;
    if (!env.scene || (deltaX == 0 && deltaY == 0)) return out;
    const HitScene& s = *env.scene;
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0) return out;
    Naming n;
    bool named = nameMouse(s, e.imgX, e.imgY, n);
    if (!env.clientWheel && !(named && n.hit.wheel)) return out;

    enc::WheelCmd w;
    w.dx = float(deltaX);
    w.dy = float(deltaY);
    w.control = env.control;
    w.location = shortId(env.control);
    std::string p;
    int sx, spx, sy, spy;
    if (named) {
        enc::ClickCmd c = clickCmdFrom(n, 0, e.mods, env.control);
        w.over = c.over;
        p = "icon-x=" + std::to_string(n.iconX) + ";icon-y=" + std::to_string(n.iconY) + ";";
        if (n.hasVis) p += "vis-x=" + std::to_string(n.visX) + ";vis-y=" + std::to_string(n.visY) + ";";
        sx = n.screenX;
        spx = n.screenPX;
        sy = n.screenY;
        spy = n.screenPY;
    } else {
        cellOf(s, e.imgX, e.imgY, sx, spx, sy, spy);
    }
    std::string btn = buttonName(e.button);
    if (!btn.empty()) p += btn + "=1;button=" + btn + ";";
    if (e.mods.ctrl) p += "ctrl=1;";
    if (e.mods.shift) p += "shift=1;";
    if (e.mods.alt) p += "alt=1;";
    p += "screen-loc=" + enc::mapIdParam(w.location) + ":" + std::to_string(sx) + ":" + std::to_string(spx) + "," +
         std::to_string(sy) + ":" + std::to_string(spy);
    w.params = p;
    out.push_back(enc::encodeWheelCmd(w));
    return out;
}

std::vector<Bytes> HoverTracker::at(const PointerEvent& e, const ChainEnv& env, bool motion) {
    std::vector<Bytes> out;
    if (!env.scene) return out;
    const HitScene& s = *env.scene;
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0) return out;
    Naming n;
    enc::AtomRef over = enc::nullRef(), loc = enc::nullRef();
    std::string params, locStr = s.secondary ? shortId(env.control) : std::string();
    bool want = env.clientHover, moves = false;
    if (nameMouse(s, e.imgX, e.imgY, n)) {
        enc::ClickCmd c = clickCmdFrom(n, e.button == 3 ? 3 : 0, e.mods, env.control);
        onSecondary(c, env, /*fullId=*/false);
        over = c.over;
        loc = c.loc;
        params = enc::hoverParams(c);
        want = want || env.clientMove || n.hit.hover;
        moves = env.clientMove || n.hit.mouseMove;
    } else {
        int sx, spx, sy, spy;
        cellOf(s, e.imgX, e.imgY, sx, spx, sy, spy);
        params = "screen-loc=" + (locStr.empty() ? std::string() : enc::mapIdParam(locStr) + ":") + std::to_string(sx) +
                 ":" + std::to_string(spx) + "," + std::to_string(sy) + ":" + std::to_string(spy);
    }
    bool same = have_ && sameRef(over, over_) && control_ == env.control;
    bool send = same ? motion && moves && params != params_ : want || wanted_;
    if (!same) {
        over_ = over;
        control_ = env.control;
        wanted_ = want;
        have_ = true;
    }
    if (motion || !same) params_ = params;
    if (send) out.push_back(enc::encodeHover(over, loc, env.control, params, locStr));
    return out;
}

std::vector<Bytes> HoverTracker::leave() {
    std::vector<Bytes> out;
    if (wanted_) out.push_back(enc::encodeHoverLeave());
    have_ = wanted_ = false;
    params_.clear();
    return out;
}

void HoverTracker::forget() {
    have_ = false;
    params_.clear();
}

std::vector<Bytes> MouseChain::press(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!wired(e.button, env)) {
        note_ = "unwired";
        held_ = false;
        return out;
    }
    held_ = true;
    dragging_ = false;
    button_ = e.button;
    pressWinX_ = e.winX;
    pressWinY_ = e.winY;
    dragActive_ = dragMoved_ = false;

    // The double-click detector is window-pixel + button + time, independent of the hit.
    bool dbl = false;
    if (haveLast_ && e.button == lastBtn_ && e.tMs - lastAt_ <= kDblWindowMs && e.tMs >= lastAt_ &&
        distSq(e.winX, e.winY, lastWinX_, lastWinY_) < int64_t(kDragThresholdPx) * kDragThresholdPx) {
        dbl = true;
        haveLast_ = false;  // consumed: a third rapid press starts a fresh pair
    } else {
        haveLast_ = true;
        lastAt_ = e.tMs;
        lastWinX_ = e.winX;
        lastWinY_ = e.winY;
        lastBtn_ = e.button;
    }

    enc::ClickCmd cmd;
    if (!commandAt(e, env, cmd)) {
        note_ = "nohit";
        return out;
    }
    onSecondary(cmd, env, /*fullId=*/false);
    dragActive_ = true;
    src_ = cmd.over;
    srcLoc_ = cmd.loc;
    srcLocStr_ = cmd.locStr;
    srcControl_ = cmd.control;
    pressCmd_ = cmd;
    cellOk_ = true;
    cellX_ = cmd.screenX;
    cellY_ = cmd.screenY;
    pressTileW_ = std::max(1, env.scene->tileW);
    pressTileH_ = std::max(1, env.scene->tileH);
    out = enc::pressFrames(cmd, env.hooks);
    note_ = out.empty() ? "down(unsubscribed)" : "down";
    if (dbl && env.hooks.dblClick) {
        cmd.sub = enc::kMouseDblClick;
        onSecondary(cmd, env, /*fullId=*/true);
        out.push_back(enc::encodeMouseCmd(cmd));
        note_ = "down+dbl";
    }
    return out;
}

std::vector<Bytes> MouseChain::move(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!held_) {
        note_ = "idle";
        return out;
    }
    if (!dragging_) {
        if (distSq(e.winX, e.winY, pressWinX_, pressWinY_) < int64_t(kDragThresholdPx) * kDragThresholdPx) {
            note_ = "idle";
            return out;
        }
        dragging_ = true;
    }
    if (!dragActive_) {
        note_ = "nohit";
        return out;
    }
    dragMoved_ = true;  // a drag is marked whether or not drags are subscribed
    if (!env.hooks.drag) {
        note_ = "drag(unsubscribed)";
        return out;
    }
    enc::ClickCmd cmd;
    PointerEvent pe = e;
    pe.button = button_;
    if (env.offMap || !commandAt(pe, env, cmd)) {
        out = dragOff(pe, env, /*drop=*/false);
        note_ = out.empty() ? "drag(nohit)" : "drag(off)";
        return out;
    }
    cellOk_ = true;
    cellX_ = cmd.screenX;
    cellY_ = cmd.screenY;
    onSecondary(cmd, env, /*fullId=*/true);
    enc::DragCmd d;
    d.drop = false;
    d.src = src_;
    d.srcLoc = srcLoc_;
    d.srcLocStr = srcLocStr_;
    d.over = cmd.over;
    d.overLoc = cmd.loc;
    d.overLocStr = cmd.locStr;
    if (cmd.over.kind == enc::RefTurf && !cmd.locStr.empty()) {
        d.overLoc = cmd.over;
        d.overLocStr.clear();
    }  // a turf by cell is its own location
    d.srcControl = srcControl_;
    d.overControl = cmd.control;
    d.params = enc::clickParams(cmd);
    out.push_back(enc::encodeDragCmd(d));
    note_ = "drag";
    return out;
}

std::vector<Bytes> MouseChain::release(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!held_) {
        note_ = "idle";
        return out;
    }
    bool active = dragActive_, moved = dragMoved_;
    enc::AtomRef src = src_, srcLoc = srcLoc_;
    std::string srcCtl = srcControl_, srcLocStr = srcLocStr_;
    int btn = button_;
    held_ = dragging_ = dragActive_ = dragMoved_ = false;
    PointerEvent pe = e;
    pe.button = btn;
    enc::ClickCmd cmd;
    if (env.offMap || !commandAt(pe, env, cmd)) {
        if (active && moved) out = dragOff(pe, env, /*drop=*/true);
        note_ = out.empty() ? "nohit" : "drop(off)";
        return out;
    }
    enc::ClickCmd up = cmd;
    onSecondary(up, env, /*fullId=*/true);

    if (active && moved) {
        if (env.hooks.up) {
            up.sub = enc::kMouseUp;
            out.push_back(enc::encodeMouseCmd(up));
        }
        if (env.hooks.drop) {
            // The drop's over-location on a secondary map: a turf named by its cell is its own location; otherwise the
            // source's short id when the drop lands on the source's own map, else a turf by id itself and anything else
            // null. Its screen-loc carries the SOURCE map's short id.
            enc::DragCmd d;
            d.drop = true;
            d.src = src;
            d.srcLoc = srcLoc;
            d.srcLocStr = srcLocStr;
            d.over = cmd.over;
            d.overLoc = cmd.loc;
            if (env.scene && env.scene->secondary) {
                d.overLoc = enc::nullRef();
                if (cmd.over.kind == enc::RefTurf)
                    d.overLoc = cmd.over;
                else if (cmd.control == srcCtl)
                    d.overLocStr = srcLocStr;
                else if (cmd.over.kind == enc::RefTurfId)
                    d.overLoc = cmd.over;
            }
            cmd.screenLocMap = srcLocStr;
            d.srcControl = srcCtl;
            d.overControl = cmd.control;
            d.params = enc::clickParams(cmd);
            out.push_back(enc::encodeDragCmd(d));
            note_ = "drop";
        } else {
            note_ = "move-only";
        }
        return out;
    }
    out = enc::releaseFrames(up, env.hooks);
    note_ = "click";
    return out;
}

std::vector<Bytes> MouseChain::releaseUnpressed(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    enc::ClickCmd cmd;
    if (env.offMap || !commandAt(e, env, cmd)) {
        note_ = "nohit";
        return out;
    }
    onSecondary(cmd, env, /*fullId=*/true);
    out = enc::releaseFrames(cmd, env.hooks);
    note_ = "click(unpressed)";
    return out;
}

// A drag step or release naming nothing. Stock reports it with over null, three ways:
//   a map's empty area    the cell under the pointer, extrapolated; over_location "" on the default map, else the map's
//                         full id (a drop's: the source's short id on the source's own map, else null)
//   another control       that control (a popup's page: the popup's id), over_location "", no screen-loc
//   outside every window  the source control and over_location as over its empty area, at the last drag cell with the
//                         press's pixel; no screen-loc once a step went over another control
// The release's MouseUp and MouseDrop carry the press's icon-x/icon-y, except outside every window; off every map their
// screen-loc is the press's cell, unprefixed, and their control a null atom outside (the drop's over_control always).
std::vector<Bytes> MouseChain::dragOff(const PointerEvent& e, const ChainEnv& env, bool drop) {
    std::vector<Bytes> out;
    const std::string btn = buttonName(button_);
    const std::string held = enc::infoClickParams(btn, e.mods),
                      withIcon = enc::infoClickParams(btn, e.mods, true, pressCmd_.iconX, pressCmd_.iconY);
    auto at = [](const std::string& map, int x, int px, int y, int py) {
        return ";screen-loc=" + (map.empty() ? std::string() : enc::mapIdParam(map) + ":") + std::to_string(x) + ":" +
               std::to_string(px) + "," + std::to_string(y) + ":" + std::to_string(py);
    };
    const int ppx = std::clamp(pressCmd_.screenPX ? pressCmd_.screenPX : pressCmd_.iconX, 1, pressTileW_);
    const int ppy = std::clamp(pressCmd_.screenPY ? pressCmd_.screenPY : pressCmd_.iconY, 1, pressTileH_);
    const std::string srcFull = srcLocStr_.empty() ? std::string() : srcControl_;  // a secondary source's full id
    enc::DragCmd d;
    d.drop = drop;
    d.src = src_;
    d.srcLoc = srcLoc_;
    d.srcLocStr = srcLocStr_;
    d.srcControl = srcControl_;
    std::string upLoc, upControl, up;  // the MouseUp's loc string, control and params
    if (!env.offMap) {
        // A map with no scene yet (before its first picture) is its empty area, with no cell, named as a secondary map's.
        // Stock has no such state.
        const HitScene* s = env.scene;
        if (s && (s->cols <= 0 || s->rows <= 0 || s->tileW <= 0 || s->tileH <= 0)) s = nullptr;
        int sx = 0, spx = 0, sy = 0, spy = 0;
        if (s) cellOf(*s, e.imgX, e.imgY, sx, spx, sy, spy);
        const bool secondary = !s || s->secondary;
        const std::string full = secondary ? env.control : std::string();
        auto cell = [&](const std::string& map) { return s ? at(map, sx, spx, sy, spy) : std::string(); };
        d.overControl = env.control;
        if (drop) {
            if (secondary && env.control == srcControl_) d.overLocStr = srcLocStr_;
            d.params = withIcon + cell(srcLocStr_);
            upLoc = full;
            upControl = env.control;
            up = withIcon + cell(full);
        } else {
            cellOk_ = s != nullptr;
            cellX_ = sx;
            cellY_ = sy;
            d.overLocStr = full;
            d.overLocBlank = full.empty();
            d.params = held + cell(full);
        }
    } else if (!env.control.empty()) {
        if (drop) {
            d.overControlNull = true;
            d.params = up = withIcon + at({}, pressCmd_.screenX, ppx, pressCmd_.screenY, ppy);
            upControl = env.control;
        } else {
            cellOk_ = false;
            d.overLocBlank = true;
            d.overControl = env.control;
            d.params = held;
        }
    } else if (drop) {
        d.overControlNull = true;
        d.params = up = held + at({}, pressCmd_.screenX, ppx, pressCmd_.screenY, ppy);
    } else {
        d.overControl = srcControl_;
        d.overLocStr = srcFull;
        d.overLocBlank = srcFull.empty();
        d.params = held + (cellOk_ ? at(srcFull, cellX_, ppx, cellY_, ppy) : std::string());
    }
    if (drop && env.hooks.up) out.push_back(enc::encodeMouseNone(enc::kMouseUp, upLoc, upControl, up));
    if (!drop || env.hooks.drop) out.push_back(enc::encodeDragCmd(d));
    return out;
}

}  // namespace fugue::render
