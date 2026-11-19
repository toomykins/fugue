// flickz: a flick() on another z-level, and a mob's flick, as stock DreamDaemon 516 sends them (tests/data/flickz-world:
// every s2c frame of that world, the viewer at (8,8,1), view 3). DD sends every flick in the world to the client, whatever
// its z: the 0x00c7, the 0x006c, and for a movable an 0x0008/0x0007 placing it on its own z, retracted the next tick.
// Stock DreamSeeker 516.1684 (pixel oracle, the same world at fps 1, a frame every 0.26 s) draws only the viewer's own z:
// oz1 (9,8,1), tz1 and mz1 flicked, and nothing for oz2 (10,8,2), oz2u (8,8,2), oz3 (10,9,3), mz2 (7,8,2) or the area.
// frames.txt lines: 33 oz1's 0x006c, 35 its 0x0007; 39 the 0x0008 placing oz2 on z2 (38 its 0x006c); 45 oz2u; 51 oz3;
// 71 mz1's 0x00c7 (mob 0) and 72 its 0x006c.
#include <QtTest>
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
constexpr uint32_t kRes = 0xeda1cadb;
struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};
constexpr Rgb kFloor{40, 40, 40}, kObj{0, 160, 0}, kFlOz1{255, 0, 255};
}  // namespace

class Flickz : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    // Lines [from, to] of frames.txt, at one ingest clock.
    void ingest(model::Session& s, size_t from, size_t to, int64_t clock) {
        s.setIngestClockNs(clock);
        for (size_t l = from; l <= to; l++) s.ingest(frames_[l - 2].first, frames_[l - 2].second);
    }
    model::Session start(size_t to, int64_t clock) {
        model::Session s;
        ingest(s, 2, to, clock);
        std::string b = slurp(dir_ + "/res/eda1cadb");
        s.noteResource(kRes, Bytes(b.begin(), b.end()), "");
        return s;
    }
    // The pixel 5 px in from the top-left corner of map tile (x, y): a flick state's square (4 px in), not the obj's or mob's.
    Rgb at(const model::Session& s, int64_t clockNs, int x, int y, int in = 5) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s, res_, clockNs, dl, st, &err)) {
            qWarning("assemble: %s", err.c_str());
            return {};
        }
        render::RgbaImage f = render::execute(dl);
        int px = (x - 5) * 32 + in, py = (11 - y) * 32 + in;
        if (px < 0 || py < 0 || px >= f.w || py >= f.h) return {};
        const uint8_t* p = &f.px[(size_t(py) * size_t(f.w) + size_t(px)) * 4];
        return {p[0], p[1], p[2]};
    }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/flickz-world";
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
        QCOMPARE(int(frames_.size()), 128);
        for (size_t l : {33, 38, 44, 50, 72}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::FlickState));
        for (size_t l : {39, 45, 51}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::InView));
        QCOMPARE(frames_[71 - 2].first, uint16_t(S2C::FlickTarget));
        std::string b = slurp(dir_ + "/res/eda1cadb");
        uint32_t want = kRes;
        std::string err;
        QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &want, &err), err.c_str());
    }

    // The control: oz1's flick on the viewer's own z plays at its tile.
    void ownZFlickDraws() {
        model::Session s = start(35, kT0);
        QVERIFY(at(s, kT0 + 5 * kDs, 9, 8) == kFlOz1);
        QVERIFY(at(s, kT0 + 50 * kDs, 9, 8, 16) == kObj);  // played out: the obj again
    }

    // An obj on z2 or z3 that DD placed for its flick does not draw on the z1 map, neither the obj nor its flick.
    void otherZFlickDoesNotDraw() {
        struct Case {
            size_t line;
            int x, y;
        } cases[] = {{39, 10, 8}, {45, 8, 8}, {51, 10, 9}};
        for (const Case& c : cases) {
            model::Session s = start(c.line, kT0);
            QVERIFY(s.world().atoms.count(c.line == 39 ? 1u : c.line == 45 ? 2u : 3u));
            Rgb p = at(s, kT0 + 5 * kDs, c.x, c.y);
            QVERIFY2(p == kFloor, qPrintable(QString("line %1: %2,%3,%4").arg(c.line).arg(p.r).arg(p.g).arg(p.b)));
            if (c.line != 45) QVERIFY(at(s, kT0 + 5 * kDs, c.x, c.y, 16) == kFloor);  // nor the obj itself
        }
    }

    // A mob's 0x00c7 does not restart the obj flick that shares its ref number (mob 0 and obj 0 are different atoms).
    void mobFlickDoesNotReplayAnObjFlick() {
        model::Session s = start(35, kT0);
        const int64_t t1 = kT0 + 300 * kDs;
        ingest(s, 36, 72, t1);
        QVERIFY(at(s, t1 + 5 * kDs, 9, 8, 16) == kObj);
        QVERIFY(at(s, t1 + 5 * kDs, 9, 8) == kFloor);
    }
};

QTEST_GUILESS_MAIN(Flickz)
#include "flickz_test.moc"
