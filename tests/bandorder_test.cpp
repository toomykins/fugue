// bandorder: the draw key (render::drawKey) graded against stock DreamSeeker 516. Scenes: tests/data/bandorder-world/<case>,
// small worlds recorded from stock DreamDaemon, one or two atoms per cell (world.dm lists them). Pixel cases
// (dreamseeker-pixels.tsv): the colour stock DreamSeeker drew at sample pixels - map against screen objs across the
// EFFECTS_LAYER / BACKGROUND_LAYER thresholds in both creation orders (bands, pairs), a layer of exactly 20000 on every row
// (background), equal-layer atoms overlapping across cells (cells) and offset into one cell (cellsame). Menu cases (dreamseeker-menus.tsv): the right-click menu,
// topmost first - thresholds on map and screen objs (thresholds, thresholds2), overlays in another band than their atom
// (overlays).
#include <QtTest>
#include <fstream>
#include <sstream>
#include "model/world.h"
#include "net/bytes.h"
#include "render/hittest.h"
#include "render/ir.h"
#include "render/scene.h"

using namespace fugue;

namespace {
std::string dir(const std::string& c) { return std::string(FUGUE_TEST_DATA_DIR) + "/bandorder-world/" + c; }

Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}

std::vector<std::vector<std::string>> tsv(const std::string& path) {
    std::vector<std::vector<std::string>> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, '\t')) cells.push_back(c);
        if (cells.size() == 4) out.push_back(cells);
    }
    return out;
}

struct Scene {
    model::Session sess;
    render::ResStore res;
    render::DrawList dl;
};

bool load(const std::string& c, const std::string& resID, Scene& s) {
    std::ifstream f(dir(c) + "/frames.txt");
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("0x", 0) != 0) continue;
        uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
        size_t sp = line.find(' ', line.find("len="));
        s.sess.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
    }
    std::ifstream rf(dir(c) + "/res/" + resID, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    Bytes b(bytes.begin(), bytes.end());
    uint32_t claimed = uint32_t(std::stoul(resID, nullptr, 16));
    std::string err;
    if (!s.res.add(b, &claimed, &err)) return false;
    s.sess.noteResource(claimed, b, "");
    render::SceneStats st;
    if (!render::assembleMapControl(s.sess, s.res, 0, s.dl, st, &err, true)) {
        qWarning("assemble: %s", err.c_str());
        return false;
    }
    return s.dl.hit != nullptr;
}

// Rows of a pixel case that fugue-qt draws as stock DreamSeeker did.
int pixelsAgree(const std::string& c, const std::string& resID, int& total) {
    Scene s;
    if (!load(c, resID, s)) return -1;
    render::RgbaImage img = render::execute(s.dl);
    if (img.w != 480 || img.h != 480) return -1;
    int agree = 0;
    total = 0;
    for (const auto& r : tsv(dir(c) + "/dreamseeker-pixels.tsv")) {
        total++;
        int x = std::stoi(r[1]), y = std::stoi(r[2]);
        const uint8_t* p = &img.px[(size_t(img.h - 1 - y) * size_t(img.w) + size_t(x)) * 4];
        char got[8];
        std::snprintf(got, sizeof got, "%02x%02x%02x", p[0], p[1], p[2]);
        if (r[3] == got)
            agree++;
        else
            qWarning("%s %s: DS %s | ours %s", c.c_str(), r[0].c_str(), r[3].c_str(), got);
    }
    return agree;
}

// Rows of a menu case whose right-click stack fugue-qt lists as stock DreamSeeker did.
int menusAgree(const std::string& c, const std::string& resID, int& total) {
    Scene s;
    if (!load(c, resID, s)) return -1;
    const render::HitScene& sc = *s.dl.hit;
    int agree = 0;
    total = 0;
    for (const auto& r : tsv(dir(c) + "/dreamseeker-menus.tsv")) {
        total++;
        std::string got;
        for (const render::Hit& h : render::hitStack(sc, std::stoi(r[1]), sc.pixH() - 1 - std::stoi(r[2])))
            got += (got.empty() ? "" : "|") + h.name;
        if (got == r[3])
            agree++;
        else
            qWarning("%s %s: DS %s | ours %s", c.c_str(), r[0].c_str(), r[3].c_str(), got.c_str());
    }
    return agree;
}
}  // namespace

class BandOrder : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { render::setSceneMutation(0); }

    void drawsAsDreamSeeker_data() {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("res");
        QTest::addColumn<int>("rows");
        QTest::newRow("bands") << "bands" << "49401359" << 192;
        QTest::newRow("pairs") << "pairs" << "6cd19be2" << 192;
        QTest::newRow("background") << "background" << "6cd19be2" << 192;
        QTest::newRow("cells") << "cells" << "6cd19be2" << 6;
        QTest::newRow("cellsame") << "cellsame" << "6cd19be2" << 12;
    }
    void drawsAsDreamSeeker() {
        QFETCH(QString, scene);
        QFETCH(QString, res);
        QFETCH(int, rows);
        int total = 0;
        QCOMPARE(pixelsAgree(scene.toStdString(), res.toStdString(), total), rows);
        QCOMPARE(total, rows);
    }

    void listsAsDreamSeeker_data() {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<int>("rows");
        QTest::newRow("overlays") << "overlays" << 9;
        QTest::newRow("thresholds") << "thresholds" << 28;
        QTest::newRow("thresholds2") << "thresholds2" << 28;
    }
    void listsAsDreamSeeker() {
        QFETCH(QString, scene);
        QFETCH(int, rows);
        int total = 0;
        QCOMPARE(menusAgree(scene.toStdString(), "7706ec97", total), rows);
        QCOMPARE(total, rows);
    }

    // Control: without the map cell's tie-break (mutation 41) a layer of exactly 20000 is BACKGROUND_LAYER on every row and
    // equal layers overlap in arrival order, which stock DreamSeeker contradicts in both scenes and both menus.
    void cellTieBreakIsObservable() {
        render::setSceneMutation(41);
        int total = 0;
        QVERIFY(pixelsAgree("background", "6cd19be2", total) < total);
        QVERIFY(pixelsAgree("cells", "6cd19be2", total) < total);
        QVERIFY(menusAgree("thresholds", "7706ec97", total) < total);
        QVERIFY(menusAgree("thresholds2", "7706ec97", total) < total);
    }
};

QTEST_APPLESS_MAIN(BandOrder)
#include "bandorder_test.moc"
