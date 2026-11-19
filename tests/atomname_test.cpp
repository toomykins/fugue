// An atom's name reaches every hit-test reader (status bar, verb menu, local menu) without BYOND's embedded text-macro marker.
// Wire: \proper space is \xff\x15space; \improper Scrubbers pipe is \xff\x16Scrubbers pipe.
// Stock DreamSeeker 516 shows the stripped names: "Scrubbers pipe" over the pipe, "space" over the turf.
// The marker is stripped where the render appearance is built.
// Scene replays tests/data/atomname-world.
#include <QtTest>
#include <fstream>
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
}  // namespace

class AtomName : public QObject {
    Q_OBJECT
    model::Session sess_;
    render::ResStore res_;
    render::DrawList dl_;
    std::shared_ptr<const render::HitScene> sc_;
    std::string frames_;

private slots:
    void initTestCase() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR);
        std::ifstream f(dir + "/atomname-world/frames.txt");
        QVERIFY(f);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            frames_ += line + "\n";
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        std::ifstream rf(dir + "/menuicons-world/res/1980f8b1", std::ios::binary);  // 512x32 test sheet
        std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
        Bytes b(bytes.begin(), bytes.end());
        uint32_t claimed = 0x1980f8b1;
        std::string err;
        QVERIFY2(res_.add(b, &claimed, &err), err.c_str());
        sess_.noteResource(claimed, b, "");
        render::SceneStats st;
        QVERIFY2(render::assembleMapControl(sess_, res_, 0, dl_, st, &err, true), err.c_str());
        sc_ = dl_.hit;
        QVERIFY(sc_);
    }

    // The wire must carry the markers, or the checks below prove nothing.
    void wireCarriesTheMarkers() {
        QVERIFY(frames_.find("ff16"
                             "5363727562626572732070697065") != std::string::npos);
        QVERIFY(frames_.find("ff15"
                             "7370616365") != std::string::npos);
    }

    // The right-click verb menu and the status bar read these names. Stock DreamSeeker 516 lists the iconless player mob too
    // (an atom that draws nothing holds its own cell): Guest-<n>, Scrubbers pipe, space.
    void hitStackNamesArePlain() {
        int col = 8 - sc_->originX, row = 8 - sc_->originY;  // the pipe's cell (8,8)
        int px = col * sc_->tileW + 16, py = sc_->pixH() - 1 - (row * sc_->tileH + 16);
        std::vector<render::Hit> hits = render::hitStack(*sc_, px, py);
        QCOMPARE(int(hits.size()), 3);
        QCOMPARE(hits[0].name.rfind("Guest-", 0), size_t(0));
        QCOMPARE(hits[1].name, std::string("Scrubbers pipe"));
        QCOMPARE(hits[2].name, std::string("space"));
        render::Hit h;
        QVERIFY(render::hitAt(*sc_, px, py, h));
        QCOMPARE(h.name, std::string("Scrubbers pipe"));
    }

    void noHitAtomCarriesAMarker() {
        QVERIFY(!sc_->atoms.empty());
        for (const render::HitAtom& a : sc_->atoms) QVERIFY2(a.name.find('\xff') == std::string::npos, a.name.c_str());
    }
};

QTEST_GUILESS_MAIN(AtomName)
#include "atomname_test.moc"
