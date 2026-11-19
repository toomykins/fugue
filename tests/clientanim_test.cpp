// animate(client) timing against stock DreamDaemon 516.1684 (tests/data/clientanim-world). Every frame is ingested at
// the tick of the "@N" chat marker before it. The expected offsets are what stock DreamSeeker's chain evaluator gives:
// parallel sequences overlap, a head step starts at its own StepDelay, a direct client.pixel_* set shifts a tween in
// flight, and vg's directional_recoil fired every two ticks holds the view kicked until the burst ends.
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
constexpr int64_t kTick = 100'000'000;
constexpr uint32_t kRes = 0x163d55fd;
constexpr uint32_t kPlayer = 0xc82828, kMarker = 0x28c828, kHud = 0x2828dc;
int64_t at(double tick) { return kT0 + int64_t(tick * double(kTick)); }
}  // namespace

class ClientAnim : public QObject {
    Q_OBJECT
    struct Frame {
        uint16_t typ;
        Bytes body;
        int tick;
    };
    std::vector<Frame> frames_;
    std::string resBytes_;
    render::ResStore res_;
    std::map<int, model::Session> upTo_;

    // The session holding every frame that arrived by `tick`.
    const model::Session& s(double tick) {
        int last = int(tick);
        auto [it, fresh] = upTo_.try_emplace(last);
        if (!fresh) return it->second;
        for (const Frame& f : frames_) {
            if (f.tick > last) break;
            it->second.setIngestClockNs(at(f.tick));
            it->second.ingest(f.typ, f.body);
        }
        it->second.noteResource(kRes, Bytes(resBytes_.begin(), resBytes_.end()), "");
        return it->second;
    }

    render::RgbaImage image(double tick) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s(tick), res_, at(tick), dl, st, &err)) return {};
        return render::execute(dl);
    }
    static uint32_t rgb(const render::RgbaImage& img, int x, int y) {
        const uint8_t* p = &img.px[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }

    // The view offset at `tick`, read off where the player's tile is drawn: it sits at (160,160) unmoved, and the map
    // moves by -x across and +y down.
    QPoint offset(double tick) {
        render::RgbaImage img = image(tick);
        for (int y = 0; y < img.h; y++)
            for (int x = 0; x < img.w; x++)
                if (rgb(img, x, y) == kPlayer) return {160 - x, y - 160};
        return {9999, 9999};
    }

private slots:
    void initTestCase() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/clientanim-world";
        std::ifstream f(dir + "/frames.txt");
        QVERIFY(f);
        std::string line;
        std::getline(f, line);  // header
        int tick = 0;
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            Frame fr{uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                     sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)), 0};
            if (fr.typ == S2C::Text && fr.body.size() > 1 && fr.body[0] == '@')
                tick = std::stoi(std::string(fr.body.begin() + 1, fr.body.end()));
            fr.tick = tick;
            frames_.push_back(std::move(fr));
        }
        QCOMPARE(int(frames_.size()), 609);
        char n[16];
        std::snprintf(n, sizeof n, "%08x", kRes);
        resBytes_ = slurp(dir + "/res/" + n);
        std::string err;
        uint32_t id = kRes;
        QVERIFY2(res_.add(Bytes(resBytes_.begin(), resBytes_.end()), &id, &err), err.c_str());
    }

    // animate(pixel_x = 32, time = 20, PARALLEL) at 31, then animate(pixel_y = 32, time = 20, PARALLEL) at 36: the
    // frame at 36 re-sends the first step and adds the second with flags 0x4 and StepDelay 0, so y runs 36 .. 56
    // while x runs 31 .. 51.
    void parallelOverlap() {
        QCOMPARE(offset(30), QPoint(0, 0));
        QCOMPARE(offset(36), QPoint(8, 0));
        QCOMPARE(offset(41), QPoint(16, 8));
        QCOMPARE(offset(51), QPoint(32, 24));
        QCOMPARE(offset(56), QPoint(32, 32));
        render::RgbaImage img = image(56);  // the marker past the east edge fills the revealed column; the HUD stays
        QCOMPARE(rgb(img, 340, 200), kMarker);
        QCOMPARE(rgb(img, 0, 320), kHud);
        QCOMPARE(offset(65), QPoint(32, 32));  // the Count = 0 frame at 60 clears the chain; 0x0007 still says 32, 32
        QCOMPARE(offset(66), QPoint(0, 0));
    }

    // vg's directional_recoil shape: a 4-tick SINE | EASE_OUT kick to (20, 8) at 76, then at 81 the return, sent as a
    // head step (flags 0x40000080, StepDelay 0) behind the re-sent kick. The return starts at 81, not where the kick
    // ended (80): the view holds (20, 8) until then.
    void kickThenReturn() {
        QCOMPARE(offset(78), QPoint(14, 5));
        QCOMPARE(offset(80), QPoint(20, 8));
        QCOMPARE(offset(81), QPoint(20, 8));
        QCOMPARE(offset(87), QPoint(15, 6));  // half way through 12 ticks of SINE | EASE_IN
        QCOMPARE(offset(93), QPoint(0, 0));
    }

    // A 2-tick kick at 227 and the return at 232: the return is the head step of the frame at 232 (StepDelay 0), so it
    // starts at 232, three ticks after the kick ended. Stock DreamSeeker measured 0.49 s from kick to return.
    void headStartsAtStepDelay() {
        QCOMPARE(offset(229), QPoint(20, 0));
        QCOMPARE(offset(232), QPoint(20, 0));
        QCOMPARE(offset(238), QPoint(15, 0));
        QCOMPARE(offset(244), QPoint(0, 0));
    }

    // animate(pixel_x = 32, time = 20) at 101; pixel_x = 6 at 106 (a 0x0007, no 0x00f0): the tween keeps its timing and
    // ends on 6, shifted by the -26 the set made.
    void directSetShiftsTween() {
        QCOMPARE(offset(105), QPoint(6, 0));
        QCOMPARE(offset(106), QPoint(-18, 0));
        QCOMPARE(offset(111), QPoint(-10, 0));
        QCOMPARE(offset(121), QPoint(6, 0));
        QCOMPARE(offset(130), QPoint(6, 0));
    }

    // The same tween with pixel_y = 6 set at 146: y moves at once, x carries on.
    void directSetOtherAxis() {
        QCOMPARE(offset(146), QPoint(8, 6));
        QCOMPARE(offset(151), QPoint(16, 6));
        QCOMPARE(offset(161), QPoint(32, 6));
    }

    // directional_recoil eight times, two ticks apart, from 181: each return (a head) starts from the kick before it
    // and each new kick (a parallel) adds 16 on top, so the view stays kicked between 16 and 30 until the last return
    // ends at 200.
    void recoilBurst() {
        QCOMPARE(offset(182), QPoint(16, 0));
        QCOMPARE(offset(185), QPoint(16, 0));
        QCOMPARE(offset(185.5), QPoint(27, 0));
        QCOMPARE(offset(186), QPoint(30, 0));
        QCOMPARE(offset(192), QPoint(30, 0));
        QCOMPARE(offset(198), QPoint(14, 0));
        QCOMPARE(offset(200), QPoint(0, 0));
    }
};

QTEST_GUILESS_MAIN(ClientAnim)
#include "clientanim_test.moc"
