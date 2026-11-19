// Hover reports (render::HoverTracker) against stock DreamSeeker 516 bytes. Vectors are c2s 0x0002 bodies stock DreamSeeker
// sent to tests/data/mousehover-world: a 13x13 view (world.view 6) of 32px cells centred on (20,20), the map control
// "main.map", with obj "hov" (ref 0, MouseEntered/Exited) on (22,20), "mov" (ref 1, MouseMove) on (18,20), "plain" (ref 3,
// no Mouse proc) on (20,18) and the client.screen obj "hud" (ref 5, MouseEntered/Exited) at 1,1. The 0x003b bodies are
// that world built with ATOM_HOVER and with CLIENT_MOVE.
#include <QtTest>
#include "net/input.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/mousechain.h"

using namespace fugue;
using namespace fugue::render;
namespace enc = fugue::proto::enc;

namespace {
Bytes hex(const char* h) {
    Bytes b;
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) b.push_back(uint8_t(std::stoi(std::string(h + i, 2), nullptr, 16)));
    return b;
}
const char* kEnterHov =
    "0800ffff010000000003000315001300026d61696e2e6d6170000269636f6e2d783d31363b69636f6e2d793d31363b73637265656e2d6c6f63"
    "3d393a31362c373a313600";
const char* kTurf16_24 =
    "0800ffff030f0017000300030f001700026d61696e2e6d6170000269636f6e2d783d31373b69636f6e2d793d31363b73637265656e2d6c6f"
    "633d333a31372c31313a313600";
const char* kEnterMov =
    "0800ffff010100000003000311001300026d61696e2e6d6170000269636f6e2d783d31363b69636f6e2d793d31363b73637265656e2d6c6f63"
    "3d353a31362c373a313600";
const char* kMoveMov12 =
    "0800ffff010100000003000311001300026d61696e2e6d6170000269636f6e2d783d31323b69636f6e2d793d31363b73637265656e2d6c6f"
    "633d353a31322c373a313600";
const char* kEnterHud =
    "0800ffff0105000000030000ffff0000026d61696e2e6d6170000269636f6e2d783d31363b69636f6e2d793d31363b73637265656e2d6c6f63"
    "3d313a31362c313a313600";
const char* kLetterbox =
    "0800ffff00ffff0000030000ffff0000026d61696e2e6d6170000273637265656e2d6c6f633d393a31362c31343a3800";
const char* kLeave = "0800ffff00ffff0000030000ffff00000000ffff00000200";
const char* kShiftMoveTurf =
    "0800ffff030f0017000300030f001700026d61696e2e6d6170000269636f6e2d783d32303b69636f6e2d793d31353b73686966743d313b"
    "73637265656e2d6c6f633d333a32302c31313a313500";

std::shared_ptr<Sprite> solid() {
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    return sp;
}
// The world above. `walkerGone` drops hov, as if it had walked off its cell under a still pointer.
HitScene scene(bool walkerGone = false) {
    HitScene s;
    s.cols = s.rows = 13;
    s.originX = 14;
    s.originY = 14;
    auto sp = solid();
    HitAtom floor;
    floor.name = "cell";
    floor.kind = kAtomTurf;
    s.atoms.push_back(floor);
    for (int r = 0; r < 13; r++)
        for (int c = 0; c < 13; c++) s.world.push_back({sp, c * 32, (12 - r) * 32, 0});
    auto obj = [&](const char* name, uint32_t ref, int x, int y, bool hover, bool move) {
        HitAtom a;
        a.name = name;
        a.kind = kAtomObj;
        a.ref = ref;
        a.worldX = x;
        a.worldY = y;
        a.hover = hover;
        a.mouseMove = move;
        s.atoms.push_back(a);
        s.world.push_back({sp, (x - 14) * 32, (12 - (y - 14)) * 32, int(s.atoms.size()) - 1});
    };
    if (!walkerGone) obj("hov", 0, 22, 20, true, false);
    obj("mov", 1, 18, 20, true, true);
    obj("plain", 3, 20, 18, false, false);
    HitAtom hud;
    hud.name = "hud";
    hud.kind = kAtomObj;
    hud.ref = 5;
    hud.screen = true;
    hud.hover = true;
    s.atoms.push_back(hud);
    HitHud h;
    h.atom = int(s.atoms.size()) - 1;
    h.tileW = h.tileH = 32;
    h.originX = 0;
    h.originY = 32;
    HitLayer l;
    l.sprite = sp;
    l.atom = h.atom;
    l.own = true;
    h.layers.push_back(l);
    h.dests.push_back({0, 12 * 32});
    s.hud.push_back(h);
    return s;
}
// Screen cell (col,row) at in-cell pixel (px,py), 1-based from the bottom-left as screen-loc counts.
PointerEvent at(int col, int row, int px, int py) {
    PointerEvent e;
    e.imgX = (col - 1) * 32 + px - 1;
    e.imgY = 13 * 32 - 1 - ((row - 1) * 32 + py - 1);
    e.button = 0;
    return e;
}
ChainEnv env(const HitScene& s, bool clientHover = false, bool clientMove = false) {
    ChainEnv e;
    e.scene = &s;
    e.control = "main.map";
    e.clientHover = clientHover;
    e.clientMove = clientMove;
    return e;
}
}  // namespace

