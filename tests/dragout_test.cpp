// A drag that ends naming nothing (tests/data/dragout-world): released outside every window, over an OUTPUT, over a
// popup's page, on a map's empty area or letterbox, and onto a camera map lying over the main map. The scenes are built
// from frames.txt, and every command is compared with the c2s 0x0002 body stock DreamSeeker sent (c2s.txt, addressed by
// its "# <step>" heading and the body's place under it, from 1).
#include <QtTest>
#include <fstream>
#include <map>
#include "model/world.h"
#include "net/resource.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/mousechain.h"
#include "render/scene.h"

using namespace fugue;
using namespace fugue::render;

namespace {
Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
constexpr int64_t kT0 = 1'000'000'000'000'000'000;
const char* kCam = "campop.cam_[0x3000000]_map";
const char* kCamShort = "cam_[0x3000000]_map";

// Screen cell (col,row) at in-cell pixel (px,py), 1-based from the bottom-left as screen-loc counts; outside the view
// (the letterbox, a map's empty area) the pixel is extrapolated.
PointerEvent at(const HitScene& s, int col, int row, int px, int py, int win, proto::enc::Mods mods = {}) {
    PointerEvent e;
    e.imgX = (col - 1 - s.screenX0) * s.tileW + px - 1;
    e.imgY = s.pixH() - 1 - ((row - 1 - s.screenY0) * s.tileH + py - 1);
    e.button = 1;
    e.winX = win;
    e.mods = mods;
    return e;
}
PointerEvent off(int win, proto::enc::Mods mods = {}) {
    PointerEvent e;
    e.button = 1;
    e.winX = win;
    e.mods = mods;
    return e;
}
ChainEnv env(const HitScene& s, const char* control) {
    ChainEnv e;
    e.scene = &s;
    e.control = control;
    e.clientWheel = false;
    return e;
}
// Over no map: the control or window there, "" outside every window.
ChainEnv offEnv(const char* control) {
    ChainEnv e;
    e.offMap = true;
    e.control = control;
    e.clientWheel = false;
    return e;
}
}  // namespace

class DragOutTest : public QObject {
    Q_OBJECT
    std::map<std::string, std::vector<Bytes>> c2s_;
    std::shared_ptr<HitScene> main_, cam_;

    const Bytes& V(const std::string& step, int n) {
        auto it = c2s_.find(step);
        if (it == c2s_.end() || n < 1 || size_t(n) > it->second.size()) qFatal("no vector %s #%d", step.c_str(), n);
        return it->second[size_t(n - 1)];
    }
    void one(const std::vector<Bytes>& got, const std::string& step, int n) {
        QCOMPARE(int(got.size()), 1);
        QCOMPARE(got[0], V(step, n));
    }
    void two(const std::vector<Bytes>& got, const std::string& step, int n) {
        QCOMPARE(int(got.size()), 2);
        QCOMPARE(got[0], V(step, n));
        QCOMPARE(got[1], V(step, n + 1));
    }

private slots:
    void initTestCase() {
        const std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/dragout-world";
        std::ifstream f(dir + "/frames.txt");
        QVERIFY(f);
        model::Session s;
        s.setIngestClockNs(kT0);
        std::string line;
        std::getline(f, line);
        int frames = 0;
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            s.ingest(uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                     sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
            frames++;
        }
        QCOMPARE(frames, 36);
        std::ifstream v(dir + "/c2s.txt");
        QVERIFY(v);
        std::string step;
        while (std::getline(v, line)) {
            if (line.rfind("# ", 0) == 0) {
                step = line.substr(2, line.find_first_of(":(, ", 2) - 2);
                continue;
            }
            if (!line.empty() && line[0] != '#') c2s_[step].push_back(unhex(line));
        }
        QCOMPARE(int(c2s_["S1-release-outside"].size()), 5);
        std::string g = slurp(dir + "/grid.dmi");
        Bytes grid(g.begin(), g.end());
        ResStore res;
        std::string err;
        QVERIFY2(res.add(grid, nullptr, &err), err.c_str());
        s.noteResource(net::resId(grid), grid, "grid.dmi");
        DrawList dl, dc;
        SceneStats st;
        QVERIFY2(assembleMapControl(s, res, kT0, dl, st, &err, /*withHit=*/true), err.c_str());
        QVERIFY2(assembleSecondaryMap(s, res, kT0, kCamShort, dc, st, &err, {}, nullptr, /*withHit=*/true),
                 err.c_str());
        QVERIFY(dl.hit && dc.hit);
        main_ = std::make_shared<HitScene>(*dl.hit);
        cam_ = std::make_shared<HitScene>(*dc.hit);
    }

