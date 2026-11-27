// A glide re-armed before it finishes carries its unfinished offset over. Frames: tests/data/glide-world, a mob moving
// like vg's observer (no LONG_GLIDE, glide_size 32/3, a step every 3 ticks): a diagonal step glides for 141 ms but the
// next one lands after 99 ms. Stock DreamSeeker keeps the view and the sprite moving (its viewport advancer runs one
// pending offset toward the newest target); dropping the unfinished 30% instead jumps the view ~10 px a step.
// tests/data/glidenpc-world: two non-player mobs falling behind their steps, which the catch-up keeps bounded.
#include <QtTest>
#include "tests/recordedinput.h"
#include "tests/testmain.h"
#include <cmath>
#include "model/world.h"

using namespace fugue;
using namespace fugue::model;

namespace {
using Frame = testinput::TimedRecordedFrame;
// A synthetic epoch keeps replay deterministic; elapsed milliseconds and frame
// order come from the recording. This is not the stock capture's wall clock.
constexpr int64_t replayEpochNs = 1'700'000'000'000'000'000;

// The map's drawn position (image px: +x right, +y down) and the player's sprite relative to it, sampled every ms.
struct Sample {
    double mapX, mapY, spriteX, spriteY;
};
struct PlayerTrace {
    std::vector<Sample> samples;
    int ingestErrors = 0;
};

PlayerTrace replay(const std::vector<Frame>& frames) {
    Session s;
    PlayerTrace out;
    size_t k = 0;
    for (int64_t t = frames.front().elapsedMs; t <= frames.back().elapsedMs + 400; t++) {
        while (k < frames.size() && frames[k].elapsedMs <= t) {
            s.setIngestClockNs(replayEpochNs + frames[k].elapsedMs * 1'000'000);
            s.ingest(frames[k].frame.type, frames[k].frame.body);
            k++;
        }
        const World& w = s.world();
        auto it = w.atoms.find(w.selfRef | kRegistryA);
        if (!w.haveEye || it == w.atoms.end()) continue;
        int64_t now = replayEpochNs + t * 1'000'000;
        int sx, sy;
        eyeScrollOffset(w, now, sx, sy);
        const Atom& m = it->second;
        double left = m.gliding ? glideLeft(m.glide, now) : 0;
        double gx = m.glide.sx * 32 * left, gy = m.glide.sy * 32 * left;
        Sample p;
        p.mapX = -w.eyeX * 32.0 + sx;
        p.mapY = w.eyeY * 32.0 + sy;
        p.spriteX = (m.x - w.eyeX) * 32.0 + gx + sx;
        p.spriteY = -((m.y - w.eyeY) * 32.0 + gy) + sy;
        out.samples.push_back(p);
    }
    out.ingestErrors = s.ingestErrors();
    return out;
}

// glidenpc-world's two movers, every ms: how far each is drawn from its tile (tiles), and the ms of each leg's first
// step and of its last frame drawn off its tile.
struct NpcTrace {
    double worst[2] = {0, 0};
    std::vector<int64_t> legStart[2], legSettle[2];
    int ingestErrors = 0;
};

NpcTrace replayNpcs(const std::vector<Frame>& frames) {
    Session s;
    NpcTrace out;
    uint32_t ids[2] = {0, 0};
    int lastX[2] = {0, 0};
    size_t k = 0;
    for (int64_t t = frames.front().elapsedMs; t <= frames.back().elapsedMs + 1500; t++) {
        while (k < frames.size() && frames[k].elapsedMs <= t) {
            s.setIngestClockNs(replayEpochNs + frames[k].elapsedMs * 1'000'000);
            s.ingest(frames[k].frame.type, frames[k].frame.body);
            k++;
        }
        const World& w = s.world();
        int64_t now = replayEpochNs + t * 1'000'000;
        for (const auto& [id, a] : w.atoms) {  // red starts on (14,14), blue on (14,25)
            if (!a.hasPos || a.x != 14 || ids[0] == id || ids[1] == id) continue;
            if (a.y == 14 && !ids[0]) {
                ids[0] = id;
                lastX[0] = 14;
            }
            if (a.y == 25 && !ids[1]) {
                ids[1] = id;
                lastX[1] = 14;
            }
        }
        for (int i = 0; i < 2; i++) {
            auto it = w.atoms.find(ids[i]);
            if (!ids[i] || it == w.atoms.end()) continue;
            const Atom& a = it->second;
            double left = a.gliding ? glideLeft(a.glide, now) : 0;
            double off = std::hypot(a.glide.sx * left, a.glide.sy * left);
            out.worst[i] =
                std::max(out.worst[i], std::fmax(std::fabs(a.glide.sx * left), std::fabs(a.glide.sy * left)));
            bool turned = (a.x - lastX[i]) * (out.legStart[i].size() % 2 ? 1 : -1) < 0;
            if (a.x != lastX[i] && (out.legStart[i].empty() || turned)) {
                out.legStart[i].push_back(t);
                out.legSettle[i].push_back(t);
            }
            lastX[i] = a.x;
            if (off * 46.4 >= 1 && !out.legSettle[i].empty())
                out.legSettle[i].back() = t;  // a screen px at the oracle's zoom
        }
    }
    out.ingestErrors = s.ingestErrors();
    return out;
}

double largestMapStep(const std::vector<Sample>& v) {
    double worst = 0;
    for (size_t i = 1; i < v.size(); i++) {
        double d = std::hypot(v[i].mapX - v[i - 1].mapX, v[i].mapY - v[i - 1].mapY);
        if (d < 32) worst = std::max(worst, d);  // a whole-tile step is the view origin moving under a finished scroll
    }
    return worst;
}
}  // namespace

class GlideCarryTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { setModelMutation(0); }

    // A fresh one-tile glide keeps its duration; an offset of more than a tile catches up.
    void durations() {
        int64_t tick = 33'000'000;
        for (bool catchUp : {false, true}) {
            GlideMotion card = glideMotion(-1, 0, 32.f / 3, 32, tick, false, 1, catchUp);
            QCOMPARE(card.durNs, glideDurationNs(1, 0, 32.f / 3, 32, tick, false));
            QCOMPARE(glideMotion(-1, -1, 32.f / 3, 32, tick, false, 1, catchUp).durNs,
                     glideDurationNs(1, 1, 32.f / 3, 32, tick, false));
            QCOMPARE(glideMotion(-1, -1, 32.f / 3, 32, tick, true, 1, catchUp).durNs, card.durNs);
        }
        GlideMotion card = glideMotion(-1, 0, 32.f / 3, 32, tick, false, 1, true);
        GlideMotion atom = glideMotion(-1.6, 0, 32.f / 3, 32, tick, false, 1, false);
        QVERIFY(std::llabs(atom.durNs - int64_t(1.6 * 3 * double(tick))) < 1000);  // glide_size is a float
        QVERIFY(std::fabs(glideLeft(atom, 1 + atom.durNs / 2) - 0.5) < 1e-6);
        GlideMotion behind = glideMotion(-1.6, 0, 32.f / 3, 32, tick, false, 1, true);
        QVERIFY(behind.durNs > card.durNs && behind.durNs < atom.durNs);
        // Continuous where the catch-up phase ends, and played out at durNs.
        double u1 = std::log((1.6 + 1.0 / 16) / (1 + 1.0 / 16));
        int64_t t1 = 1 + int64_t(u1 / behind.rate);
        QVERIFY(std::fabs(glideLeft(behind, t1 - 1) - glideLeft(behind, t1 + 1)) < 1e-6);
        QVERIFY(std::fabs(glideLeft(behind, t1) * 1.6 - 1) < 1e-4);
        QCOMPARE(glideLeft(behind, 1 + behind.durNs), 0.0);
        QCOMPARE(glideLeft(behind, 1), 1.0);
    }

    // The view never jumps: at 1 ms sampling a diagonal 10.67 px/tick glide moves well under 2 px per ms, even when
    // catching up. Without the carry each re-armed step jumps the unfinished part of the last one.
    void viewMovesContinuously() {
        std::vector<Frame> frames; QString error;
        QVERIFY2(testinput::readTimedRecordedFrames(QString(FUGUE_TEST_DATA_DIR)+"/glide-world/frames.txt",frames,error),qPrintable(error));
        QCOMPARE(frames.size(),size_t(29));
        QCOMPARE(frames.front().elapsedMs,198);
        QCOMPARE(frames.back().elapsedMs,1782);
        const auto trace = replay(frames);
        QCOMPARE(trace.ingestErrors,0);
        const auto& v = trace.samples;
        QVERIFY(v.size() > 500);
        QVERIFY2(largestMapStep(v) < 2, qPrintable(QString("largest step %1 px").arg(largestMapStep(v))));
        setModelMutation(30);  // control: re-arm from the tile, dropping what was left
        const auto control = replay(frames);
        QCOMPARE(control.ingestErrors,0);
        double dropped = largestMapStep(control.samples);
        QVERIFY2(dropped > 8, qPrintable(QString("control's largest step %1 px").arg(dropped)));
    }

    // The player's sprite stays where the view puts it: sprite and view carry the same offset, including the first
    // step, whose Pos arrives before the same frame binds the appearance with the new glide size.
    void spriteHoldsStill() {
        std::vector<Frame> frames; QString error;
        QVERIFY2(testinput::readTimedRecordedFrames(QString(FUGUE_TEST_DATA_DIR)+"/glide-world/frames.txt",frames,error),qPrintable(error));
        QCOMPARE(frames.size(),size_t(29));
        QCOMPARE(frames.front().elapsedMs,198);
        QCOMPARE(frames.back().elapsedMs,1782);
        const auto trace = replay(frames);
        QCOMPARE(trace.ingestErrors,0);
        const auto& v = trace.samples;
        QVERIFY(v.size() > 500);
        QVERIFY(largestMapStep(v) > 0); // sprite stability must be observed during actual movement
        double worst = 0;
        for (const Sample& p : v) worst = std::max({worst, std::fabs(p.spriteX), std::fabs(p.spriteY)});
        QVERIFY2(worst <= 1, qPrintable(QString("sprite strayed %1 px").arg(worst)));
    }

    // The re-arm rule follows world.movement_mode (0x003b). LEGACY: a move that keeps going drops what was left on that
    // axis (restarts from the tile just left), a reversal keeps it, a turn drops it all. TILE or PIXEL: every move adds,
    // and two tiles is scaled back to two.
    void reArmRules() {
        World w;
        GlideMotion g = glideMotion(-1, 0, 4, 32, 100'000'000, false, 1, true);
        int64_t at = 1 + g.durNs / 4;  // a quarter of the way: 0.75 tile left
        auto arm = [&](int mx, int my, uint8_t last, double& sx, double& sy) {
            return armGlideOffset(w, &g, at, mx, my, last, sx, sy);
        };
        double sx, sy;
        QVERIFY(arm(-1, 0, 4, sx, sy));
        QCOMPARE(sx, -1.0);  // EAST again
        QVERIFY(arm(1, 0, 4, sx, sy));
        QVERIFY(std::fabs(sx - 0.25) < 1e-9);  // WEST: reversal
        QVERIFY(arm(0, -1, 4, sx, sy));
        QCOMPARE(sx, 0.0);
        QCOMPARE(sy, -1.0);  // NORTH: a turn
        w.movementMode = 2;
        QVERIFY(arm(-1, 0, 4, sx, sy));
        QVERIFY(std::fabs(sx + 1.75) < 1e-9);
        QVERIFY(arm(0, -1, 4, sx, sy));
        QVERIFY(std::fabs(sx + 0.75) < 1e-9);
        QCOMPARE(sy, -1.0);
        g = glideMotion(-1.8, -0.9, 4, 32, 100'000'000, false, 1, true);
        uint8_t last = 4;
        QVERIFY(armGlideOffset(w, &g, 1, -1, 0, last, sx, sy));
        QCOMPARE(sx, -kGlideMaxTiles);
        QVERIFY(std::fabs(sy + 0.9 * 2 / 2.8) < 1e-9);
    }

    // Non-player mobs that fall behind their steps (glidenpc-world, PIXEL_MOVEMENT_MODE) stay within two tiles, and each
    // leg settles when stock DreamSeeker's does: under the pixel oracle (100 fps, 46.4 px tiles) the diagonal mover was
    // drawn off its tile 710-720 ms after its leg's first step and the slow cardinal one 930-940 ms, over four legs each.
    void npcMoversStayBounded() {
        std::vector<Frame> frames; QString error;
        QVERIFY2(testinput::readTimedRecordedFrames(QString(FUGUE_TEST_DATA_DIR)+"/glidenpc-world/frames.txt",frames,error),qPrintable(error));
        QCOMPARE(frames.size(),size_t(43));
        QCOMPARE(frames.front().elapsedMs,100);
        QCOMPARE(frames.back().elapsedMs,3729);
        NpcTrace tr = replayNpcs(frames);
        QCOMPARE(tr.ingestErrors,0);
        const int64_t ds[2] = {715, 935};
        for (int i = 0; i < 2; i++) {
            QVERIFY2(tr.worst[i] <= kGlideMaxTiles + 1e-9,
                     qPrintable(QString("mover %1 trailed %2 tiles").arg(i).arg(tr.worst[i])));
            QCOMPARE(int(tr.legStart[i].size()), 2);
            for (size_t l = 0; l < 2; l++) {
                int64_t took = tr.legSettle[i][l] - tr.legStart[i][l];
                QVERIFY2(std::llabs(took - ds[i]) <= 30,
                         qPrintable(QString("mover %1 leg %2 settled after %3 ms").arg(i).arg(l).arg(took)));
            }
        }
        QVERIFY(tr.worst[1] > 1.9);  // the slow mover reaches the bound
        setModelMutation(31);        // control: the move only adds, never catches up or is bounded
        NpcTrace ctl = replayNpcs(frames);
        QCOMPARE(ctl.ingestErrors,0);
        QVERIFY2(ctl.worst[1] > 3, qPrintable(QString("control trailed %1 tiles").arg(ctl.worst[1])));
    }
};

FUGUE_TEST_GUILESS_MAIN(GlideCarryTest)
#include "glidecarry_test.moc"
