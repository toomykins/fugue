// zas: vg's gas overlays (code/modules/ZAS/Turf.dm update_graphic) are shared nullspace /obj on plane 12 put in TURFS'
// vis_contents. Stock DreamDaemon sends a turf's vis_contents in 0x00d8 tag-1 records ([x][y][z] 0-based, the turf's
// appearance, the items); appid 0xffff with no items empties it. Stock DreamSeeker 516 draws each item at the turf, over
// what stands there (plane 12 over the crate and the mob), and an emptied turf keeps its own look. gasA is translucent
// (alpha 160) and cycles three frames at delay 2 on the shared clock, as vg's plasma does; DreamSeeker blends it over
// whatever is beneath, the mob included.
//
// The scene comes from a recorded session (tests/data/zas-world: every s2c frame of that contrast world as served by
// stock DreamDaemon). Phases (frames.txt line of the state): 1 -> 35, 2 -> 38, 3 -> 42, 5 (the player at (15,5),
// gasA on (17,5) since phase 4) -> 50, 6 -> 54, 7 (the player on (16,5)) -> 58. Asserted at 32 px a tile on the
// composited map control. At kT0 the shared clock is 1e10 ds, 4 ds into gasA's 6 ds cycle: its last frame.
#include <QtTest>
#include <cstdlib>
#include <fstream>
#include "model/world.h"
#include "net/bytes.h"
#include "render/scene.h"

using namespace fugue;

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
constexpr int64_t kDs = 100'000'000;
constexpr uint32_t kResThing = 0xff2f7ab6;
constexpr uint32_t kTileApp = 4;  // /turf/tile's appearance
constexpr uint32_t kCyan = 0x00ffff, kTile = 0x969696, kCrate = 0xc8c8c8, kMob = 0x2828ff;
// gasA's frames (magenta, yellow, green at alpha 160) over the tile, the crate, the mob and the floor's 96 grey. The tile,
// mob and floor values are stock DreamSeeker 516's pixels for that scene.
constexpr uint32_t kOnTile[3] = {0xd838d8, 0xd8d838, 0x38d838};
constexpr uint32_t kOnCrate[3] = {0xeb4beb, 0xebeb4b, 0x4beb4b};
constexpr uint32_t kOnMob[3] = {0xaf0fff, 0xafaf5f, 0x0faf5f};
constexpr uint32_t kOnFloor[3] = {0xc424c4, 0xc4c424, 0x24c424};
constexpr int kLast = 2;  // gasA's frame at kT0
bool near(uint32_t a, uint32_t b) {
    for (int sh : {16, 8, 0})
        if (std::abs(int(a >> sh & 0xff) - int(b >> sh & 0xff)) > 1) return false;
    return true;
}
}  // namespace

