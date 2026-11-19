// particles: BYOND particle sets (an obj's `particles`) as vg uses them, its ZAS airflow dust among them
// (code/game/objects/effects/particles_system.dm flying_dust: a nullspace holder in a turf's vis_contents).
//
// The scene comes from a recorded session (tests/data/particles-world: every s2c frame of that contrast world as served by
// stock DreamDaemon 516). frames.txt line 26 is the 0x0008 carrying all ten sets, 28 the turf vis_contents holding the
// dust's holder; then the driver's phases: 29 the dust's velocity (0,3), 30 its spawning 0, 31 particles = null (particleID
// 0xffff), 32 a new datum.
//
// The pixels asserted are stock DreamSeeker 516.1684's for the same world (pixel oracle, world.fps 10, map at 32 px a
// tile): the placement probes exactly, the streams by their statistics (no per-particle RNG parity is expected). Stock
// steps particles on the client frame, so at world.fps 10 its dot stream is still between ticks.
#include <QtTest>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include "model/particles.h"
#include "model/world.h"
#include "net/bytes.h"
#include "render/pngwrite.h"
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
constexpr int64_t kT0 = 1'000'000'000'000'000'000;  // a whole number of ticks
constexpr int64_t kDs = 100'000'000;
constexpr uint32_t kResSq = 0x46cdb13c, kResP = 0x7b19df8d;

// A pixel window around one tile of the 15x15 view (bottom-left (1,1)): coordinates from the tile's top-left.
struct Px {
    int x, y;
    uint8_t r, g, b;
};
std::vector<Px> around(const render::RgbaImage& f, int tx, int ty, int pad = 64) {
    std::vector<Px> out;
    int x0 = (tx - 1) * 32, y0 = (15 - ty) * 32;
    for (int y = y0 - pad; y < y0 + 32 + pad; y++)
        for (int x = x0 - pad; x < x0 + 32 + pad; x++) {
            if (x < 0 || y < 0 || x >= f.w || y >= f.h) continue;
            const uint8_t* p = &f.px[(size_t(y) * size_t(f.w) + size_t(x)) * 4];
            if (p[0] | p[1] | p[2]) out.push_back({x - x0, y - y0, p[0], p[1], p[2]});
        }
    return out;
}
}  // namespace

