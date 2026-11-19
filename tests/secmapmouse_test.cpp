// Mouse input on secondary map controls (render::HoverTracker, render::MouseChain, render::wheelFrames) against what stock
// DreamSeeker 516 sent to tests/data/secmapmouse-world. Vectors are the c2s 0x0002 bodies of c2s-default.txt and
// c2s-atomall.txt, numbered from 1 in the order sent. The scenes are that world's: main.map, a 13x13 view of 32 px cells
// centred on (20,20) with hov (ref 0) on (22,20); main.map2, the 3x3 box of its background with hud2 (ref 4) in cell 1,1;
// and (ATOM_ALL) the camera map campop.cam_[0x3000000]_map, the 5x5 turfs (28,28)-(32,32) with hov2 (ref 1) on (30,30).
#include <QtTest>
#include <fstream>
#include "proto/enc_c2s_0002_input.h"
#include "render/mousechain.h"

using namespace fugue;
using namespace fugue::render;
namespace enc = fugue::proto::enc;

namespace {
Bytes hex(const std::string& h) {
    Bytes b;
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
    return b;
}
std::vector<Bytes> load(const char* name) {
    std::ifstream f(std::string(FUGUE_TEST_DATA_DIR) + "/secmapmouse-world/" + name);
    std::vector<Bytes> out;
    for (std::string l; std::getline(f, l);)
        if (!l.empty() && l[0] != '#') out.push_back(hex(l));
    return out;
}

std::shared_ptr<Sprite> solid() {
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    return sp;
}
HitAtom atom(const char* name, uint8_t kind, uint32_t ref, int x, int y, bool hover) {
    HitAtom a;
    a.name = name;
    a.kind = kind;
    a.ref = ref;
    a.worldX = x;
    a.worldY = y;
    a.hover = hover;
    a.wheel = hover;
    return a;
}
void put(HitScene& s, HitAtom a, int col, int rowFromBottom) {
    s.atoms.push_back(std::move(a));
    s.world.push_back({solid(), (col - 1) * 32, (s.rows - rowFromBottom) * 32, int(s.atoms.size()) - 1});
}
HitScene mainMap() {
    HitScene s;
    s.cols = s.rows = 13;
    s.originX = 14;
    s.originY = 14;
    for (int r = 1; r <= 13; r++)
        for (int c = 1; c <= 13; c++) put(s, atom("cell", kAtomTurf, 0, 13 + c, 13 + r, false), c, r);
    put(s, atom("hov", kAtomObj, 0, 22, 20, true), 9, 7);
    s.anyHover = true;
    return s;
}
HitScene map2() {
    HitScene s;
    s.secondary = true;
    s.cols = s.rows = 3;
    HitAtom h = atom("hud2", kAtomObj, 4, 0, 0, true);
    h.screen = true;
    put(s, h, 1, 1);
    s.anyHover = true;
    return s;
}
HitScene camera() {
    HitScene s;
    s.secondary = true;
    s.cols = s.rows = 5;
    for (int r = 1; r <= 5; r++)
        for (int c = 1; c <= 5; c++) put(s, atom("cell", kAtomTurf, 0, 27 + c, 27 + r, true), c, r);
    put(s, atom("hov", kAtomObj, 1, 30, 30, true), 3, 3);
    s.anyHover = true;
    return s;
}
// Screen cell (col,row) at in-cell pixel (px,py), 1-based from the bottom-left as screen-loc counts.
PointerEvent at(const HitScene& s, int col, int row, int px, int py, int64_t t = 0, int win = 0) {
    PointerEvent e;
    e.imgX = (col - 1) * 32 + px - 1;
    e.imgY = s.rows * 32 - 1 - ((row - 1) * 32 + py - 1);
    e.button = 1;
    e.tMs = t;
    e.winX = win;
    return e;
}
ChainEnv env(const HitScene& s, const char* control) {
    ChainEnv e;
    e.scene = &s;
    e.control = control;
    e.clientWheel = false;
    return e;
}
void one(const std::vector<Bytes>& got, const Bytes& want) {
    QCOMPARE(int(got.size()), 1);
    QCOMPARE(got[0], want);
}
}  // namespace

