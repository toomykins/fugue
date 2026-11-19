// MouseWheel frames (render::wheelFrames) against stock DreamSeeker 516 bytes, and the gate that decides whether a notch is
// sent. Vectors are c2s 0x0002 bodies stock DreamSeeker sent to tests/data/mousewheel-world: a 13x13 view (world.view 6)
// of 32px cells centred on (20,20), an obj (ref 0) on (21,20), the map control "main.map". The 0x003b bodies are that
// world built with CLIENT_WHEEL and with ATOM_WHEEL.
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
// A view of `n` x `n` cells whose bottom-left is (ox,oy): every cell a floor turf, plus the obj on (21,20).
HitScene scene(int n, int ox, int oy, bool objWheel = false) {
    HitScene s;
    s.cols = s.rows = n;
    s.originX = ox;
    s.originY = oy;
    HitAtom floor;
    floor.name = "cell";
    floor.kind = kAtomTurf;
    HitAtom thing;
    thing.name = "thing";
    thing.kind = kAtomObj;
    thing.ref = 0;
    thing.worldX = 21;
    thing.worldY = 20;
    thing.wheel = objWheel;
    s.atoms = {floor, thing};
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++) s.world.push_back({sp, c * 32, (n - 1 - r) * 32, 0});
    int tc = 21 - ox, tr = 20 - oy;
    if (tc >= 0 && tc < n && tr >= 0 && tr < n) s.world.push_back({sp, tc * 32, (n - 1 - tr) * 32, 1});
    return s;
}
// Map-image pixel of screen cell (col,row) at in-cell pixel (px,py), both 1-based from the bottom-left as screen-loc counts.
PointerEvent at(const HitScene& s, int col, int row, int px, int py, int button = 0) {
    PointerEvent e;
    e.imgX = (col - 1) * s.tileW + px - 1;
    e.imgY = s.pixH() - 1 - ((row - 1) * s.tileH + py - 1);
    e.button = button;
    return e;
}
ChainEnv env(const HitScene& s, bool clientWheel = true) {
    ChainEnv e;
    e.scene = &s;
    e.control = "main.map";
    e.clientWheel = clientWheel;
    return e;
}
}  // namespace

class MouseWheelTest : public QObject {
    Q_OBJECT
private slots:
    void notchOverAnObjIsTheStockFrame() {
        HitScene s = scene(13, 14, 14);
        auto f = wheelFrames(at(s, 8, 7, 16, 16), env(s), 0, 120);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex("07000a0100ffff0000060001000000000100000000010000f042026d617000026d61696e2e6d6170000269636f6"
                           "e2d783d31363b69"
                           "636f6e2d793d31363b73637265656e2d6c6f633d6d61703a383a31362c373a313600"));
    }
    void notchOverATurfCarriesHeldButtonAndModifiers() {
        HitScene s = scene(13, 14, 14);
        auto held = wheelFrames(at(s, 6, 10, 11, 11, 3), env(s), 0, 120);  // right button held
        QCOMPARE(int(held.size()), 1);
        QCOMPARE(
            held[0],
            hex("07000a0100ffff0000060003120016000100000000010000f042026d617000026d61696e2e6d6170000269636f6e2d783d3131"
                "3b"
                "69636f6e2d793d31313b72696768743d313b627574746f6e3d72696768743b73637265656e2d6c6f633d6d61703a363a31312c"
                "31303a313100"));
        PointerEvent e = at(s, 6, 10, 11, 11);
        e.mods.shift = true;
        auto down = wheelFrames(e, env(s), 0, -120);
        QCOMPARE(int(down.size()), 1);
        QCOMPARE(down[0],
                 hex("07000a0100ffff0000060003120016000100000000010000f0c2026d617000026d61696e2e6d6170000269636f6e2d783"
                     "d31313b"
                     "69636f6e2d793d31313b73686966743d313b73637265656e2d6c6f633d6d61703a363a31312c31303a313100"));
    }
    // Above the image (the letterbox) the cell is extrapolated past the view and the object is null.
    void notchOverTheLetterboxNamesNothing() {
        HitScene s = scene(11, 15, 15);  // world.view 5
        PointerEvent e;
        e.imgX = 4 * 32 + 16;
        e.imgY = -10;
        e.button = 0;
        auto f = wheelFrames(e, env(s), 0, -120);
        QCOMPARE(int(f.size()), 1);
        QCOMPARE(f[0], hex("07000a0100ffff0000060000ffff00000100000000010000f0c2026d617000026d61696e2e6d617000027363726"
                           "5656e2d6c6f63"
                           "3d6d61703a353a31372c31323a313000"));
    }
    // location is the control's own id, and screen-loc is prefixed with it.
    void locationIsTheMapControlId() {
        HitScene s = scene(13, 14, 14);
        ChainEnv e = env(s);
        e.control = "mainwin.viewport";
        auto f = wheelFrames(at(s, 8, 7, 16, 16), e, 0, 120);
        QCOMPARE(int(f.size()), 1);
        std::string body(f[0].begin(), f[0].end());
        QVERIFY(body.find(std::string("\x02viewport\0\x02mainwin.viewport\0", 28)) != std::string::npos);
        QVERIFY(body.find("screen-loc=viewport:8:16,7:16") != std::string::npos);
    }
    // Without client/MouseWheel, stock sends a notch only over an atom whose appearance carries the wheel flag.
    void withoutClientWheelOnlyAFlaggedAtomGetsTheNotch() {
        HitScene plain = scene(13, 14, 14), flagged = scene(13, 14, 14, true);
        QVERIFY(wheelFrames(at(plain, 8, 7, 16, 16), env(plain, false), 0, 120).empty());
        QCOMPARE(int(wheelFrames(at(flagged, 8, 7, 16, 16), env(flagged, false), 0, 120).size()), 1);
        QVERIFY(wheelFrames(at(flagged, 1, 12, 32, 16), env(flagged, false), 0, -120).empty());  // the floor
        PointerEvent box;
        box.imgX = 10;
        box.imgY = -5;
        box.button = 0;
        QVERIFY(wheelFrames(box, env(flagged, false), 0, 120).empty());  // nothing under the pointer
        QVERIFY(wheelFrames(at(plain, 8, 7, 16, 16), env(plain), 0, 0).empty());
    }
    void clientWheelBitComesFrom003b() {
        net::InputModel im;
        QVERIFY(!im.haveMouseSubs());
        im.onFrame(S2C::MouseSubs,
                   hex("01000101000100000200000004210080020000000402ea01000000436f6d6d616e647300ff9406000004020000"));
        QVERIFY(im.haveMouseSubs());
        QVERIFY(im.clientWheel());
        net::InputModel atomOnly;
        atomOnly.onFrame(S2C::MouseSubs,
                         hex("010001010001000002000000042100000402ea01000000436f6d6d616e647300ff9406000004020000"));
        QVERIFY(atomOnly.haveMouseSubs());
        QVERIFY(!atomOnly.clientWheel());
    }
};

QTEST_APPLESS_MAIN(MouseWheelTest)
#include "mousewheel_test.moc"
