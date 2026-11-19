// Frame sequences render::MouseChain produces for press, motion and release on a synthetic scene: one obj on cell (5,5) over a floor.
// One case per rule in render/mousechain.h. Frames are checked by their 0x0002 sub byte and, where a rule names a target, by the over/loc refs.
#include <QtTest>
#include "proto/enc_c2s_0002_input.h"
#include "render/mousechain.h"

using namespace fugue;
using namespace fugue::render;
namespace enc = fugue::proto::enc;

namespace {
SpritePtr solid(uint8_t a) {
    auto s = std::make_shared<Sprite>();
    s->w = s->h = 32;
    s->px.assign(32 * 32 * 4, 0);
    for (size_t i = 0; i < s->px.size(); i += 4) {
        s->px[i] = 200;
        s->px[i + 3] = a;
    }
    return s;
}
// a 15x15 view at 32px, origin (1,1): every cell a floor turf; an /obj (ref 7, "door") on cell (5,5).
HitScene scene() {
    HitScene s;
    s.cols = s.rows = 15;
    s.originX = s.originY = 1;
    s.atoms.push_back({"floor", 1, kAtomTurf, 0, 0, 0, false});
    s.atoms.push_back({"door", 1, kAtomObj, 7, 5, 5, false});
    auto sp = solid(255);
    for (int r = 0; r < 15; r++)
        for (int c = 0; c < 15; c++) s.world.push_back({sp, c * 32, (14 - r) * 32, 0});
    s.world.push_back({sp, 4 * 32, (14 - 4) * 32, 1});  // cell (5,5) = col 4, row 4 from the bottom
    return s;
}
// pixel centre of world cell (x,y)
void centre(int x, int y, int& px, int& py) {
    px = (x - 1) * 32 + 16;
    py = 480 - 1 - ((y - 1) * 32 + 16);
}
PointerEvent ev(int cx, int cy, int64_t t, int button = 1) {
    PointerEvent e;
    centre(cx, cy, e.imgX, e.imgY);
    e.winX = e.imgX;
    e.winY = e.imgY;
    e.button = button;
    e.tMs = t;
    return e;
}
QList<int> subs(const std::vector<Bytes>& f) {
    QList<int> l;
    for (auto& b : f) l << int(b[0]);
    return l;
}
}  // namespace