class SecmapMouseTest : public QObject {
    Q_OBJECT
    std::vector<Bytes> a_, b_;
    const Bytes& A(int n) { return a_.at(size_t(n - 1)); }
    const Bytes& B(int n) { return b_.at(size_t(n - 1)); }
private slots:
    void initTestCase() {
        a_ = load("c2s-default.txt");
        b_ = load("c2s-atomall.txt");
        QCOMPARE(int(a_.size()), 46);
        QCOMPARE(int(b_.size()), 17);
    }
    // One tracker for every map control. Moving from main.map onto main.map2, stock reports hud2 first and main.map's leave
    // after it, so the leave ends hud2's hover and the next step on hud2 reports it again (step X1).
    void oneTrackerForEveryMap() {
        HitScene m = mainMap(), m2 = map2();
        HoverTracker t;
        one(t.at(at(m, 9, 7, 16, 16), env(m, "main.map")), A(1));
        one(t.at(at(m2, 1, 1, 17, 16), env(m2, "main.map2")), A(2));
        one(t.leave(), A(3));
        one(t.at(at(m2, 1, 1, 17, 16), env(m2, "main.map2")), A(4));
        one(t.at(at(m, 9, 7, 16, 16), env(m, "main.map")), A(5));
        one(t.leave(), A(6));
        one(t.at(at(m, 9, 7, 18, 14), env(m, "main.map")), A(7));
    }
    // A button press forgets the atom but not whether it wanted hover: the next step over hov reports it again, and a step
    // onto a plain turf still reports (T1, T5). After a click on a plain turf, nothing (T0).
    void pressForgetsTheAtom() {
        HitScene m = mainMap();
        ChainEnv e = env(m, "main.map");
        HoverTracker t;
        MouseChain c;
        QCOMPARE(int(t.at(at(m, 9, 7, 16, 16), e).size()), 1);
        t.forget();
        one(c.press(at(m, 9, 7, 16, 16, 1000), e), A(8));
        auto r = c.release(at(m, 9, 7, 16, 16, 1050), e);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], A(9));
        QCOMPARE(r[1], A(10));
        one(t.at(at(m, 9, 7, 18, 14), e), A(11));
        one(t.at(at(m, 1, 12, 32, 16), e), A(12));
        t.forget();
        c.press(at(m, 1, 12, 32, 16, 3000), e);
        c.release(at(m, 1, 12, 32, 16, 3050), e);
        QVERIFY(t.at(at(m, 2, 12, 3, 14), e).empty());
        // T5: press hov, drag to a plain turf, drop
        one(t.at(at(m, 9, 7, 16, 16), e), A(16));
        t.forget();
        one(c.press(at(m, 9, 7, 16, 16, 5000, 0), e), A(17));
        one(c.move(at(m, 5, 9, 24, 32, 5100, 50), e), A(18));
        one(c.move(at(m, 1, 12, 32, 16, 5200, 100), e), A(19));
        r = c.release(at(m, 1, 12, 32, 16, 5300, 100), e);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], A(20));
        QCOMPARE(r[1], A(21));
        one(t.at(at(m, 4, 12, 6, 16), e), A(22));
    }
    // On main.map2: MouseDown names the short id, MouseUp and Click the full id; the wheel the short id (C1).
    void secondaryMapClickAndWheel() {
        HitScene m2 = map2();
        ChainEnv e = env(m2, "main.map2");
        MouseChain c;
        one(c.press(at(m2, 1, 1, 17, 16, 1000), e), A(25));
        auto r = c.release(at(m2, 1, 1, 17, 16, 1050), e);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], A(26));
        QCOMPARE(r[1], A(27));
        PointerEvent w = at(m2, 1, 1, 17, 16);
        w.button = 0;
        one(wheelFrames(w, e, 0, 120), A(28));
    }
    // A drag from main.map onto hud2 on main.map2, and from hud2 back onto a main.map turf (G1, G2): each frame names the
    // control under the pointer, the source keeps its own control and location.
    void dragBetweenMaps() {
        HitScene m = mainMap(), m2 = map2();
        ChainEnv em = env(m, "main.map"), e2 = env(m2, "main.map2");
        MouseChain c;
        HoverTracker t;
        one(c.press(at(m, 9, 7, 16, 16, 1000, 0), em), A(31));
        t.forget();
        one(c.move(at(m, 12, 8, 10, 9, 1100, 50), em), A(32));
        one(c.move(at(m2, 1, 1, 17, 16, 1200, 100), e2), A(33));
        auto r = c.release(at(m2, 1, 1, 17, 16, 1300, 100), e2);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], A(34));
        QCOMPARE(r[1], A(35));
        one(t.at(at(m2, 1, 1, 17, 16), e2), A(36));
        one(c.press(at(m2, 1, 1, 17, 16, 3000, 100), e2), A(37));
        t.forget();
        one(c.move(at(m, 9, 9, 20, 22, 3100, 50), em), A(38));
        one(c.move(at(m, 4, 10, 6, 11, 3200, 0), em), A(39));
        r = c.release(at(m, 4, 10, 6, 11, 3300, 0), em);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], A(40));
        QCOMPARE(r[1], A(41));
        one(t.at(at(m, 4, 10, 8, 9), em), A(42));
    }
    // vg's camera console in shape: a runtime map inside a popup, its turfs and objs shown through a screen object's
    // vis_contents. A turf is named by its own cell, the location is the map's id, and screen-loc carries the id
    // url-encoded (K1, K2).
    void cameraConsole() {
        HitScene m = mainMap(), cam = camera();
        for (HitAtom& a : m.atoms) a.hover = a.wheel = true;  // ATOM_ALL: every atom declares the Mouse procs
        ChainEnv em = env(m, "main.map"), ec = env(cam, "campop.cam_[0x3000000]_map");
        HoverTracker t;
        MouseChain c;
        one(t.at(at(m, 1, 13, 32, 12), em), B(1));
        one(t.at(at(cam, 1, 5, 16, 17), ec), B(2));
        t.leave();
        one(t.at(at(cam, 1, 5, 20, 13), ec), B(4));
        one(t.at(at(cam, 3, 3, 16, 17), ec), B(5));
        t.forget();
        one(c.press(at(cam, 3, 3, 16, 17, 1000), ec), B(6));
        auto r = c.release(at(cam, 3, 3, 16, 17, 1050), ec);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], B(7));
        QCOMPARE(r[1], B(8));
        PointerEvent w = at(cam, 3, 3, 16, 17);
        w.button = 0;
        one(wheelFrames(w, ec, 0, -120), B(9));
        one(c.press(at(cam, 3, 3, 16, 17, 3000, 100), ec), B(12));
        t.forget();
        one(c.move(at(m, 4, 12, 28, 2, 3100, 50), em), B(13));
        one(c.move(at(m, 1, 13, 32, 19, 3200, 0), em), B(14));
        r = c.release(at(m, 1, 13, 32, 19, 3300, 0), em);
        QCOMPARE(int(r.size()), 2);
        QCOMPARE(r[0], B(15));
        QCOMPARE(r[1], B(16));
        one(t.at(at(m, 2, 13, 2, 17), em), B(17));
    }
    // Over a secondary map's empty area: a null report naming the map, sent only to end a hover.
    void secondaryNullReport() {
        HitScene m2 = map2();
        ChainEnv e = env(m2, "main.map2");
        HoverTracker t;
        QCOMPARE(int(t.at(at(m2, 1, 1, 17, 16), e).size()), 1);
        auto f = t.at(at(m2, 2, 3, 31, 4), e);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0],
                 enc::encodeHover(enc::nullRef(), enc::nullRef(), "main.map2", "screen-loc=map2:2:31,3:4", "map2"));
        QVERIFY(t.at(at(m2, 2, 3, 32, 3), e).empty());
    }
    void mapIdParam() {
        QCOMPARE(enc::mapIdParam("cam_[0x3000000]_map"), std::string("cam_%5b0x3000000%5d_map"));
        QCOMPARE(enc::mapIdParam("main.map2"), std::string("main.map2"));
    }
};

QTEST_APPLESS_MAIN(SecmapMouseTest)
#include "secmapmouse_test.moc"
