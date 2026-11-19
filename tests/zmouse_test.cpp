// Turfs on another z: which naming stock DreamSeeker 516 gives a turf, [03][x][y] or [07][turf id], on the default map
// and on a camera map (tests/data/zmouse-world). frames.txt is every s2c frame stock DreamDaemon sent that session; the
// scenes are built from it by the scene assembler, and every command is compared with the c2s 0x0002 body stock sent
// (c2s.txt, addressed by its "# <step>" heading and the body's place under it, from 1).
// Phases (frames.txt line of the state before the next phase): P1 -> 40 (mob and eye on z1, the camera shows z2),
// P2 -> 49 (mob and eye on z2, the camera shows z1), P3 -> 57 (mob on z1, client.eye a z2 turf, the camera shows z2).
// The rule: a turf on the default map's z is named by its cell, any other by its id.
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

struct Phase {
    model::Session s;
    std::shared_ptr<HitScene> main, cam;
};

// Screen cell (col,row) at in-cell pixel (px,py), 1-based from the bottom-left as screen-loc counts.
PointerEvent at(const HitScene& s, int col, int row, int px, int py, int64_t t = 0, int win = 0) {
    PointerEvent e;
    e.imgX = (col - 1 - s.screenX0) * s.tileW + px - 1;
    e.imgY = s.pixH() - 1 - ((row - 1 - s.screenY0) * s.tileH + py - 1);
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
}  // namespace