class MouseChainTest : public QObject {
    Q_OBJECT
    HitScene sc = scene();
    ChainEnv env(enc::MouseHooks h = enc::allMouseHooks(), bool right = false) {
        ChainEnv e;
        e.scene = &sc;
        e.hooks = h;
        e.rightClickToWire = right;
        return e;
    }
private slots:
    void plainClickIsDownThenUpClick() {
        MouseChain c;
        auto d = c.press(ev(5, 5, 0), env());
        QCOMPARE(subs(d), (QList<int>{enc::kMouseDown}));
        auto u = c.release(ev(5, 5, 100), env());
        QCOMPARE(subs(u), (QList<int>{enc::kMouseUp, enc::kMouseClick}));
        QCOMPARE(c.note(), std::string("click"));
        // the over ref is the door (obj 7), the loc its turf (5,5)
        Bytes want = enc::encodeAtomRef(enc::objRef(7));
        QVERIFY(std::equal(want.begin(), want.end(), u[1].begin() + 4));
        Bytes loc = enc::encodeAtomRef(enc::turfRef(5, 5));
        QVERIFY(std::equal(loc.begin(), loc.end(), u[1].begin() + 4 + 5 + 2));
    }
    void floorClickNamesTheTurf() {
        MouseChain c;
        c.press(ev(3, 9, 0), env());
        auto u = c.release(ev(3, 9, 50), env());
        Bytes t = enc::encodeAtomRef(enc::turfRef(3, 9));
        QVERIFY(std::equal(t.begin(), t.end(), u[1].begin() + 4));
    }
    void subscriptionMaskGatesEveryFrame() {
        enc::MouseHooks h;
        h.click = true;  // Click only
        MouseChain c;
        QVERIFY(c.press(ev(5, 5, 0), env(h)).empty());
        QCOMPARE(subs(c.release(ev(5, 5, 90), env(h))), (QList<int>{enc::kMouseClick}));
        enc::MouseHooks none;  // a world that declared nothing: no frame at all
        QVERIFY(c.press(ev(5, 5, 1000), env(none)).empty());
        QVERIFY(c.release(ev(5, 5, 1090), env(none)).empty());
    }
    void doubleClickAddsOneFrameAfterDownOnly() {
        MouseChain c;
        c.press(ev(5, 5, 0), env());
        c.release(ev(5, 5, 60), env());
        auto d2 = c.press(ev(5, 5, 300), env());  // 300 ms later: inside the 400 ms window
        QCOMPARE(subs(d2), (QList<int>{enc::kMouseDown, enc::kMouseDblClick}));
        QCOMPARE(subs(c.release(ev(5, 5, 360), env())),
                 (QList<int>{enc::kMouseUp, enc::kMouseClick}));  // Click is NOT replaced
        // a third rapid press starts a fresh pair
        QCOMPARE(subs(c.press(ev(5, 5, 400), env())), (QList<int>{enc::kMouseDown}));
        c.release(ev(5, 5, 420), env());
    }
    void doubleClickWindowAndDeclaration() {
        MouseChain c;
        c.press(ev(5, 5, 0), env());
        c.release(ev(5, 5, 60), env());
        QCOMPARE(subs(c.press(ev(5, 5, 500), env())), (QList<int>{enc::kMouseDown}));  // 500 ms: two independent clicks
        c.release(ev(5, 5, 560), env());
        enc::MouseHooks nodbl = enc::allMouseHooks();
        nodbl.dblClick = false;
        MouseChain d;
        d.press(ev(5, 5, 0), env(nodbl));
        d.release(ev(5, 5, 60), env(nodbl));
        QCOMPARE(subs(d.press(ev(5, 5, 200), env(nodbl))),
                 (QList<int>{enc::kMouseDown}));  // undeclared: just a second click
        MouseChain e;                             // far apart: not a double click
        e.press(ev(5, 5, 0), env());
        e.release(ev(5, 5, 60), env());
        QCOMPARE(subs(e.press(ev(9, 9, 200), env())), (QList<int>{enc::kMouseDown}));
    }
    void phantomDragBelowThresholdIsStillAClick() {
        MouseChain c;
        c.press(ev(5, 5, 0), env());
        PointerEvent m = ev(5, 5, 20);
        m.winX += 2;
        m.imgX += 2;  // moved 2 px
        QVERIFY(c.move(m, env()).empty());
        auto u = c.release(m, env());
        QCOMPARE(subs(u), (QList<int>{enc::kMouseUp, enc::kMouseClick}));
    }
    void realDragSendsDragThenUpAndDropNoClick() {
        MouseChain c;
        c.press(ev(5, 5, 0), env());
        auto m = c.move(ev(7, 5, 40), env());  // two tiles over
        QCOMPARE(m.size(), size_t(1));
        QCOMPARE(int(m[0][0]), int(enc::kMouseExt));
        QCOMPARE(int(m[0][2]) | int(m[0][3]) << 8, int(enc::kMouseEventDrag));
        auto u = c.release(ev(7, 5, 80), env());
        QCOMPARE(u.size(), size_t(2));
        QCOMPARE(int(u[0][0]), int(enc::kMouseUp));
        QCOMPARE(int(u[1][0]), int(enc::kMouseExt));
        QCOMPARE(int(u[1][2]) | int(u[1][3]) << 8, int(enc::kMouseEventDrop));  // MouseDrop, and no Click
        QCOMPARE(c.note(), std::string("drop"));
    }
    void dragWorldWithoutDropGetsBareMouseUp() {
        enc::MouseHooks h = enc::allMouseHooks();
        h.drop = false;
        h.drag = false;  // Click/Down/Up declared, no drag/drop
        MouseChain c;
        c.press(ev(5, 5, 0), env(h));
        QVERIFY(c.move(ev(7, 5, 40), env(h)).empty());  // no MouseDrag frames
        auto u = c.release(ev(7, 5, 80), env(h));
        QCOMPARE(subs(u), (QList<int>{enc::kMouseUp}));  // the release is MouseUp alone: no Click, no Drop
        QCOMPARE(c.note(), std::string("move-only"));
    }
    void dragFramesOnlyWhenDragDeclared() {
        enc::MouseHooks h = enc::allMouseHooks();
        h.drag = false;  // drop but no drag
        MouseChain c;
        c.press(ev(5, 5, 0), env(h));
        QVERIFY(c.move(ev(6, 5, 20), env(h)).empty());
        QCOMPARE(c.release(ev(7, 5, 80), env(h)).size(), size_t(2));  // Up + Drop
    }
    void rightButtonOnlyWithRightClickParam() {
        MouseChain c;
        QVERIFY(c.press(ev(5, 5, 0, 3), env()).empty());
        QCOMPARE(c.note(), std::string("unwired"));
        QVERIFY(c.release(ev(5, 5, 50, 3), env()).empty());
        auto d = c.press(ev(5, 5, 100, 3), env(enc::allMouseHooks(), true));
        QCOMPARE(d.size(), size_t(1));
        QVERIFY(QString::fromLatin1(reinterpret_cast<const char*>(d[0].data()), int(d[0].size()))
                    .contains("right=1;button=right"));
    }
    // A right press the world kept to the popup (show_popup_menus TRUE) whose release finds the flag FALSE: MouseUp and
    // Click, no MouseDown; the same atom the release names.
    void unsentRightPressEndsAsUpAndClick() {
        MouseChain c;
        auto u = c.releaseUnpressed(ev(5, 5, 100, 3), env(enc::allMouseHooks(), true));
        QCOMPARE(subs(u), (QList<int>{enc::kMouseUp, enc::kMouseClick}));
        QCOMPARE(c.note(), std::string("click(unpressed)"));
        Bytes want = enc::encodeAtomRef(enc::objRef(7));
        QVERIFY(std::equal(want.begin(), want.end(), u[1].begin() + 4));
        QVERIFY(std::string(u[1].begin(), u[1].end()).find("right=1;button=right;") != std::string::npos);
        enc::MouseHooks clickOnly;
        clickOnly.click = true;
        QCOMPARE(subs(c.releaseUnpressed(ev(5, 5, 200, 3), env(clickOnly, true))), (QList<int>{enc::kMouseClick}));
        QVERIFY(c.releaseUnpressed(ev(5, 5, 300, 3), env(enc::MouseHooks{}, true)).empty());
        QVERIFY(!c.held());
    }
    // The report that ends a popup: with the right button still down the params carry right=1 (no button=right), ahead of
    // screen-loc; with no button they do not.
    void hoverWhileRightHeldCarriesRight() {
        const std::string held = [&] {
            HoverTracker h;
            ChainEnv e = env();
            e.clientHover = true;
            auto f = h.at(ev(5, 5, 0, 3), e, false);
            return f.empty() ? std::string() : std::string(f[0].begin(), f[0].end());
        }();
        QVERIFY(!held.empty());
        QVERIFY(held.find("right=1;screen-loc=") != std::string::npos);
        QVERIFY(held.find("button=") == std::string::npos);
        HoverTracker h;
        ChainEnv e = env();
        e.clientHover = true;
        auto f = h.at(ev(5, 5, 0, 0), e, false);
        QVERIFY(!f.empty());
        QVERIFY(std::string(f[0].begin(), f[0].end()).find("right=1") == std::string::npos);
    }
    void nothingUnderThePointerSendsNothing() {
        HitScene s = scene();
        s.world.clear();  // a view that draws nothing
        ChainEnv e;
        e.scene = &s;
        MouseChain c;
        QVERIFY(c.press(ev(5, 5, 0), e).empty());
        QVERIFY(c.release(ev(5, 5, 50), e).empty());
        QCOMPARE(c.note(), std::string("nohit"));
    }
};

QTEST_APPLESS_MAIN(MouseChainTest)
#include "mousechain_test.moc"
