// visanim: the icon animation and stacking of vis_contents items, as stock DreamSeeker 516 draws them (tests/data/visanim-world,
// recorded from stock DreamDaemon; the same world captured in stock DreamSeeker under wine every ~0.2 s):
//   * a looping state on an item runs on the shared clock, in step with a map obj showing the same state;
//   * a finite state plays from the item's own icon start: when it is first sent (on joining a vis_contents, although
//     its icon_state was set long before) or changes icon/icon_state;
//   * the holder turf changing its icon_state, or the item joining a second turf, does not restart it;
//   * two items on one plane and layer stack in list order, the later on top.
// Each phase's frames are ingested at its own clock (frames.txt lines: phase 1 31-40, 2 41-43, 3 44-49, 4 50-54).
#include <QtTest>
#include <fstream>
#include <set>
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
constexpr int64_t kDs = 100'000'000;
constexpr int64_t kT0 = 1'000'000'000'000'000'000;
constexpr int64_t kPhase[5] = {kT0, kT0 + 100 * kDs, kT0 + 160 * kDs, kT0 + 220 * kDs, kT0 + 280 * kDs};
constexpr size_t kPhaseLine[5] = {0, 31, 41, 44, 50};
constexpr uint32_t kRes = 0x3b42c8e8;
constexpr uint32_t kRed = 0xff0000, kGreen = 0x00ff00, kBlue = 0x0000ff, kWhite = 0xffffff;
constexpr uint32_t kOrdA = 0xff8000, kOrdB = 0x8000ff;
}  // namespace

class VisAnim : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    void ingestUpTo(model::Session& s, size_t lastLine) {
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++) {
            int ph = 0;
            while (ph < 4 && i + 2 >= kPhaseLine[ph + 1]) ph++;
            s.setIngestClockNs(kPhase[ph]);
            s.ingest(frames_[i].first, frames_[i].second);
        }
        std::string b = slurp(dir_ + "/res/3b42c8e8");
        s.noteResource(kRes, Bytes(b.begin(), b.end()), "");
    }
    render::RgbaImage map(const model::Session& s, int64_t clockNs) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s, res_, clockNs, dl, st, &err)) {
            qWarning("assemble: %s", err.c_str());
            return {};
        }
        return render::execute(dl);
    }
    // The centre pixel of world tile (x, y) on the 9x9 view whose bottom-left tile is (1,1).
    static uint32_t tile(const render::RgbaImage& f, int x, int y) {
        const uint8_t* p = &f.px[(size_t(f.h - y * 32 + 16) * size_t(f.w) + size_t((x - 1) * 32 + 16)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
#define PX(got, want)         \
    QVERIFY2((got) == (want), \
             qPrintable(QString("got %1 want %2").arg(got, 6, 16, QChar('0')).arg(want, 6, 16, QChar('0'))))

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/visanim-world";
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
        QCOMPARE(int(frames_.size()), 53);
        for (size_t l : {31, 41, 44, 50}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::Text));  // "PHASE n"
        for (size_t l : {40, 49, 54}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::VisContents));
        std::string b = slurp(dir_ + "/res/3b42c8e8");
        uint32_t id = kRes;
        std::string err;
        QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &id, &err), err.c_str());
    }

    // C1's "once" was set at world start, yet it plays from when it joins (4,5); C3 in (5,6) likewise. C2 is still "plain".
    void finiteStatePlaysFromFirstSent() {
        model::Session s;
        ingestUpTo(s, 40);
        const uint32_t want[4] = {kRed, kGreen, kBlue, kBlue};
        const int at[4] = {5, 15, 25, 55};
        for (int k = 0; k < 4; k++) {
            render::RgbaImage f = map(s, kPhase[1] + at[k] * kDs);
            QCOMPARE(f.w, 288);
            PX(tile(f, 4, 5), want[k]);
            PX(tile(f, 5, 6), want[k]);
            PX(tile(f, 6, 5), kWhite);
        }
    }
    // C2 becoming "once" in place restarts it; C1, done, holds its last frame.
    void iconStateChangeRestarts() {
        model::Session s;
        ingestUpTo(s, 43);
        const uint32_t want[3] = {kRed, kGreen, kBlue};
        for (int k = 0; k < 3; k++) {
            render::RgbaImage f = map(s, kPhase[2] + (5 + 10 * k) * kDs);
            PX(tile(f, 6, 5), want[k]);
            PX(tile(f, 4, 5), kBlue);
        }
    }
    // Neither the holder's icon change (5,6) nor joining a second turf (C1 into (4,7), C3 into (6,7)) restarts an item.
    void holderAndSecondHolderDoNotRestart() {
        model::Session s;
        ingestUpTo(s, 49);
        render::RgbaImage f = map(s, kPhase[3] + 5 * kDs);
        PX(tile(f, 5, 6), kBlue);
        PX(tile(f, 4, 7), kBlue);
        model::Session s4;
        ingestUpTo(s4, 54);
        f = map(s4, kPhase[4] + 5 * kDs);
        PX(tile(f, 6, 7), kBlue);
        PX(tile(f, 4, 7), kBlue);
    }
    // A looping item (7,3) shows the same frame as the map obj at (3,3), and both cycle.
    void loopingStateOnTheSharedClock() {
        model::Session s;
        ingestUpTo(s, 40);
        std::set<uint32_t> seen;
        for (int k = 0; k < 3; k++) {
            render::RgbaImage f = map(s, kPhase[1] + (3 + 7 * k) * kDs);
            PX(tile(f, 7, 3), tile(f, 3, 3));
            seen.insert(tile(f, 7, 3));
        }
        QCOMPARE(int(seen.size()), 3);
    }
    void listOrderStacks() {
        model::Session s;
        ingestUpTo(s, 40);
        render::RgbaImage f = map(s, kPhase[1] + 5 * kDs);
        PX(tile(f, 8, 5), kOrdB);  // OA, then OB
        PX(tile(f, 8, 6), kOrdA);  // OB, then OA
    }
};

QTEST_APPLESS_MAIN(VisAnim)
#include "visanim_test.moc"