class Particles : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    void ingestUpTo(model::Session& s, size_t lastLine, int64_t clock = kT0) {
        s.setIngestClockNs(clock);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++) s.ingest(frames_[i].first, frames_[i].second);
        for (uint32_t id : {kResSq, kResP}) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            s.noteResource(id, Bytes(b.begin(), b.end()), "");
        }
    }
    void ingestLine(model::Session& s, size_t line) { s.ingest(frames_[line - 2].first, frames_[line - 2].second); }
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
    // FUGUE_PARTICLES_DUMP=<dir>: the frames the statistics below are taken over, as PNGs (for comparing by eye).
    void dump(const render::RgbaImage& f, int k) {
        const char* d = std::getenv("FUGUE_PARTICLES_DUMP");
        if (!d) return;
        char n[64];
        std::snprintf(n, sizeof n, "/f%04d.png", k);
        auto png = render::encodePng(f);
        std::ofstream(std::string(d) + n, std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    }
    const model::ParticleSet& set(const model::Session& s, uint32_t obj) { return s.world().particles.at(obj); }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/particles-world";
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
        QCOMPARE(int(frames_.size()), 31);
        for (size_t l : {26, 29, 30, 31, 32}) QCOMPARE(frames_[l - 2].first, uint16_t(S2C::InView));
        for (uint32_t id : {kResSq, kResP}) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            uint32_t want = id;
            std::string err;
            QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &want, &err), err.c_str());
        }
    }

    // The fixed block and the keyed generators, as stock DreamDaemon sent them for the world's /particles types.
    void decodeReachesTheModel() {
        model::Session s;
        ingestUpTo(s, 26);
        const auto& ps = s.world().particles;
        QCOMPARE(int(ps.size()), 10);
        const model::ParticleSet& dots = set(s, 0);
        QCOMPARE(int(dots.width), 200);
        QCOMPARE(int(dots.count), 100);
        QCOMPARE(dots.spawning, 1.0f);
        QCOMPARE(dots.bound1[0], -1000.0f);
        QCOMPARE(dots.bound2[2], 1000.0f);
        QCOMPARE(int(dots.gen[model::PLifespan].type), 0x80);
        QCOMPARE(dots.gen[model::PLifespan].a[0], 20.0f);
        QCOMPARE(dots.gen[model::PFade].a[0], 10.0f);
        QCOMPARE(int(dots.gen[model::PVelocity].type), 0x81);
        QCOMPARE(dots.gen[model::PVelocity].a, (std::vector<float>{1.5f, 0, 0}));
        QCOMPARE(dots.gen[model::PColor].colors, std::vector<uint32_t>{0xff00ff00u});  // #00FF00, RGBA bytes
        QVERIFY(!dots.gen[model::PIcon].present());

        const model::ParticleSet& g = set(s, 7);  // /particles/gens
        QCOMPARE(int(g.width), 64);
        QCOMPARE(int(g.height), 32);
        QCOMPARE(int(g.count), 7);
        QCOMPARE(g.spawning, 0.25f);
        QCOMPARE(g.bound1, (std::array<float, 3>{-50, -60, -70}));
        QCOMPARE(g.bound2, (std::array<float, 3>{51, 61, 71}));
        QCOMPARE(g.gravity, (std::array<float, 3>{0.01f, 0.02f, 0.03f}));
        auto& G = g.gen;
        QCOMPARE(int(G[model::PLifespan].type), 0);
        QCOMPARE(G[model::PLifespan].b[0], 9.0f);
        QCOMPARE(int(G[model::PFade].rand), 1);    // NORMAL_RAND
        QCOMPARE(int(G[model::PFadein].rand), 2);  // LINEAR_RAND
        QCOMPARE(int(G[model::PColor].type), 2);
        QCOMPARE(G[model::PColor].colors, (std::vector<uint32_t>{0xff0000ffu, 0xffff0000u}));
        QCOMPARE(int(G[model::PPosition].type), 0x21);
        QCOMPARE(int(G[model::PPosition].sub), 0);  // circle
        QCOMPARE(int(G[model::PVelocity].sub), 1);
        QCOMPARE(int(G[model::PVelocity].rand), 3);  // sphere, SQUARE_RAND
        QCOMPARE(int(G[model::PDrift].sub), 2);      // square
        QCOMPARE(int(G[model::PGrow].sub), 3);       // cube
        QCOMPARE(int(G[model::PScale].type), 1);     // vector
        QCOMPARE(int(G[model::PRotation].type), 0);
        QCOMPARE(G[model::PRotation].a[0], -45.0f);
        QCOMPARE(int(G[model::PSpin].type), 0);
        QCOMPARE(int(G[model::PFriction].type), 0);
        QCOMPARE(int(G[model::PIcon].ids.size()), 2);
        QCOMPARE(G[model::PIcon].ids[0].second, 2.0f);
        QCOMPARE(G[model::PIconState].strs[0].first, std::string("sq"));
        QCOMPARE(G[model::PIconState].strs[1].first, std::string("dust"));
        QCOMPARE(G[model::PIconState].strs[1].second, 1.0f);

        const model::ParticleSet& gr = set(s, 8);  // /particles/grad: matrix(1, 0.5, 2, 0, 1, 3)
        QCOMPARE(int(gr.gradient.size()), 3);
        QCOMPARE(gr.gradient[1].pos, 0.5f);
        QCOMPARE(gr.gradient[1].rgba, 0xff00ff00u);
        QVERIFY(gr.gradientLoop);
        QVERIFY(gr.hasTransform);
        // a,d | b,e | c,f fill the x, y and z rows
        QCOMPARE(gr.transform[0], 1.0f);
        QCOMPARE(gr.transform[1], 0.0f);
        QCOMPARE(gr.transform[4], 0.5f);
        QCOMPARE(gr.transform[5], 1.0f);
        QCOMPARE(gr.transform[8], 2.0f);
        QCOMPARE(gr.transform[9], 3.0f);
        QCOMPARE(gr.transform[12], 0.0f);
        QCOMPARE(gr.transform[15], 1.0f);
        QCOMPARE(int(gr.gen[model::PColorChange].type), 0x80);
        QCOMPARE(int(gr.gen[model::PVelocity].type), 0x20);  // box

        const model::ParticleSet& x4 = set(s, 6);  // a 4x4 list: translation in the w row
        QCOMPARE(x4.transform[12], 6.0f);
        QCOMPARE(x4.transform[13], 9.0f);
    }

    // The same /particles datum updated keeps its particles; null detaches the set; a new datum starts empty.
    void updatesAndDetach() {
        model::Session s;
        ingestUpTo(s, 28);
        for (int k = 0; k <= 30; k++) (void)map(s, kT0 + k * kDs);
        const model::ParticleSet& d = set(s, 9);
        QCOMPARE(int(d.pid), 0x1b);
        int alive = int(d.live.size());
        QVERIFY2(alive >= 15, qPrintable(QString::number(alive)));
        ingestLine(s, 29);
        QCOMPARE(set(s, 9).gen[model::PVelocity].a, (std::vector<float>{0, 3, 0}));
        QCOMPARE(int(set(s, 9).live.size()), alive);
        ingestLine(s, 30);
        QCOMPARE(set(s, 9).spawning, 0.0f);
        (void)map(s, kT0 + 60 * kDs);  // spawning 0: the last particles live out their 10 ticks
        QVERIFY(set(s, 9).live.empty());
        QVERIFY(!model::particlesActive(set(s, 9)));
        ingestLine(s, 31);
        QVERIFY(!s.world().particles.count(9));
        ingestLine(s, 32);
        QCOMPARE(int(set(s, 9).pid), 0x1d);
        QVERIFY(set(s, 9).live.empty());
        QCOMPARE(set(s, 9).spawning, 2.0f);
    }

    // Stock DreamSeeker's placement of a still 8x8 icon particle (MEASURED): the icon centred on the holder's centre,
    // (x, y) rounded half to even, +y up; a /matrix's translation is dropped (it lands in the z row), a 4x4 list's is not;
    // a dot at (0.5, 0.5) lands on the tile's pixel (16, 15).
    void placementMatchesDreamSeeker() {
        model::Session s;
        ingestUpTo(s, 28);
        render::RgbaImage f = map(s, kT0 + 5 * kDs);
        QCOMPARE(f.w, 480);
        QCOMPARE(f.h, 480);
        struct Probe {
            const char* name;
            int tx, ty;
            uint8_t r, g, b;
            int x0, x1, y0, y1;
        };
        const Probe probes[] = {
            {"one", 3, 4, 255, 0, 0, 12, 19, 12, 19},     {"off", 6, 4, 0, 255, 0, 17, 24, 5, 12},
            {"half", 9, 4, 0, 0, 255, 14, 21, 16, 23},    {"dot1", 12, 4, 255, 255, 255, 16, 16, 15, 15},
            {"xlate", 3, 8, 255, 255, 0, 12, 19, 12, 19}, {"xlate4", 6, 8, 0, 255, 255, 18, 25, 3, 10},
        };
        for (const Probe& p : probes) {
            int x0 = 999, x1 = -999, y0 = 999, y1 = -999, n = 0;
            for (const Px& q : around(f, p.tx, p.ty, 32)) {
                if (q.r != p.r || q.g != p.g || q.b != p.b) continue;
                x0 = std::min(x0, q.x);
                x1 = std::max(x1, q.x);
                y0 = std::min(y0, q.y);
                y1 = std::max(y1, q.y);
                n++;
            }
            QVERIFY2(x0 == p.x0 && x1 == p.x1 && y0 == p.y0 && y1 == p.y1,
                     qPrintable(
                         QString("%1: x %2..%3 y %4..%5 (%6 px)").arg(p.name).arg(x0).arg(x1).arg(y0).arg(y1).arg(n)));
        }
    }

    // The green dot stream (1 a tick, 1.5 px a tick east, life 20, fade 10). Stock, over 100 frames at world.fps 10: 20
    // dots every frame on the holder's row, dx 0..29 from the centre, full intensity to dx 15 and fading after (mean
    // 223 over dx 15-19, 134 over 20-24, 45 over 25-30).
    void dotStreamMatchesDreamSeeker() {
        model::Session s;
        ingestUpTo(s, 28);
        for (int k = 0; k < 40; k++) (void)map(s, kT0 + k * kDs);
        for (int k = 0; k < 30; k++) {
            int64_t t = kT0 + (40 + k) * kDs + (k % 3) * (kDs / 3);
            render::RgbaImage f = map(s, t);
            int n = 0, minX = 99, maxX = -99;
            double sum[4] = {};
            int cnt[4] = {};
            for (const Px& q : around(f, 3, 12, 32)) {
                if (q.r || q.b || !q.g) continue;
                QCOMPARE(q.y, 16);
                n++;
                int dx = q.x - 16;
                minX = std::min(minX, dx);
                maxX = std::max(maxX, dx);
                int band = dx < 15 ? 0 : dx < 20 ? 1 : dx < 25 ? 2 : 3;
                sum[band] += q.g;
                cnt[band]++;
            }
            QVERIFY2(n >= 19 && n <= 20, qPrintable(QString::number(n)));
            QCOMPARE(minX, 0);
            QVERIFY2(maxX >= 28 && maxX <= 30, qPrintable(QString::number(maxX)));
            QCOMPARE(sum[0] / cnt[0], 255.0);
            QVERIFY(std::fabs(sum[1] / cnt[1] - 223) < 16);
            QVERIFY(std::fabs(sum[2] / cnt[2] - 134) < 16);
            QVERIFY(std::fabs(sum[3] / cnt[3] - 45) < 16);
        }
    }

    // vg's zas_dust shape, held in a turf's vis_contents. Stock, over 100 frames: 154 lit pixels a frame (sd 2.5), centroid
    // (-16.6, -2.0) image px from the tile centre (the cloud trails 15 px west of it: 3 px a tick over a mean age of 5),
    // x within -48..25 (5th-95th percentile -40..8), y -28..27 (-21..18), mean intensity 99. Then velocity (0,3) moves it
    // north.
    void dustCloudMatchesDreamSeeker() {
        model::Session s;
        ingestUpTo(s, 28);
        double px = 0, cx = 0, cy = 0, inten = 0;
        int frames = 0, minX = 99, maxX = -99, minY = 99, maxY = -99;
        for (int k = 0; k < 40; k++) (void)map(s, kT0 + k * kDs);
        for (int k = 0; k < 90; k++) {
            render::RgbaImage f = map(s, kT0 + (40 + k) * kDs);
            dump(f, k);
            std::vector<Px> v = around(f, 12, 12);
            double sx = 0, sy = 0;
            int n = 0;
            for (const Px& q : v) {
                if (q.r != q.g || q.g != q.b) continue;
                n++;
                sx += q.x - 16;
                sy += q.y - 16;
                inten += q.r;
                minX = std::min(minX, q.x - 16);
                maxX = std::max(maxX, q.x - 16);
                minY = std::min(minY, q.y - 16);
                maxY = std::max(maxY, q.y - 16);
            }
            QVERIFY(n > 0);
            px += n;
            cx += sx / n;
            cy += sy / n;
            frames++;
        }
        double meanPx = px / frames, meanCx = cx / frames, meanCy = cy / frames, meanI = inten / px;
        QString got = QString("px %1 centroid %2,%3 x %4..%5 y %6..%7 int %8")
                          .arg(meanPx)
                          .arg(meanCx)
                          .arg(meanCy)
                          .arg(minX)
                          .arg(maxX)
                          .arg(minY)
                          .arg(maxY)
                          .arg(meanI);
        QVERIFY2(std::fabs(meanPx - 154) < 154 * 0.12, qPrintable(got));
        QVERIFY2(std::fabs(meanCx - -16.6) < 2.0, qPrintable(got));
        QVERIFY2(std::fabs(meanCy - -2.0) < 2.0, qPrintable(got));
        QVERIFY2(minX >= -50 && minX <= -44 && maxX >= 20 && maxX <= 27, qPrintable(got));
        QVERIFY2(minY >= -32 && minY <= -24 && maxY >= 22 && maxY <= 29, qPrintable(got));
        QVERIFY2(std::fabs(meanI - 99) < 12, qPrintable(got));

        ingestLine(s, 29);  // velocity (0,3): north
        for (int k = 130; k < 160; k++) (void)map(s, kT0 + k * kDs);
        double ny = 0;
        for (int k = 0; k < 30; k++) {
            render::RgbaImage f = map(s, kT0 + (160 + k) * kDs);
            double sy = 0;
            int n = 0;
            for (const Px& q : around(f, 12, 12))
                if (q.r == q.g && q.g == q.b) {
                    sy += q.y - 16;
                    n++;
                }
            QVERIFY(n > 0);
            ny += sy / n;
        }
        QVERIFY2(ny / 30 < -12, qPrintable(QString::number(ny / 30)));
    }

    // Control: with the particle pass off (test-only scene mutation 40) the holders draw nothing at all.
    void controlWithoutParticles() {
        model::Session s;
        ingestUpTo(s, 28);
        render::setSceneMutation(40);
        render::RgbaImage f = map(s, kT0 + 5 * kDs);
        render::setSceneMutation(0);
        QVERIFY(around(f, 3, 4, 32).empty());
        QVERIFY(around(f, 12, 12).empty());
    }

    // A still map with an active set is motion; a stopped set with nothing left alive is not.
    void activeSetIsMotion() {
        model::ParticleSet p;
        p.spawning = 2;
        QVERIFY(model::particlesActive(p));
        p.spawning = 0;
        QVERIFY(!model::particlesActive(p));
        p.live.emplace_back();
        QVERIFY(model::particlesActive(p));
    }

    // The simulation's own statistics, against the definition: a steady stream holds spawning x lifespan particles (capped
    // by count); spawns are spread evenly over the step; friction 0.2 a tick, shed before each tick's move, stops a 4 px/tick
    // particle at 4 x 0.8 / 0.2 = 16 px (stock draws that stream at dx 1..16); gravity bends velocity (1,2) under (0,-0.2)
    // to its apex at 10 ticks, 10 px up.
    void simulationStatistics() {
        auto make = [](float spawning, uint16_t count, float life) {
            model::ParticleSet s;
            s.width = s.height = 200;
            s.count = count;
            s.spawning = spawning;
            s.gen[model::PLifespan].type = 0x80;
            s.gen[model::PLifespan].a = {life};
            return s;
        };
        model::ParticleSet a = make(2, 100, 10);
        for (int k = 0; k <= 50; k++) model::advanceParticles(a, kT0 + k * kDs, kDs, 1);
        QCOMPARE(int(a.live.size()), 21);  // ages 0, 0.5, ..., 10 (age == life is still alive)
        model::ParticleSet c = make(5, 12, 100);
        for (int k = 0; k <= 50; k++) model::advanceParticles(c, kT0 + k * kDs, kDs, 1);
        QCOMPARE(int(c.live.size()), 12);
        // At a 30 ms frame the clock steps 0.3 ticks: one spawn every other-or-third frame, never two.
        model::ParticleSet h = make(1, 100, 20);
        for (int k = 0; k <= 200; k++) model::advanceParticles(h, kT0 + int64_t(k) * 30'000'000, 30'000'000, 1);
        QVERIFY(std::abs(int(h.live.size()) - 21) <= 1);
        for (size_t i = 1; i < h.live.size(); i++)
            QVERIFY(std::fabs((h.live[i].born - h.live[i - 1].born) - 1.0) < 0.31);

        // A particle keeps what it spawned with (its icon_state) across steps on which nothing dies.
        model::ParticleSet ic = make(0.5f, 10, 16);
        ic.gen[model::PIcon].type = 5;
        ic.gen[model::PIcon].ids = {{1, 1.0f}};
        ic.gen[model::PIconState].type = 6;
        ic.gen[model::PIconState].strs = {{"sq", 1.0f}};
        for (int k = 0; k <= 40; k++) model::advanceParticles(ic, kT0 + k * kDs, kDs, 1);
        QCOMPARE(int(ic.live.size()), 8);
        for (const model::Particle& p : ic.live) {
            QCOMPARE(p.iconState, std::string("sq"));
            QCOMPARE(p.icon, 1u);
        }

        model::ParticleSet fr = make(0.05f, 1, 100);
        fr.gen[model::PVelocity].type = 0x81;
        fr.gen[model::PVelocity].a = {4, 0, 0};
        fr.gen[model::PFriction].type = 0x80;
        fr.gen[model::PFriction].a = {0.2f};
        for (int k = 0; k <= 80; k++) model::advanceParticles(fr, kT0 + k * kDs, kDs, 1);
        QCOMPARE(int(fr.live.size()), 1);
        QVERIFY2(std::fabs(fr.live[0].pos[0] - 16.0f) < 0.05f, qPrintable(QString::number(fr.live[0].pos[0])));

        model::ParticleSet gv = make(1, 1, 100);
        gv.gravity = {0, -0.2f, 0};
        gv.gen[model::PVelocity].type = 0x81;
        gv.gen[model::PVelocity].a = {1, 2, 0};
        model::advanceParticles(gv, kT0, kDs, 1);
        double born = gv.live.at(0).born;
        model::advanceParticles(gv, kT0 + int64_t(std::llround((born + 10) * double(kDs))), kDs, 1);
        QVERIFY(std::fabs(gv.live[0].pos[1] - 10.0f) < 0.01f);
        QVERIFY(std::fabs(gv.live[0].vel[1]) < 0.001f);
        QVERIFY(std::fabs(gv.live[0].pos[0] - 10.0f) < 0.01f);
    }

    // Generators sample inside their ranges with the shapes the names say.
    void generatorShapes() {
        std::mt19937 rng(7);
        model::ParticleGen box;
        box.type = 0x20;
        box.a = {-15, -15, 0};
        box.b = {15, 15, 0};
        model::ParticleGen circle;
        circle.type = 0x21;
        circle.sub = 0;
        circle.a = {3};
        circle.b = {6};
        model::ParticleGen sq;
        sq.type = 0x21;
        sq.sub = 2;
        sq.a = {2};
        sq.b = {2};
        model::ParticleGen num;
        num.type = 0;
        num.a = {5};
        num.b = {9};
        double meanX = 0;
        for (int i = 0; i < 2000; i++) {
            auto b = model::particleGenVec(box, rng);
            QVERIFY(b[0] >= -15 && b[0] <= 15 && b[1] >= -15 && b[1] <= 15 && b[2] == 0);
            meanX += b[0];
            auto c = model::particleGenVec(circle, rng);
            float r = std::sqrt(c[0] * c[0] + c[1] * c[1]);
            QVERIFY(r >= 2.999f && r <= 6.001f);
            auto q = model::particleGenVec(sq, rng);
            QVERIFY(std::fabs(std::max(std::fabs(q[0]), std::fabs(q[1])) - 2) < 1e-5f);
            float n = model::particleGenNum(num, rng);
            QVERIFY(n >= 5 && n <= 9);
        }
        QVERIFY(std::fabs(meanX / 2000) < 1.0);
        model::ParticleSet g;
        g.gradient = {{0, 0xff0000ffu}, {0.5f, 0xff00ff00u}, {1, 0xffff0000u}};
        g.gradientLoop = true;
        QCOMPARE(model::particleGradient(g, 0.5f), 0xff00ff00u);
        QCOMPARE(model::particleGradient(g, 1.5f), 0xff00ff00u);   // wraps
        QCOMPARE(model::particleGradient(g, 0.25f), 0xff008080u);  // halfway from red to green
    }
};

QTEST_APPLESS_MAIN(Particles)
#include "particles_test.moc"
