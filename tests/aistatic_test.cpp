// aistatic: an atom's render_target and the render_source images that relay it, as stock DreamDaemon 516 sends them
// (tests/data/aistatic-world: every s2c frame of that world, the viewer at (8,8), view 3). vg's AI camera static is
// this shape: a client.screen obj with render_target "*aistatic", relayed by an /image on every obscured turf.
// Stock DreamSeeker 516.1684 (pixel oracle, the same world) draws the "*st" screen obj's icon on every relay turf
// (x < 8) and not at its own screen_loc 4,4 (over the viewer, who stays visible); "nst" (no *) draws both at its
// screen_loc 7,7 and on its relay turf (10,10).
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
constexpr uint32_t kRes = 0xa8be0202;
struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};
constexpr Rgb kFloor{40, 40, 40}, kMob{0, 0, 200}, kGrey{128, 128, 128}, kRed{255, 0, 0}, kYellow{255, 255, 0},
    kWhite{255, 255, 255}, kBlue{0, 128, 255};
QString str(Rgb c) { return QString("%1,%2,%3").arg(c.r).arg(c.g).arg(c.b); }
}  // namespace

class Aistatic : public QObject {
    Q_OBJECT
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;
    model::Session s_;

    // The pixel (ix, iy) px in from the top-left corner of map tile (x, y).
    Rgb at(int64_t clockNs, int x, int y, int ix, int iy) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s_, res_, clockNs, dl, st, &err)) {
            qWarning("assemble: %s", err.c_str());
            return {};
        }
        render::RgbaImage f = render::execute(dl);
        int px = (x - 5) * 32 + ix, py = (11 - y) * 32 + iy;
        if (px < 0 || py < 0 || px >= f.w || py >= f.h) return {};
        const uint8_t* p = &f.px[(size_t(py) * size_t(f.w) + size_t(px)) * 4];
        return {p[0], p[1], p[2]};
    }

private slots:
    void initTestCase() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/aistatic-world";
        std::ifstream f(dir + "/frames.txt");
        QVERIFY(f);
        std::string line;
        std::getline(f, line);
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            frames_.push_back({uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                               sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1))});
        }
        QCOMPARE(int(frames_.size()), 137);
        std::string b = slurp(dir + "/res/a8be0202");
        uint32_t want = kRes;
        std::string err;
        QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &want, &err), err.c_str());
        s_.setIngestClockNs(kT0);
        for (auto& [op, body] : frames_) s_.ingest(op, body);
        s_.noteResource(kRes, Bytes(b.begin(), b.end()), "");
    }

    // Every relay turf shows the source's icon (a red or yellow top-left quadrant on grey); the floor east of them does not.
    void relaysDrawTheSource() {
        const int64_t t = kT0 + 5 * kDs;
        for (int x = 5; x <= 7; x++)
            for (int y = 5; y <= 11; y++) {
                Rgb q = at(t, x, y, 4, 4), r = at(t, x, y, 24, 24);
                QVERIFY2((q == kRed || q == kYellow) && r == kGrey,
                         qPrintable(QString("(%1,%2): %3 / %4").arg(x).arg(y).arg(str(q), str(r))));
            }
        QVERIFY(at(t, 9, 6, 4, 4) == kFloor);
    }

    // The relays follow the source's icon animation (2 frames of 1 s): one frame red, the next yellow.
    void relaysAnimateWithTheSource() {
        Rgb a = at(kT0 + 5 * kDs, 6, 8, 4, 4), b = at(kT0 + 15 * kDs, 6, 8, 4, 4);
        QVERIFY2(a == kRed && b == kYellow, qPrintable(str(a) + " / " + str(b)));
    }

    // A "*" render_target is not drawn at its own screen_loc: the viewer under screen 4,4 stays visible.
    void starTargetIsNotDrawn() {
        Rgb c = at(kT0 + 5 * kDs, 8, 8, 16, 16), q = at(kT0 + 5 * kDs, 8, 8, 4, 4);
        QVERIFY2(c == kMob && q == kFloor, qPrintable(str(c) + " / " + str(q)));
    }

    // A render_target without "*" draws at its screen_loc (7,7 = tile (11,11)) and on its relay turf (10,10).
    void plainTargetDrawsBoth() {
        const int64_t t = kT0 + 5 * kDs;
        QVERIFY(at(t, 11, 11, 0, 0) == kWhite && at(t, 11, 11, 16, 16) == kBlue);
        Rgb e = at(t, 10, 10, 0, 0), c = at(t, 10, 10, 16, 16);
        QVERIFY2(e == kWhite && c == kBlue, qPrintable(str(e) + " / " + str(c)));
    }
};

QTEST_GUILESS_MAIN(Aistatic)
#include "aistatic_test.moc"