    // Outside every window: the drag step names the source map at the last drag cell with the press's pixel; the MouseUp
    // and MouseDrop name nothing at all and carry the press's cell and pixel, without icon-x/icon-y.
    void releaseOutside() {
        ChainEnv em = env(*main_, "main.map");
        MouseChain c;
        one(c.press(at(*main_, 3, 3, 6, 4, 0), em), "S1-release-outside", 1);
        one(c.move(at(*main_, 4, 2, 6, 3, 50), em), "S1-release-outside", 2);
        one(c.move(off(300), offEnv("")), "S1-release-outside", 3);
        two(c.release(off(300), offEnv("")), "S1-release-outside", 4);
        QCOMPARE(QString::fromStdString(c.note()), QString("drop(off)"));

        one(c.press(at(*main_, 3, 2, 11, 31, 1000), em), "S2-straight-outside", 1);
        one(c.move(off(1300), offEnv("")), "S2-straight-outside", 2);
        two(c.release(off(1300), offEnv("")), "S2-straight-outside", 3);
    }
    // Over a non-map control or a popup's page: the drag step names that control with over_location "" and no screen-loc;
    // the MouseUp names it as its control and carries the press's icon-x/icon-y and cell; the drop's over_control is null.
    void releaseOverAControl() {
        ChainEnv em = env(*main_, "main.map");
        for (auto [step, control] :
             {std::pair{"S3-release-on-output", "main.out"}, std::pair{"S4-release-on-popup-page", "campop"}}) {
            MouseChain c;
            one(c.press(at(*main_, 3, 3, 6, 4, 0), em), step, 1);
            one(c.move(at(*main_, 9, 2, 19, 3, 200), em), step, 2);
            one(c.move(off(500), offEnv(control)), step, 3);
            two(c.release(off(500), offEnv(control)), step, 4);
        }
        MouseChain c;
        one(c.press(at(*main_, 3, 3, 6, 4, 0), em), "S14-output-then-outside", 1);
        one(c.move(off(500), offEnv("main.out")), "S14-output-then-outside", 2);
        one(c.move(off(900), offEnv("")), "S14-output-then-outside", 3);  // no screen-loc once over another control
        two(c.release(off(900), offEnv("")), "S14-output-then-outside", 4);
    }
    // From a camera turf: outside, the drag step's over_location and screen-loc carry the camera's full id; the MouseUp and
    // MouseDrop do not.
    void cameraSourceOffEveryMap() {
        ChainEnv ec = env(*cam_, kCam);
        MouseChain c;
        one(c.press(at(*cam_, 1, 1, 15, 15, 0), ec), "S10-cam-turf-release-outside", 1);
        one(c.move(at(*cam_, 4, 1, 16, 15, 50), ec), "S10-cam-turf-release-outside", 2);
        one(c.move(off(800), offEnv("")), "S10-cam-turf-release-outside", 3);
        two(c.release(off(800), offEnv("")), "S10-cam-turf-release-outside", 4);

        one(c.press(at(*cam_, 1, 1, 15, 15, 2000), ec), "S11-cam-turf-release-on-output", 1);
        one(c.move(off(2500), offEnv("main.out")), "S11-cam-turf-release-on-output", 2);
        two(c.release(off(2500), offEnv("main.out")), "S11-cam-turf-release-on-output", 3);
    }
    // The modifiers are the ones held at each step.
    void modifiers() {
        ChainEnv em = env(*main_, "main.map");
        proto::enc::Mods ctrl;
        ctrl.ctrl = true;
        MouseChain c;
        c.press(at(*main_, 3, 3, 6, 4, 0, ctrl), em);
        one(c.move(off(300, ctrl), offEnv("")), "S12-ctrl-release-outside", 2);
        two(c.release(off(300, ctrl), offEnv("")), "S12-ctrl-release-outside", 3);

        one(c.press(at(*main_, 3, 3, 6, 4, 2000), em), "S13-ctrl-at-release-outside", 1);
        one(c.move(off(2300), offEnv("")), "S13-ctrl-at-release-outside", 2);
        two(c.release(off(2300, ctrl), offEnv("")), "S13-ctrl-at-release-outside", 3);
    }
    // A map's empty area: the cell under the pointer, extrapolated. On the default map's letterbox over_location is "" and
    // the MouseUp's loc null; on the camera map both are its full id, and the drop names the camera as over_control.
    void releaseOnEmptyArea() {
        ChainEnv em = env(*main_, "main.map"), ec = env(*cam_, kCam);
        MouseChain c;
        one(c.press(at(*main_, 3, 3, 6, 4, 0), em), "S15-release-on-letterbox", 1);
        one(c.move(at(*main_, 1, 12, 32, 16, 100), em), "S15-release-on-letterbox", 2);
        one(c.move(at(*main_, 2, 14, 14, 7, 200), em), "S15-release-on-letterbox", 3);
        two(c.release(at(*main_, 2, 14, 14, 7, 200), em), "S15-release-on-letterbox", 4);

        one(c.press(at(*main_, 3, 3, 6, 4, 2000), em), "S17-outside-then-letterbox", 1);
        one(c.move(off(2300), offEnv("")), "S17-outside-then-letterbox", 2);
        one(c.move(at(*main_, 2, 0, 14, 26, 2400), em), "S17-outside-then-letterbox", 3);
        two(c.release(at(*main_, 2, 0, 14, 26, 2400), em), "S17-outside-then-letterbox", 4);

        one(c.press(at(*main_, 3, 3, 6, 4, 4000), em), "S6-release-on-cam-empty-over-main", 1);
        one(c.move(at(*main_, 4, 10, 6, 11, 4100), em), "S6-release-on-cam-empty-over-main", 2);
        one(c.move(at(*cam_, 8, -1, 31, 3, 4200), ec), "S6-release-on-cam-empty-over-main", 3);
        two(c.release(at(*cam_, 8, -1, 31, 3, 4200), ec), "S6-release-on-cam-empty-over-main", 4);

        one(c.press(at(*main_, 3, 3, 6, 4, 6000), em), "S8-through-cam-back-to-main", 1);
        one(c.move(at(*cam_, 8, -1, 31, 3, 6100), ec), "S8-through-cam-back-to-main", 2);
        one(c.move(at(*main_, 1, 12, 32, 16, 6200), em), "S8-through-cam-back-to-main", 3);
        two(c.release(at(*main_, 1, 12, 32, 16, 6200), em), "S8-through-cam-back-to-main", 4);

        one(c.press(at(*main_, 3, 3, 6, 4, 8000), em), "S9-outside-and-back", 1);
        one(c.move(off(8300), offEnv("")), "S9-outside-and-back", 2);
        one(c.move(at(*main_, 1, 12, 32, 16, 8400), em), "S9-outside-and-back", 3);
        two(c.release(at(*main_, 1, 12, 32, 16, 8400), em), "S9-outside-and-back", 4);
    }
    // Released on a camera turf where the popup lies over the main map: the camera's turf, as over any camera turf (the
    // widget's part, the topmost window deciding, is shell/tests/input_test.cpp).
    void releaseOnCameraOverMain() {
        ChainEnv em = env(*main_, "main.map"), ec = env(*cam_, kCam);
        MouseChain c;
        one(c.press(at(*main_, 3, 3, 6, 4, 0), em), "S5-release-on-cam-turf-over-main", 1);
        one(c.move(at(*main_, 4, 10, 6, 11, 100), em), "S5-release-on-cam-turf-over-main", 2);
        one(c.move(at(*cam_, 3, 3, 9, 5, 200), ec), "S5-release-on-cam-turf-over-main", 3);
        two(c.release(at(*cam_, 3, 3, 9, 5, 200), ec), "S5-release-on-cam-turf-over-main", 4);
    }
    // Released outside every window, the MouseUp carries the press's pixel inside a cell of the pressed scene's tile size
    // (measured at 32 px; a 64 px world is the same rule, not separately captured).
    void releaseOutsideKeepsThePressPixelAtAnyTileSize() {
        HitScene s;
        s.tileW = s.tileH = 64;
        s.cols = s.rows = 3;
        s.originX = s.originY = 1;
        s.atoms.push_back({"thing", 1, kAtomObj, 9, 2, 2, false});
        auto sp = std::make_shared<Sprite>();
        sp->w = sp->h = 64;
        sp->px.assign(64 * 64 * 4, 255);
        s.world.push_back({sp, 64, 64, 0});
        ChainEnv e = env(s, "main.map");
        MouseChain c;
        QCOMPARE(int(c.press(at(s, 2, 2, 50, 40, 0), e).size()), 1);
        QCOMPARE(int(c.move(off(300), offEnv("")).size()), 1);
        std::vector<Bytes> r = c.release(off(300), offEnv(""));
        QCOMPARE(int(r.size()), 2);
        const std::string up(r[0].begin(), r[0].end());
        QVERIFY2(up.find("screen-loc=2:50,2:40") != std::string::npos, up.c_str());
    }
    // A map with no scene yet (a secondary map before its first picture) is all empty area: named like a secondary map's
    // empty area, with no cell. Stock has no such state.
    void mapWithNoSceneIsItsEmptyArea() {
        ChainEnv em = env(*main_, "main.map"), none;
        none.control = "main.map2";
        none.clientWheel = false;
        MouseChain c;
        one(c.press(at(*main_, 3, 3, 6, 4, 0), em), "S1-release-outside", 1);
        std::vector<Bytes> d = c.move(at(*main_, 4, 2, 6, 3, 50), none);
        QCOMPARE(int(d.size()), 1);
        const std::string drag(d[0].begin(), d[0].end());
        QVERIFY(drag.find(std::string("\x02main.map2\0", 11)) != std::string::npos);
        QVERIFY(drag.find("screen-loc") == std::string::npos);
        std::vector<Bytes> r = c.release(at(*main_, 4, 2, 6, 3, 50), none);
        QCOMPARE(int(r.size()), 2);
        const std::string up(r[0].begin(), r[0].end());
        QVERIFY(up.find("main.map2") != std::string::npos && up.find("icon-x=6;icon-y=4") != std::string::npos);
        QVERIFY(up.find("screen-loc") == std::string::npos);
        QCOMPARE(QString::fromStdString(c.note()), QString("drop(off)"));
    }
};

QTEST_APPLESS_MAIN(DragOutTest)
#include "dragout_test.moc"
