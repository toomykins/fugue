// Session::snapshot: the JSON of the "world" object. The section list, shapes and sort keys are fixed, because the
// sha256 input of cells.tilesSHA depends on them.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "model/sha256.h"
#include "model/world.h"
#include "proto/g2_mapview.h"

namespace fugue::model {

namespace {

using canon::Json;
using canon::Object;

Json kvArray(const std::map<uint32_t, uint32_t>& m) {
    std::vector<Json> v;
    v.reserve(m.size());
    for (auto& [k, x] : m) v.push_back(Object().u("k", k).u("v", x).build());
    return canon::array(v);
}

Json u32Array(const std::vector<uint32_t>& xs) {
    std::vector<Json> v;
    v.reserve(xs.size());
    for (uint32_t x : xs) v.push_back(canon::u(x));
    return canon::array(v);
}

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

}  // namespace

const std::vector<std::string>& unimplementedSections() {
    static const std::vector<std::string> s = {};  // none
    return s;
}

namespace {
// Collects sections in insertion order so callers can get each section's own canonical text.
struct Sections {
    std::vector<std::pair<std::string, Json>> v;
    Sections& set(std::string_view n, Json j) {
        v.emplace_back(std::string(n), std::move(j));
        return *this;
    }
};
}  // namespace

std::string Session::snapshot() const {
    Object o;
    for (auto& [name, text] : sections()) o.set(name, text);
    return o.build();
}

// The viewport cell grid. A separate function so the map assembler can read it directly: going through the "cells"
// JSON would canonicalise every appearance and atom on each map frame. sections() builds its "cells" section from this.
CellGrid Session::cells() const {
    const World& w = w_;
    bool err = false;
    int cw = 0, ch = 0, ox = 0, oy = 0, vz = 1;
    bool haveZ = false;
    std::vector<uint32_t> cells;
    auto& mv = st_.mapView;
    auto& g2v = st_.get<proto::g2::MapView>();
    if (!w.bounds.have) {
        err = true;
    } else if (w.mapViewErrs == 0 && mv.winW > 0 && mv.winH > 0 && !w.tiles.empty()) {
        // The cumulative refresh grid.
        cw = mv.winW;
        ch = mv.winH;
        cells.resize(size_t(cw) * ch);
        for (int ly = 0; ly < ch; ly++)
            for (int lx = 0; lx < cw; lx++) {
                auto it = w.tiles.find({uint16_t(g2v.originX + lx), uint16_t(g2v.originY + ly), g2v.z});
                cells[size_t(ly) * cw + lx] = it == w.tiles.end() ? 0 : it->second;  // never-told stays 0
            }
        ox = g2v.originX + 1;
        oy = g2v.originY + 1;
        vz = int(g2v.z) + 1;
        haveZ = true;
    } else if (!w.grid.have) {
        err = true;
    } else {
        // WindowDims: 0x3e window field, else sqrt of the grid cell count, else 2*view+1.
        int wW = 0, wH = 0;
        bool ok = true;
        size_t n = w.grid.cells.size();
        size_t side = n ? size_t(std::sqrt(double(n))) : 0;
        if (w.bounds.winW > 0 && w.bounds.winH > 0) {
            wW = w.bounds.winW;
            wH = w.bounds.winH;
        } else if (n > 0 && side > 0 && side * side == n) {
            wW = wH = int(side);
        } else if (w.bounds.have && w.bounds.view > 0) {
            wW = wH = 2 * w.bounds.view + 1;
        } else
            ok = false;
        if (!ok) {
            err = true;
        } else {
            if (w.bounds.maxX > 0 && wW > w.bounds.maxX) wW = w.bounds.maxX;
            if (w.bounds.maxY > 0 && wH > w.bounds.maxY) wH = w.bounds.maxY;
            cw = wW;
            ch = wH;
            size_t want = size_t(cw) * ch;
            // DefaultTurfAppid: the unique appearance with layer == 2.0
            uint32_t turf = 0;
            int cnt = 0;
            for (auto& [id, a] : w.appearances)
                if (bitsToFloat(a.fields.layerBits) == 2.0f) {
                    turf = id;
                    cnt++;
                }
            cells.assign(want, 0);
            for (size_t i = 0; i < std::min(n, want); i++) {
                uint32_t c = w.grid.cells[i];
                if (c == 0xffffffffu && cnt == 1) c = turf;
                cells[i] = c;
            }
            ox = w.grid.x0 + 1;
            oy = w.grid.y0 + 1;
        }
    }
    if (err) {
        cw = ch = ox = oy = 0;
        cells.clear();
    }
    CellGrid g;
    g.err = err;
    g.w = cw;
    g.h = ch;
    g.originX = ox;
    g.originY = oy;
    g.viewZ = vz;
    g.haveZ = haveZ;
    g.appids = std::move(cells);
    return g;
}

std::vector<std::pair<std::string, std::string>> Session::sections() const {
    const World& w = w_;
    Sections world;

    // ---- meta
    Json se = Object()
                  .boolean("seen", w.sessionEnd.seen)
                  .u("reason", w.sessionEnd.reason)
                  .u("version", w.sessionEnd.version)
                  .str("text", w.sessionEnd.text)
                  .build();
    world.set("meta", Object()
                          .str("name", w.name)
                          .boolean("haveBounds", w.bounds.have)
                          .i("maxX", w.bounds.maxX)
                          .i("maxY", w.bounds.maxY)
                          .i("view", w.bounds.view)
                          .i("winW", w.bounds.winW)
                          .i("winH", w.bounds.winH)
                          .i("viewW", w.bounds.viewW)
                          .i("viewH", w.bounds.viewH)
                          .i("tickNanos", w.tickNanos > 0 ? w.tickNanos : kDefaultTickNanos)
                          .i("mapViewErrs", w.mapViewErrs)
                          .i("cellRefreshErrs", w.cellRefreshErrs)
                          .u("clientAppid", w.clientAppid)
                          .boolean("haveClientAppid", w.haveClientAppid)
                          .str("chatControl", w.chatControl)
                          .str("hostMessage", w.hostMessage)
                          .set("sessionEnd", se)
                          .build());

    // ---- eye
    world.set("eye", Object()
                         .boolean("haveEye", w.haveEye)
                         .i("x", w.haveEye ? w.eyeX : 0)
                         .i("y", w.haveEye ? w.eyeY : 0)
                         .set("glideSize", canon::f32(w.eyeGlideSizeBits))
                         .set("stepSize", canon::f32(w.stepSizeBits))
                         .boolean("longGlide", w.eyeLongGlide)
                         .u("seeInvisible", w.seeInvisible)
                         .boolean("haveSeeInvisible", w.haveSeeInvisible)
                         .u("sight", w.sight)
                         .boolean("haveSight", w.haveSight)
                         .build());

    // ---- self
    world.set("self", Object().boolean("have", w.haveSelf).u("ref", w.selfRef).build());

    // ---- appearances (fields = the decoded record's canonical JSON)
    {
        std::vector<Json> v;
        v.reserve(w.appearances.size());
        for (auto& [id, a] : w.appearances)
            v.push_back(Object().u("appid", id).set("fields", proto::toJson(a.fields)).build());
        world.set("appearances", canon::array(v));
    }

    // ---- atoms
    {
        std::vector<Json> v;
        v.reserve(w.atoms.size());
        for (auto& [id, a] : w.atoms) {
            std::vector<uint32_t> vc;
            auto it = w.visContents.find(id);
            if (it != w.visContents.end()) vc = it->second;
            v.push_back(Object()
                            .u("id", id)
                            .u("appid", a.appid)
                            .i("x", a.x)
                            .i("y", a.y)
                            .i("z", a.z)
                            .boolean("hasPos", a.hasPos)
                            .i("dir", a.dir)
                            .i("pixelX", a.pixelX)
                            .i("pixelY", a.pixelY)
                            .i("pixelW", a.pixelW)
                            .i("pixelZ", a.pixelZ)
                            .i("boundX", a.boundX)
                            .i("boundY", a.boundY)
                            .i("boundW", a.boundW)
                            .i("boundH", a.boundH)
                            .boolean("hasBounds", a.hasBounds)
                            .i("stepX", a.stepX)
                            .i("stepY", a.stepY)
                            .set("stepSize", canon::f32(a.stepSizeBits))
                            .boolean("hasStep", a.hasStep)
                            .i("visCount", a.visCount)
                            .set("visContents", u32Array(vc))
                            .build());
        }
        world.set("atoms", canon::array(v));
    }

    // ---- cells
    {
        CellGrid g = cells();
        bool err = g.err;
        int cw = g.w, ch = g.h, ox = g.originX, oy = g.originY;
        const std::vector<uint32_t>& cells = g.appids;

        Sha256 h;
        for (auto& [k, appid] : w.tiles) {  // std::map over (x,y,z) gives the sort order
            uint8_t rec[10] = {uint8_t(k[0]),        uint8_t(k[0] >> 8),  uint8_t(k[1]),  uint8_t(k[1] >> 8),
                               uint8_t(k[2]),        uint8_t(k[2] >> 8),  uint8_t(appid), uint8_t(appid >> 8),
                               uint8_t(appid >> 16), uint8_t(appid >> 24)};
            h.update(rec, sizeof rec);
        }
        world.set("cells", Object()
                               .boolean("err", err)
                               .i("w", cw)
                               .i("h", ch)
                               .i("originX", ox)
                               .i("originY", oy)
                               .set("appids", u32Array(cells))
                               .u("tilesN", w.tiles.size())
                               .str("tilesSHA", h.hexDigest())
                               .build());
    }

    world.set("turfBinds", kvArray(w.turfBinds));
    world.set("refBind", kvArray(w.refBind));

    // ---- remaining sections. Key names use the canonical lower-case form (dmfsha, htmlsha, sha).
    {  // images: sorted by id; row = {id, op: <decoder record>}
        std::vector<Json> v;
        for (auto& [id, op] : w.images) v.push_back(Object().u("id", id).set("op", proto::toJson(op)).build());
        world.set("images", canon::array(v));
    }
    {  // screen: ARRIVAL order (HUDOrder); pos is the position in it
        std::vector<Json> v;
        for (size_t i = 0; i < w.hudOrder.size(); i++) {
            auto it = w.hud.find(w.hudOrder[i]);
            if (it == w.hud.end()) continue;
            v.push_back(
                Object().i("pos", int64_t(i)).u("ref", w.hudOrder[i]).set("slot", proto::toJson(it->second)).build());
        }
        world.set("screen", canon::array(v));
    }
    {  // verbs: arrival order, the decoded record
        std::vector<Json> v;
        for (uint32_t id : w.verbOrder) {
            auto it = w.verbById.find(id);
            if (it != w.verbById.end()) v.push_back(proto::toJson(it->second));
        }
        world.set("verbs", canon::array(v));
    }
    {  // stat
        std::vector<Json> tabs, lines;
        for (auto& t : w.stat.tabs) tabs.push_back(canon::str(t));
        for (auto& l : w.stat.lines)
            lines.push_back(
                Object().u("ordinal", l.ordinal).str("name", l.name).set("value", proto::toJson(l.value)).build());
        world.set("stat", Object()
                              .set("tabs", canon::array(tabs))
                              .i("selected", w.stat.selected)
                              .u("token", w.stat.token)
                              .i("frames", w.stat.frames)
                              .i("errors", w.stat.errors)
                              .i("stale", w.stat.stale)
                              .set("lines", canon::array(lines))
                              .build());
    }
    {  // browsers: arrival order; html summarised by byte length + sha256
        std::vector<Json> v;
        for (auto& n : w.browserOrder) {
            auto it = w.browsers.find(n);
            if (it == w.browsers.end()) continue;
            auto c = w.browserCalls.find(n);
            v.push_back(Object()
                            .str("name", n)
                            .str("params", it->second.params)
                            .u("htmlLen", it->second.html.size())
                            .str("htmlsha", sha256Hex(it->second.html.data(), it->second.html.size()))
                            .u("outCalls", c == w.browserCalls.end() ? 0 : c->second)
                            .build());
        }
        world.set("browsers", canon::array(v));
    }
    {  // resources: offered = the ResourceList entries in arrival order; received/names come only from noteResource()
        std::vector<Json> off;
        for (auto& [id, ty] : w.resources) off.push_back(Object().u("id", id).u("type", ty).build());
        std::vector<Json> rec, nm;
        for (auto& [id, r] : w.received) rec.push_back(Object().u("id", id).u("len", r.len).str("sha", r.sha).build());
        for (auto& [id, n] : w.resNames) nm.push_back(Object().u("id", id).str("name", n).build());
        world.set("resources", Object()
                                   .set("offered", canon::array(off))
                                   .set("received", canon::array(rec))
                                   .set("names", canon::array(nm))
                                   .build());
    }
    world.set("skin", Object()
                          .boolean("present", !w.skinDmf.empty())
                          .u("resID", w.skinResID)
                          .i("seq", w.skinSeq)
                          .u("dmfLen", w.skinDmf.size())
                          .str("dmfsha", sha256Hex(w.skinDmf.data(), w.skinDmf.size()))
                          .build());
    {  // controls: sorted by key (std::map<string> is bytewise)
        std::vector<Json> v;
        for (auto& [k, val] : w.controls) v.push_back(Object().str("k", k).str("v", val).build());
        world.set("controls", canon::array(v));
    }
    {  // chat: sha over u32-LE-length-prefixed lines
        auto shaLines = [](const std::vector<std::string>& ls) {
            Sha256 h;
            for (auto& l : ls) {
                uint8_t n[4] = {uint8_t(l.size()), uint8_t(l.size() >> 8), uint8_t(l.size() >> 16),
                                uint8_t(l.size() >> 24)};
                h.update(n, 4);
                h.update(l.data(), l.size());
            }
            return h.hexDigest();
        };
        world.set("chat", Object()
                              .u("lines", w.chat.size())
                              .u("total", w.chatTotal)
                              .str("sha", shaLines(w.chat))
                              .str("htmlsha", shaLines(w.chatHtml))
                              .build());
    }
    {  // transient: ids (registry bit included) of atoms with a glide / an animate chain; animatingImgs = ImageAnims keys
        // (chain existence, never time-expired; removed with the image or an ImageCreate re-create).
        std::vector<uint32_t> gl, an;
        for (auto& [id, a] : w.atoms) {
            if (a.gliding) gl.push_back(id);
            if (a.animating) an.push_back(id);
        }
        world.set("transient",
                  Object()
                      .set("glidingAtoms", u32Array(gl))
                      .set("animatingAtoms", u32Array(an))
                      .set("animatingImgs", u32Array(std::vector<uint32_t>(w.imageAnims.begin(), w.imageAnims.end())))
                      .build());
    }

    return std::move(world.v);
}

}  // namespace fugue::model
