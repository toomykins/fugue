// hudoverlay: overlays and underlays on client.screen objects (vg's inventory highlights, action buttons, health doll),
// drawn and clicked as stock DreamSeeker 516 does, with or without a plane master. Scene: tests/data/hudoverlay-world
// (world.dm lists the objects), recorded from stock DreamDaemon with a plane master (frames-pm.txt) and without
// (frames-nopm.txt). dreamseeker-map.png is stock DreamSeeker's 480x480 map of that world (the same in both), and
// dreamseeker-answers.tsv what its Click() logged at 14 pixels (the same in both).
#include <QtTest>
#include <fstream>
#include <sstream>
#include "model/world.h"
#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/hittest.h"
#include "render/mousechain.h"
#include "render/png.h"
#include "render/scene.h"

using namespace fugue;

namespace {
struct Row {
    std::string label, obj, iconX, iconY, screenLoc;
    int x, y;
};

std::string dir() { return std::string(FUGUE_TEST_DATA_DIR) + "/hudoverlay-world"; }

std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

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
}  // namespace

class HudOverlay : public QObject {
    Q_OBJECT
    std::vector<Row> rows_;
    render::RgbaImage ds_;

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
        std::string bytes = slurp(dir() + "/res/c17aa85d");
        Bytes b(bytes.begin(), bytes.end());
        uint32_t claimed = 0xc17aa85d;
        std::string err;
        if (!w.res.add(b, &claimed, &err)) return false;
        w.sess.noteResource(claimed, b, "");
        render::SceneStats st;
        if (!render::assembleMapControl(w.sess, w.res, 0, w.dl, st, &err, true)) {
            qWarning("assemble: %s", err.c_str());
            return false;
        }
        return w.dl.hit != nullptr;
    }

    // Every pixel of the map against stock DreamSeeker's; the mismatches are counted per 32x32 cell.
    void drawn(const std::string& frames) {
        World w;
        QVERIFY2(load(frames, w), frames.c_str());
        render::RgbaImage img = render::execute(w.dl);
        QCOMPARE(img.w, ds_.w);
        QCOMPARE(img.h, ds_.h);
        std::map<std::pair<int, int>, int> bad;
        for (int y = 0; y < img.h; y++)
            for (int x = 0; x < img.w; x++) {
                const uint8_t* a = &img.px[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
                const uint8_t* b = &ds_.px[(size_t(y) * size_t(ds_.w) + size_t(x)) * 4];
                for (int c = 0; c < 3; c++)
                    if (std::abs(int(a[c]) - int(b[c])) > 1) {
                        bad[{x / 32 + 1, 15 - y / 32}]++;
                        break;
                    }
            }
        for (const auto& [cell, n] : bad) qWarning("cell %d,%d: %d pixels differ", cell.first, cell.second, n);
        QVERIFY(bad.empty());
    }

    void grade(const std::string& frames) {
        World w;
        QVERIFY2(load(frames, w), frames.c_str());
        const render::HitScene& sc = *w.dl.hit;
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

private slots:
    void initTestCase() {
        rows_ = loadTsv();
        QCOMPARE(int(rows_.size()), 14);
        std::string png = slurp(dir() + "/dreamseeker-map.png");
        std::string why;
        QVERIFY2(render::decodePng(reinterpret_cast<const uint8_t*>(png.data()), png.size(), ds_, why) ==
                     render::PngStatus::Ok,
                 why.c_str());
        QCOMPARE(ds_.w, 480);
    }
    void drawnPlaneMaster() { drawn("frames-pm.txt"); }
    void drawnNoPlaneMaster() { drawn("frames-nopm.txt"); }
    void clicksPlaneMaster() { grade("frames-pm.txt"); }
    void clicksNoPlaneMaster() { grade("frames-nopm.txt"); }
};

QTEST_GUILESS_MAIN(HudOverlay)
#include "hudoverlay_test.moc"
