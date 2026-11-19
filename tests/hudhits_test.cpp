// hudhits: client.screen objects larger than one tile (vg's 64x32 interface buttons, its 480x480 holomap image) are drawn
// and clicked whole, with or without a plane master. Scene: tests/data/hudhits-world (world.dm lists the objects), recorded
// from stock DreamDaemon once with a plane master (frames-pm.txt) and once without (frames-nopm.txt). The hit test is graded
// against what stock DreamSeeker 516 answered at 14 pixels (dreamseeker-answers.tsv, read out of the world's Click() log);
// its answers are the same in both worlds.
#include <QtTest>
#include <fstream>
#include <sstream>
#include "model/world.h"
#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/hittest.h"
#include "render/mousechain.h"
#include "render/scene.h"

using namespace fugue;

namespace {
struct Row {
    std::string label, obj, iconX, iconY, screenLoc;
    int x, y;
};

std::string dir() { return std::string(FUGUE_TEST_DATA_DIR) + "/hudhits-world"; }

std::vector<Row> loadTsv() {
    std::vector<Row> rows;
    std::ifstream f(dir() + "/dreamseeker-answers.tsv");
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> c;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) c.push_back(cell);
        if (c.size() != 7) continue;
        rows.push_back({c[0], c[3], c[4], c[5], c[6], std::stoi(c[1]), std::stoi(c[2])});
    }
    return rows;
}

Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}

std::string screenLocOf(const std::string& params) {
    size_t p = params.find("screen-loc=");
    return p == std::string::npos ? "" : params.substr(p + 11);
}

constexpr uint32_t kBlue = 0x2050e0, kOrange = 0xe05020, kGreen = 0x20c040, kMagenta = 0xc020c0, kFloor = 0x606060;
}  // namespace

class HudHits : public QObject {
    Q_OBJECT
    std::vector<Row> rows_;

    struct World {
        model::Session sess;
        render::ResStore res;
        render::DrawList dl;
    };

    bool load(const std::string& frames, World& w) {
        std::ifstream f(dir() + "/" + frames);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            w.sess.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        for (const char* r : {"51cf2fc7", "52951a07", "660c6062"}) {
            std::ifstream rf(dir() + "/res/" + r, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
            Bytes b(bytes.begin(), bytes.end());
            uint32_t claimed = uint32_t(std::stoul(r, nullptr, 16));
            std::string err;
            if (!w.res.add(b, &claimed, &err)) return false;
            w.sess.noteResource(claimed, b, "");
        }
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(w.sess, w.res, 0, w.dl, st, &err, true)) {
            qWarning("assemble: %s", err.c_str());
            return false;
        }
        return w.dl.hit != nullptr;
    }

    // The colour at (x, y) counted from the viewport's bottom-left corner.
    static uint32_t at(const render::RgbaImage& img, int x, int y) {
        const uint8_t* p = &img.px[(size_t(img.h - 1 - y) * size_t(img.w) + size_t(x)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }

    void grade(const std::string& frames) {
        World w;
        QVERIFY2(load(frames, w), frames.c_str());
        const render::HitScene& sc = *w.dl.hit;
        QCOMPARE(sc.pixW(), 480);
        QCOMPARE(sc.pixH(), 480);
        int agree = 0;
        for (const Row& r : rows_) {
            int px = r.x, py = sc.pixH() - 1 - r.y;
            render::Naming n;
            if (!render::nameMouse(sc, px, py, n)) {
                qWarning("%s: we name nothing; DS named %s", r.label.c_str(), r.obj.c_str());
                continue;
            }
            proto::enc::ClickCmd c = render::clickCmdFrom(n, 1, {}, "mapwindow.map");
            std::string obj = n.hit.kind == render::kAtomTurf
                                  ? "turf " + std::to_string(n.overX) + "," + std::to_string(n.overY)
                                  : n.hit.name;
            std::string sl = screenLocOf(proto::enc::clickParams(c));
            if (obj == r.obj && std::to_string(n.iconX) == r.iconX && std::to_string(n.iconY) == r.iconY &&
                sl == r.screenLoc)
                agree++;
            else
                qWarning("%s: DS %s %s,%s %s | ours %s %d,%d %s", r.label.c_str(), r.obj.c_str(), r.iconX.c_str(),
                         r.iconY.c_str(), r.screenLoc.c_str(), obj.c_str(), n.iconX, n.iconY, sl.c_str());
        }
        QCOMPARE(agree, int(rows_.size()));
    }

    // What stock DreamSeeker draws at the clicked pixels: every object whole, its transparent pixels showing the floor.
    void drawn(const std::string& frames) {
        World w;
        QVERIFY2(load(frames, w), frames.c_str());
        render::RgbaImage img = render::execute(w.dl);
        QCOMPARE(img.w, 480);
        QCOMPARE(at(img, 330, 110), kBlue);
        QCOMPARE(at(img, 380, 110), kBlue);
        QCOMPARE(at(img, 372, 110), kFloor);
        QCOMPARE(at(img, 265, 110), kOrange);
        QCOMPARE(at(img, 120, 350), kGreen);
        QCOMPARE(at(img, 60, 410), kGreen);
        QCOMPARE(at(img, 120, 400), kFloor);
        QCOMPARE(at(img, 400, 400), kMagenta);
        QCOMPARE(at(img, 330, 400), kFloor);
    }

private slots:
    void initTestCase() {
        rows_ = loadTsv();
        QCOMPARE(int(rows_.size()), 14);
    }
    void agreesWithDreamSeekerPlaneMaster() { grade("frames-pm.txt"); }
    void agreesWithDreamSeekerNoPlaneMaster() { grade("frames-nopm.txt"); }
    void drawnWholePlaneMaster() { drawn("frames-pm.txt"); }
    void drawnWholeNoPlaneMaster() { drawn("frames-nopm.txt"); }
};

QTEST_GUILESS_MAIN(HudHits)
#include "hudhits_test.moc"