class MouseHoverTest : public QObject {
    Q_OBJECT
private slots:
    // Per-atom procs only (ATOM_HOVER): the path the capture took, frame for frame.
    void atomProcsReportOnlyAroundAtomsThatDeclareThem() {
        HitScene s = scene();
        ChainEnv e = env(s);
        HoverTracker t;
        QVERIFY(t.at(at(3, 11, 13, 16), e).empty());  // a plain turf, nothing before it
        QVERIFY(t.at(at(4, 11, 13, 16), e).empty());  // another plain turf
        auto f = t.at(at(9, 7, 16, 16), e);           // onto hov: MouseEntered
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kEnterHov));
        QVERIFY(t.at(at(9, 7, 18, 16), e).empty());  // hov declares no MouseMove
        f = t.at(at(3, 11, 17, 16), e);              // off hov onto a turf: the report MouseExited needs
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kTurf16_24));
        f = t.at(at(5, 7, 16, 16), e);  // onto mov
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kEnterMov));
        f = t.at(at(5, 7, 12, 16), e);  // a step on mov: MouseMove
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kMoveMov12));
        QVERIFY(t.at(at(5, 7, 12, 16), e).empty());          // the same icon pixel again
        QCOMPARE(int(t.at(at(7, 5, 17, 17), e).size()), 1);  // off mov onto plain: still reported, mov wanted it
        QVERIFY(t.at(at(7, 4, 17, 17), e).empty());          // plain to a plain turf: neither wants it
        f = t.at(at(1, 1, 16, 16), e);                       // the HUD object: location null
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kEnterHud));
        QVERIFY(t.at(at(1, 1, 21, 14), e).empty());
        f = t.leave();  // off the control
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kLeave));
        QVERIFY(t.leave().empty());
        QVERIFY(t.at(at(7, 4, 17, 17), e).empty());  // back in over a plain turf
    }
    // Over the letterbox nothing is named: a null report carrying the extrapolated cell, sent only to close a hover.
    void letterboxIsANullReport() {
        HitScene s = scene();
        ChainEnv e = env(s);
        HoverTracker t;
        QCOMPARE(int(t.at(at(9, 7, 16, 16), e).size()), 1);
        PointerEvent box;
        box.imgX = 8 * 32 + 15;
        box.imgY = -8;  // row 14, pixel 8: above the image
        auto f = t.at(box, e);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kLetterbox));
        box.imgX += 3;
        QVERIFY(t.at(box, e).empty());
        QVERIFY(t.leave().empty());  // the letterbox wanted nothing
    }
    // client/MouseMove (CLIENT_MOVE): every atom change and every step over any atom; never a step over the letterbox.
    void clientMoveReportsEveryStep() {
        HitScene s = scene();
        ChainEnv e = env(s, true, true);
        HoverTracker t;
        auto f = t.at(at(3, 11, 17, 16), e);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kTurf16_24));
        PointerEvent shifted = at(3, 11, 20, 15);
        shifted.mods.shift = true;
        f = t.at(shifted, e);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex(kShiftMoveTurf));
        QCOMPARE(int(t.at(at(4, 11, 17, 16), e).size()), 1);  // a plain turf is a change of atom
        PointerEvent box;
        box.imgX = 40;
        box.imgY = -8;
        QCOMPARE(int(t.at(box, e).size()), 1);
        box.imgX += 5;
        QVERIFY(t.at(box, e).empty());
        QCOMPARE(int(t.leave().size()), 1);
    }
    // Under a still pointer only a change of atom is reported; a step is pointer motion only.
    void stillPointerReportsOnlyAChangeOfAtom() {
        HitScene with = scene(), gone = scene(true);
        HoverTracker t;
        QCOMPARE(int(t.at(at(9, 7, 16, 16), env(with, true, true)).size()), 1);
        QVERIFY(t.at(at(9, 7, 20, 16), env(with, true, true), /*motion=*/false).empty());
        auto f = t.at(at(9, 7, 16, 16), env(gone), false);  // hov walked away: the turf beneath
        QCOMPARE(int(f.size()), 1);
        Naming n;
        QVERIFY(nameMouse(gone, at(9, 7, 16, 16).imgX, at(9, 7, 16, 16).imgY, n));
        QCOMPARE(f[0], enc::encodeHover(enc::turfRef(22, 20), enc::turfRef(22, 20), "main.map",
                                        "icon-x=16;icon-y=16;screen-loc=9:16,7:16"));
    }
    // Each report names the map control it came from.
    void reportNamesItsControl() {
        HitScene s = scene();
        ChainEnv e = env(s);
        e.control = "mapwindow.map";
        HoverTracker t;
        auto f = t.at(at(9, 7, 16, 16), e);
        QCOMPARE(int(f.size()), 1);
        std::string body(f[0].begin(), f[0].end());
        QVERIFY(body.find(std::string("\x02mapwindow.map\0", 15)) != std::string::npos);
    }
    void clientHoverBitsComeFrom003b() {
        net::InputModel atom;
        atom.onFrame(S2C::MouseSubs,
                     hex("010001010001000002000000042100000402ea01000000436f6d6d616e647300ff9406000004020000"));
        QVERIFY(!atom.clientHover());
        QVERIFY(!atom.clientMove());
        net::InputModel client;
        client.onFrame(
            S2C::MouseSubs,
            hex("01000101000100000200000004210082010000000402ea01000000436f6d6d616e647300ff9406000004020000"));
        QVERIFY(client.clientHover());
        QVERIFY(client.clientMove());
        QVERIFY(!client.clientWheel());
    }
};

QTEST_APPLESS_MAIN(MouseHoverTest)
#include "mousehover_test.moc"