class ZMouseTest : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    std::map<std::string, std::vector<Bytes>> c2s_;
    ResStore res_;
    Bytes grid_;

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
    void build(Phase& p, size_t lastLine) {
        p.s.setIngestClockNs(kT0);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++)
            p.s.ingest(frames_[i].first, frames_[i].second);
        p.s.noteResource(net::resId(grid_), grid_, "grid.dmi");
        DrawList dl, dc;
        SceneStats st;
        std::string err;
        QVERIFY2(assembleMapControl(p.s, res_, kT0, dl, st, &err, /*withHit=*/true), err.c_str());
        QVERIFY2(assembleSecondaryMap(p.s, res_, kT0, kCamShort, dc, st, &err, {}, nullptr, /*withHit=*/true),
                 err.c_str());
        QVERIFY(dl.hit && dc.hit);
        p.main = std::make_shared<HitScene>(*dl.hit);
        p.cam = std::make_shared<HitScene>(*dc.hit);
    }

    // Over a camera turf: hover, a step after the main map's leave, a click and a double-click.
    void camClicks(Phase& p, const std::string& phase) {
        ChainEnv ec = env(*p.cam, kCam);
        HoverTracker t;
        MouseChain c;
        const std::string cam = phase + "-cam", dbl = phase + "-cam-dbl";
        one(t.at(at(*p.cam, 2, 4, 17, 16), ec), cam, 1);
        t.leave();
        one(t.at(at(*p.cam, 2, 4, 19, 14), ec), cam, 3);
        t.forget();
        one(c.press(at(*p.cam, 2, 4, 19, 14, 1000), ec), cam, 4);
        two(c.release(at(*p.cam, 2, 4, 19, 14, 1050), ec), cam, 5);
        one(c.press(at(*p.cam, 2, 4, 19, 14, 3000), ec), dbl, 1);
        two(c.release(at(*p.cam, 2, 4, 19, 14, 3050), ec), dbl, 2);
        two(c.press(at(*p.cam, 2, 4, 19, 14, 3150), ec), dbl, 4);
        two(c.release(at(*p.cam, 2, 4, 19, 14, 3200), ec), dbl, 6);
    }
    // A drag from a camera turf onto the main map, and from the main map onto a camera turf.
    void camDrags(Phase& p, const std::string& phase) {
        ChainEnv em = env(*p.main, "main.map"), ec = env(*p.cam, kCam);
        MouseChain c;
        const std::string toMain = phase + "-drag-cam-to-main", toCam = phase + "-drag-main-to-cam";
        one(c.press(at(*p.cam, 2, 4, 19, 14, 1000, 0), ec), toMain, 1);
        one(c.move(at(*p.main, 12, 7, 30, 12, 1100, 300), em), toMain, 2);
        one(c.move(at(*p.main, 8, 8, 16, 16, 1200, 400), em), toMain, 3);
        two(c.release(at(*p.main, 8, 8, 16, 16, 1300, 400), em), toMain, 4);
        one(c.press(at(*p.main, 8, 8, 16, 16, 3000, 400), em), toCam, 1);
        one(c.move(at(*p.main, 13, 7, 6, 5, 3100, 200), em), toCam, 2);
        one(c.move(at(*p.cam, 3, 3, 17, 17, 3200, 0), ec), toCam, 3);
        two(c.release(at(*p.cam, 3, 3, 17, 17, 3300, 0), ec), toCam, 4);
    }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/zmouse-world";
        std::ifstream f(dir_ + "/frames.txt");
        QVERIFY(f);
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            frames_.push_back({uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                               sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1))});
        }
        QCOMPARE(int(frames_.size()), 56);
        std::ifstream v(dir_ + "/c2s.txt");
        QVERIFY(v);
        std::string step;
        while (std::getline(v, line)) {
            if (line.rfind("# ", 0) == 0) {
                step = line.substr(2, line.find_first_of(":(, ", 2) - 2);
                continue;
            }
            if (!line.empty() && line[0] != '#') c2s_[step].push_back(unhex(line));
        }
        QCOMPARE(int(c2s_["P1-cam"].size()), 6);
        std::string g = slurp(dir_ + "/grid.dmi");
        grid_.assign(g.begin(), g.end());
        std::string err;
        QVERIFY2(res_.add(grid_, nullptr, &err), err.c_str());
    }

    // Eye on z1: the main map's turfs by cell, the camera's z2 turfs by id (camera turf (29,31,2) = 07 00000b0c) in every
    // command; a drag's over-location on the camera is then the map's full id, the drop's the turf.
    void eyeOnZ1() {
        Phase p;
        build(p, 40);
        ChainEnv em = env(*p.main, "main.map");
        HoverTracker t;
        MouseChain c;
        one(t.at(at(*p.main, 8, 8, 16, 16), em), "P1-main-turf", 1);
        t.forget();
        one(c.press(at(*p.main, 8, 8, 18, 14, 1000), em), "P1-main-turf", 2);
        two(c.release(at(*p.main, 8, 8, 18, 14, 1050), em), "P1-main-turf", 3);
        camClicks(p, "P1");
        camDrags(p, "P1");
    }
    // Eye on z2: the main map's z2 turfs by cell, the camera's z1 turfs by id.
    void eyeOnZ2() {
        Phase p;
        build(p, 49);
        ChainEnv em = env(*p.main, "main.map");
        MouseChain c;
        one(c.press(at(*p.main, 8, 8, 18, 14, 1000), em), "P2-main-turf", 2);
        camClicks(p, "P2");
        camDrags(p, "P2");
    }
    // client.eye on a z2 turf with the mob on z1: the default map's z is the eye's, so the camera's z2 turfs are named by
    // cell, and a drag over one names that turf as its over-location.
    void eyeNotMob() {
        Phase p;
        build(p, 57);
        camClicks(p, "P3");
        camDrags(p, "P3");
    }
    // A drag that ends naming nothing: released outside the window, over the popup's page (after a step over main.out),
    // and on the camera map's empty area.
    void dragEndsOverNothing() {
        Phase p;
        build(p, 40);
        ChainEnv em = env(*p.main, "main.map"), ec = env(*p.cam, kCam), out = env(*p.main, "main.out"),
                 popup = env(*p.main, "campop"), none = env(*p.main, "");
        for (ChainEnv* e : {&out, &popup, &none}) e->offMap = true;
        PointerEvent away;  // off every map only the screen position counts (past the drag threshold)
        away.winX = 500;
        MouseChain c;
        one(c.press(at(*p.main, 8, 8, 16, 16, 1000, 0), em), "P1-release-outside", 2);
        one(c.move(at(*p.main, 4, 3, 6, 27, 1100, 200), em), "P1-release-outside", 3);
        one(c.move(away, none), "P1-release-outside", 4);
        two(c.release(away, none), "P1-release-outside", 5);

        one(c.press(at(*p.main, 8, 8, 16, 16, 3000, 0), em), "P1-drag-main-to-popup", 3);
        one(c.move(away, out), "P1-drag-main-to-popup", 4);
        one(c.move(away, popup), "P1-drag-main-to-popup", 5);
        two(c.release(away, popup), "P1-drag-main-to-popup", 6);

        one(c.press(at(*p.cam, 2, 4, 19, 14, 5000, 0), ec), "P1-drag-cam-to-empty", 2);
        one(c.move(at(*p.cam, 6, 0, 15, 5, 5100, 200), ec), "P1-drag-cam-to-empty", 3);
        two(c.release(at(*p.cam, 10, -5, 12, 27, 5200, 300), ec), "P1-drag-cam-to-empty", 4);
    }
    // A drag within the camera map: the drop's over-location is the map's short id for a turf by id, the turf for one by
    // cell (run 2 of the same world).
    void dragWithinTheCamera() {
        for (auto [last, phase] :
             {std::pair{size_t(40), std::string("P1")}, std::pair{size_t(57), std::string("P3")}}) {
            Phase p;
            build(p, last);
            ChainEnv ec = env(*p.cam, kCam);
            MouseChain c;
            const std::string s = phase + "-drag-cam-to-cam";
            one(c.press(at(*p.cam, 2, 4, 19, 14, 1000, 0), ec), s, 3);
            one(c.move(at(*p.cam, 3, 3, 17, 16, 1100, 30), ec), s, 4);
            one(c.move(at(*p.cam, 4, 2, 16, 17, 1200, 60), ec), s, 5);
            two(c.release(at(*p.cam, 4, 2, 16, 17, 1300, 60), ec), s, 6);
        }
    }
};

QTEST_APPLESS_MAIN(ZMouseTest)
#include "zmouse_test.moc"
