// Right-click menu per-atom icons (render::appearanceIcon) against the stock DreamSeeker 516 drawing of the same atoms
// (tests/data/menuicons-world/dreamseeker-icons.txt). Each 16x16 cell is classed O (orange), G (green) or '.' (other) and compared pixel by pixel.
// The 32x32 icon is halved by nearest neighbour first (every state is flat colour on 2-pixel boundaries, so box and nearest agree).
// The world (tests/data/menuicons-world/world.dm) varies one appearance property per tile.
//
// Negative controls: a map-style draw and a raw FLOAT_LAYER overlay sort must each score strictly below the real thing.
#include <QtTest>
#include <filesystem>
#include <fstream>
#include <map>
#include "model/world.h"
#include "net/bytes.h"
#include "render/hittest.h"
#include "render/scene.h"

using namespace fugue;

namespace {
Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}
using Grid = std::vector<std::string>;
}  // namespace

class MenuIcon : public QObject {
    Q_OBJECT
    model::Session sess_;
    render::ResStore res_;
    std::shared_ptr<const render::HitScene> sc_;
    std::map<std::string, Grid> ds_;

    bool build() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/menuicons-world";
        std::ifstream f(dir + "/frames.txt");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        for (const auto& e : std::filesystem::directory_iterator(dir + "/res")) {
            std::ifstream rf(e.path(), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
            Bytes b(bytes.begin(), bytes.end());
            uint32_t claimed = uint32_t(std::stoul(e.path().filename().string(), nullptr, 16));
            std::string err;
            if (!res_.add(b, &claimed, &err)) return false;
            sess_.noteResource(claimed, b, "");
        }
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(sess_, res_, 0, dl, st, &err, true)) {
            qWarning("assemble: %s", err.c_str());
            return false;
        }
        sc_ = dl.hit;
        std::ifstream g(dir + "/dreamseeker-icons.txt");
        std::string name;
        while (std::getline(g, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.size() != 16) {
                name = line;
                continue;
            }
            ds_[name].push_back(line);
        }
        return bool(sc_);
    }

    const render::HitAtom* atom(const std::string& name) const {
        for (const auto& a : sc_->atoms)
            if (a.name == name) return &a;
        return nullptr;
    }

    // Our icon for the named atom, halved to DreamSeeker's 16x16 and classed. Empty grid = no icon.
    Grid ours(const std::string& name, int* w = nullptr, int* h = nullptr) {
        const render::HitAtom* a = atom(name);
        if (!a) return {};
        int iw = 0, ih = 0;
        std::vector<uint8_t> px;
        if (!render::appearanceIcon(sess_, res_, a->appid, a->dir, iw, ih, px)) return {};
        if (w) *w = iw;
        if (h) *h = ih;
        Grid g;
        for (int y = 0; y < 16; y++) {
            std::string r;
            for (int x = 0; x < 16; x++) {
                const uint8_t* p = &px[(size_t(y * ih / 16) * size_t(iw) + size_t(x * iw / 16)) * 4];
                if (p[3] == 255 && p[0] == 208 && p[1] == 112 && p[2] == 32)
                    r += 'O';
                else if (p[3] == 255 && p[0] == 0 && p[1] == 255 && p[2] == 0)
                    r += 'G';
                else
                    r += '.';
            }
            g.push_back(r);
        }
        return g;
    }

    int grade(std::vector<std::string>* bad) {
        int agree = 0;
        for (const auto& [name, want] : ds_) {
            Grid got = ours(name);
            if (got == want) {
                agree++;
                continue;
            }
            if (!bad) continue;
            std::string d = name + ": DreamSeeker | ours";
            for (size_t i = 0; i < want.size(); i++)
                d += "\n  " + want[i] + " | " + (i < got.size() ? got[i] : std::string("(no icon)"));
            bad->push_back(d);
        }
        return agree;
    }

private slots:
    void initTestCase() {
        QVERIFY2(build(), "scene from the menuicons world");
        QCOMPARE(int(ds_.size()), 10);
        for (const auto& [n, g] : ds_) QVERIFY2(g.size() == 16, n.c_str());
    }

    void agreesWithDreamSeeker() {
        render::setSceneMutation(0);
        std::vector<std::string> bad;
        int agree = grade(&bad);
        for (auto& b : bad) qWarning("%s", b.c_str());
        qInfo("MENU ICONS C++ vs stock DreamSeeker: %d/%zu atoms identical", agree, ds_.size());
        QCOMPARE(agree, int(ds_.size()));
    }

    // A client.screen object has no map layer; its row still gets its icon.
    void screenAtomHasItsIcon() {
        const render::HitAtom* a = atom("hudthing");
        QVERIFY(a && a->screen);
        int w = 0, h = 0;
        std::vector<uint8_t> px;
        QVERIFY(render::appearanceIcon(sess_, res_, a->appid, a->dir, w, h, px));
        QCOMPARE(w, 32);
        QCOMPARE(h, 32);
        QVERIFY(px[0] == 0 && px[1] == 255 && px[2] == 255 && px[3] == 255);  // scene.dmi "hud": pure cyan
        // The hit stack carries the appearance for a HUD hit, which the icon is drawn from.
        std::vector<render::Hit> hits = render::hitStack(*sc_, 16, sc_->pixH() - 16);
        QVERIFY(!hits.empty());
        QCOMPARE(QString::fromStdString(hits[0].name), QString("hudthing"));
        QCOMPARE(hits[0].appid, a->appid);
        const render::HitAtom* held = atom("held");
        QVERIFY(held && held->screen);
        hits = render::hitStack(*sc_, 40, sc_->pixH() - 16);  // "left": only the left half is opaque
        QVERIFY(!hits.empty());
        QCOMPARE(QString::fromStdString(hits[0].name), QString("held"));
        QCOMPARE(hits[0].appid, held->appid);
    }

    void negativeControlMutations_data() {
        QTest::addColumn<int>("mutation");
        QTest::newRow("map-look") << 19;
        QTest::newRow("raw-float-layer") << 20;
    }
    void negativeControlMutations() {
        QFETCH(int, mutation);
        render::setSceneMutation(mutation);
        int agree = grade(nullptr);
        render::setSceneMutation(0);
        qInfo("negative control (mutation %d): %d/%zu", mutation, agree, ds_.size());
        QVERIFY2(agree < int(ds_.size()),
                 "the broken icon rule still matches every DreamSeeker icon: the world does not separate it");
    }
};

QTEST_APPLESS_MAIN(MenuIcon)
#include "menuicon_test.moc"