class Zas : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    void ingestUpTo(model::Session& s, size_t lastLine, int64_t at = kT0) {
        s.setIngestClockNs(at);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++) s.ingest(frames_[i].first, frames_[i].second);
        std::string b = slurp(dir_ + "/res/ff2f7ab6");
        s.noteResource(kResThing, Bytes(b.begin(), b.end()), "");
    }
    render::RgbaImage map(const model::Session& s, int64_t clockNs = kT0) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s, res_, clockNs, dl, st, &err)) {
            qWarning("assemble: %s", err.c_str());
            return {};
        }
        return render::execute(dl);
    }
    // The pixel (px, py) of world tile (x, y) on a 9x9 view whose bottom-left tile is (x0, 1), pixels from the tile's top-left.
    static uint32_t tile(const render::RgbaImage& f, int x0, int x, int y, int px = 16, int py = 16) {
        int c = x - x0, r = y - 1;
        const uint8_t* p = &f.px[(size_t(f.h - (r + 1) * 32 + py) * size_t(f.w) + size_t(c * 32 + px)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
#define PX(got, want)             \
    QVERIFY2(near((got), (want)), \
             qPrintable(QString("got %1 want %2").arg(got, 6, 16, QChar('0')).arg(want, 6, 16, QChar('0'))))

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/zas-world";
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
        QCOMPARE(int(frames_.size()), 57);
        for (size_t l : {35, 38, 42, 50, 54}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::VisContents));
        QCOMPARE(frames_[56 - 2].first, uint16_t(S2C::InView));  // phase 7: the player's step
        std::string b = slurp(dir_ + "/res/ff2f7ab6");
        uint32_t id = kResThing;
        std::string err;
        QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &id, &err), err.c_str());
    }

    // One obj (gasA, obj 0) in two turfs' vis_contents, gasB (obj 1) in a third; each record also restates the turf's look.
    void turfVisReachesTheModel() {
        model::Session s;
        ingestUpTo(s, 35);
        const model::World& w = s.world();
        QCOMPARE(int(w.turfVis.size()), 3);
        QCOMPARE(w.turfVis.at({3, 4, 0}), std::vector<uint32_t>{0});
        QCOMPARE(w.turfVis.at({5, 4, 0}), std::vector<uint32_t>{0});
        QCOMPARE(w.turfVis.at({4, 5, 0}), std::vector<uint32_t>{1});
        QCOMPARE(w.tiles.at({3, 4, 0}), kTileApp);
    }

    // appid 0xffff + no items empties the turf's vis_contents and leaves its appearance (the stock handler never writes it).
    void clearKeepsTheTurf() {
        model::Session s;
        ingestUpTo(s, 38);
        const model::World& w = s.world();
        QVERIFY(!w.turfVis.count({3, 4, 0}));
        QVERIFY(w.tiles.count({3, 4, 0}));
        QCOMPARE(w.tiles.at({3, 4, 0}), kTileApp);
        QCOMPARE(int(w.turfVis.size()), 2);
    }

    void phase1GasDrawnOverWhatStandsThere() {
        model::Session s;
        ingestUpTo(s, 35);
        render::RgbaImage f = map(s);
        QCOMPARE(f.w, 288);
        QCOMPARE(f.h, 288);
        PX(tile(f, 1, 4, 5), kOnTile[kLast]);
        PX(tile(f, 1, 6, 5), kOnCrate[kLast]);  // over the crate
        PX(tile(f, 1, 6, 5, 16, 4), kOnTile[kLast]);
        PX(tile(f, 1, 5, 6, 16, 4), kCyan);  // gasB: the icon's top half
        PX(tile(f, 1, 5, 6, 16, 24), kTile);
        PX(tile(f, 1, 5, 5), kMob);
        PX(tile(f, 1, 3, 5, 16, 17), 0x606060);  // a bare floor
    }
    // The translucent gas cycles its frames on the shared clock, in both turfs that hold it.
    void gasAnimates() {
        model::Session s;
        ingestUpTo(s, 35);
        for (int k = 0; k < 3; k++) {
            render::RgbaImage f = map(s, kT0 + (2 + 2 * k) * kDs);
            PX(tile(f, 1, 4, 5), kOnTile[k]);
            PX(tile(f, 1, 6, 5), kOnCrate[k]);
        }
    }
    void phase2RemovedGasLeavesTheTile() {
        model::Session s;
        ingestUpTo(s, 38);
        render::RgbaImage f = map(s);
        PX(tile(f, 1, 4, 5), kTile);
        PX(tile(f, 1, 6, 5), kOnCrate[kLast]);
    }
    void phase3SwappedGas() {
        model::Session s;
        ingestUpTo(s, 42);
        render::RgbaImage f = map(s);
        PX(tile(f, 1, 6, 5, 16, 4), kCyan);
        PX(tile(f, 1, 6, 5, 16, 20), kCrate);
        PX(tile(f, 1, 4, 5), kTile);
    }
    // gasA was put on (17,5) while out of view; the player's step resends it with the band.
    void walkInShowsGasSetOutOfView() {
        model::Session s;
        ingestUpTo(s, 50);
        QVERIFY(!s.world().turfVis.count({5, 4, 0}));  // the turfs left behind are emptied
        render::RgbaImage f = map(s);
        PX(tile(f, 11, 17, 5, 16, 17), kOnFloor[kLast]);
        PX(tile(f, 11, 16, 5, 16, 17), 0x606060);
    }
    void phase6GasMovesInView() {
        model::Session s;
        ingestUpTo(s, 54);
        render::RgbaImage f = map(s);
        PX(tile(f, 11, 16, 5, 16, 17), kOnFloor[kLast]);
        PX(tile(f, 11, 17, 5, 16, 17), 0x606060);
    }
    // The player steps onto the gassed turf: the gas (plane 12) is drawn over the mob, still animating.
    // The mob's step is ingested a second early: a mob with glide_size 0 glides 4 px per tick (800 ms a tile), and the
    // gas is sampled over the mob standing on its tile.
    void phase7GasOverTheMob() {
        model::Session s;
        ingestUpTo(s, 58, kT0 - 10 * kDs);
        for (int k = 0; k < 3; k++) {
            render::RgbaImage f = map(s, kT0 + (2 + 2 * k) * kDs);
            QCOMPARE(f.w, 288);
            PX(tile(f, 12, 16, 5), kOnMob[k]);
            PX(tile(f, 12, 16, 5, 2, 17), kOnFloor[k]);  // beside the mob
            PX(tile(f, 12, 15, 5, 16, 17), 0x606060);
        }
    }
};

QTEST_APPLESS_MAIN(Zas)
#include "zas_test.moc"
