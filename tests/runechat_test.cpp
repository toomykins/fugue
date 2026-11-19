// The SS13 speech bubble and runechat reach the map as client.images. Stock DreamSeeker draws the bubble icon over the speaker and the
// text in gold with a black outline, centred just above its tile. Glyph shapes are not graded (a stub rasterizer stands in for Qt's).
//
// Scene (tests/data/runechat-world), per spoken line:
//   bubble   : 0x0033 [img 0][appid 7: turf.dmi "wall", plane 16][loc kind 2 = mob][NPC's mob slot]
//   runechat : 0x0033 [img 1][appid 8: alpha 0, maptext 96x16 at maptext_x -32, pixel_y 30, plane 25]
//              0x0035 [img 1][appid 9: the same at alpha 255]  +  0x00f0 kind 13 [img 1] 8 -> 9 over 2 ticks, delay 1
//              (MeasureText answers 96x16, so the box is 16 high)
// Asserted: the bubble on the NPC's tile; the maptext box moved -32 / +30 px, its text in the box's bottom line, centred, gold with a
// black halo; the fade-in; and the bubble leaving with its 0x0034.
#include <QtTest>
#include <algorithm>
#include <fstream>
#include "model/world.h"
#include "net/bytes.h"
#include "render/dsmaptext.h"
#include "render/hittest.h"
#include "render/maptextpaint.h"
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
constexpr int64_t kTick = 100'000'000;  // world.fps = 10
constexpr uint32_t kGold = 0xffd700;
}  // namespace

class Runechat : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    std::string mapStyle_;
    render::ResStore res_;

    // A stub glyph: a solid block one pixel narrower than the advance and as tall as the face's ascent, so ink is exactly
    // [pen, pen + adv - 1) x [baseline - ascent, baseline).
    static void installStub() {
        render::setGlyphRasterizer([](const render::DsFace& f, char32_t cp, render::GlyphMask& out) {
            int adv = f.bitmap ? (f.em * 2) / 3 : f.em / 2;
            (void)cp;
            out.w = std::max(1, adv - 1);
            out.h = f.ascent;
            out.a.assign(size_t(out.w) * size_t(out.h), 255);
            return true;
        });
    }

    // Every frame through frames.txt line `lastLine`, all ingested at kT0.
    void ingestUpTo(model::Session& s, size_t lastLine) {
        s.setIngestClockNs(kT0);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++)  // frames_[0] is file line 2
            s.ingest(frames_[i].first, frames_[i].second);
        for (auto& [id, name] : std::map<uint32_t, std::string>{{0x4c895ab0, ""}, {0x5c894aea, ""}, {0x6075a274, ""}}) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            s.noteResource(id, Bytes(b.begin(), b.end()), name);
        }
    }

    struct Scene {
        render::DrawList dl;
        std::shared_ptr<const render::HitScene> hit;
    };
    Scene assemble(const model::Session& s, int64_t clock) {
        Scene sc;
        render::SceneStats st;
        std::string err;
        bool ok = render::assembleMapControl(s, res_, clock, sc.dl, st, &err, true, mapStyle_);
        if (!ok) qWarning("assemble: %s", err.c_str());
        sc.hit = sc.dl.hit;
        return sc;
    }
    // The NPC's tile (world 13,10) as the top-left of its 32x32 cell in the map image.
    static void npcTile(const render::HitScene& h, int& x, int& y) {
        x = (13 - h.originX) * h.tileW;
        y = h.pixH() - (10 - h.originY + 1) * h.tileH;
    }
    static const render::DrawLayer* onPlane(const render::DrawList& dl, double plane, int w, int h) {
        for (const auto& p : dl.planes)
            if (p.plane == plane)
                for (const auto& l : p.layers)
                    if (l.sprite && l.sprite->w == w && l.sprite->h == h) return &l;
        return nullptr;
    }
    static uint32_t rgb(const render::Sprite& s, int x, int y) {
        const uint8_t* p = &s.px[(size_t(y) * size_t(s.w) + size_t(x)) * 4];
        return p[3] ? (uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]) : 0xffffffffu;  // transparent = all ones
    }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/runechat-world";
        std::ifstream f(dir_ + "/frames.txt");
        QVERIFY(f);
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            frames_.push_back({uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                               sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1))});
        }
        QCOMPARE(int(frames_.size()), 89);
        // The default map control's style, verbatim from the world's skin.
        std::string dmf = slurp(dir_ + "/res/6075a274");
        size_t at = dmf.find("style = \".center");
        QVERIFY(at != std::string::npos);
        size_t q0 = dmf.find('"', at), q1 = dmf.find('"', q0 + 1);
        mapStyle_ = dmf.substr(q0 + 1, q1 - q0 - 1);
        QVERIFY(mapStyle_.find(".black_outline { -dm-text-outline: 1px black }") != std::string::npos);
        for (const char* id : {"4c895ab0", "5c894aea", "6075a274"}) {
            std::string b = slurp(dir_ + "/res/" + id);
            uint32_t claimed = uint32_t(std::stoul(id, nullptr, 16));
            std::string err;
            QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &claimed, &err), err.c_str());
        }
        installStub();
    }
    void cleanupTestCase() { render::setGlyphRasterizer(nullptr); }

    // The layout half, on the world's markup and sheet: one Small Fonts 7pt line (tmHeight 11, ascent 9), centred, gold, outlined black.
    void layoutReadsVgClasses() {
        render::DsLayout L = render::dsLayoutMaptext(
            "<span class='center maptext black_outline' style='color: #ffd700;'>Hello there 1</span>", mapStyle_, 96);
        QVERIFY(L.ok);
        QCOMPARE(L.height, 11);  // = the MeasureText height stock DreamSeeker answers for this text (96x11)
        QCOMPARE(int(L.lines.size()), 1);
        QCOMPARE(L.lines[0].align, 1);
        QCOMPARE(L.lines[0].ascent, 9);
        QCOMPARE(L.faces[size_t(L.lines[0].glyphs[0].face)].family, std::string("small fonts"));
        const render::DsPaint& p = L.paints[size_t(L.lines[0].glyphs[0].paint)];
        QCOMPARE(p.color, 0xffd700ffu);
        QVERIFY(p.outline);
        QCOMPARE(p.outlineColor, 0x000000ffu);
        QCOMPARE(int(L.lines[0].glyphs.size()), 11);  // "Hello there 1" less its two spaces
    }

    // Through the first line's frames, settled: the bubble on the NPC's tile, the runechat box above it.
    void bubbleAndRunechatAreDrawn() {
        model::Session s;
        ingestUpTo(s, 46);
        QCOMPARE(int(s.world().images.size()), 2);
        Scene sc = assemble(s, kT0 + 10 * 1'000'000'000LL);
        QVERIFY(sc.hit);
        int nx, ny;
        npcTile(*sc.hit, nx, ny);

        const render::DrawLayer* bubble = onPlane(sc.dl, 16, 32, 32);
        QVERIFY2(bubble, "no speech bubble on plane 16");
        QCOMPARE(bubble->x, nx);
        QCOMPARE(bubble->y, ny);

        const render::DrawLayer* mt = onPlane(sc.dl, 25, 96, 16);
        QVERIFY2(mt, "no runechat maptext box on plane 25");
        QCOMPARE(mt->x, nx - 32);            // maptext_x = (96 - 32) * -0.5
        QCOMPARE(mt->y, ny + 32 - 30 - 16);  // pixel_y = 32 * 0.95 = 30 up, box 16 high
        QCOMPARE(mt->alpha, 255);
        // Ink: the line is flush with the box bottom (rows 5..15 of 16: the 9 rows above the baseline are glyph), centred.
        const render::Sprite& sp = *mt->sprite;
        int minX = 96, maxX = -1, minY = 16, maxY = -1, black = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 96; x++) {
                uint32_t c = rgb(sp, x, y);
                if (c == kGold) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
                if (c == 0) black++;
            }
        QVERIFY2(maxX >= 0, "no gold text in the box");
        QCOMPARE(minY, 16 - 11);
        QCOMPARE(maxY, 16 - 11 + 9 - 1);
        render::DsLayout L = render::dsLayoutMaptext(
            "<span class='center maptext black_outline' style='color: #ffd700;'>Hello there 1</span>", mapStyle_, 96);
        QCOMPARE(minX, (96 - L.lines[0].width) / 2);  // centred: the first glyph sits half the slack in
        QVERIFY2(black > 0, "no black outline");
        QCOMPARE(rgb(sp, minX - 1, minY), 0u);  // the halo's left edge
        // And it reaches the executed frame: gold pixels above the NPC's tile.
        render::RgbaImage img = render::execute(sc.dl);
        int gold = 0;
        for (int y = ny - 14; y < ny; y++)
            for (int x = nx - 32; x < nx + 64; x++) {
                const uint8_t* p = &img.px[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
                if ((uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]) == kGold) gold++;
            }
        QVERIFY(gold > 50);
    }

    // The runechat fades in: animate(alpha 255, 2 ticks) after DD's 1-tick delay, from the alpha-0 appearance.
    // Line 46 is the chain's Count = 0 frame, which the server sends after the fade has played out and which clears it.
    void runechatFadesIn() {
        model::Session s;
        ingestUpTo(s, 45);
        Scene before = assemble(s, kT0 + kTick / 2);  // the chain has not started: the From appearance, alpha 0
        const render::DrawLayer* mt = onPlane(before.dl, 25, 96, 16);
        QVERIFY(!mt || mt->alpha == 0);
        Scene mid = assemble(s, kT0 + kTick + kTick);  // halfway through the 2-tick step
        mt = onPlane(mid.dl, 25, 96, 16);
        QVERIFY(mt);
        QVERIFY2(mt->alpha > 100 && mt->alpha < 160, std::to_string(mt->alpha).c_str());
    }

    // A text-macro marker in the maptext is not drawn: the box paints as the same text without it.
    void maptextMarkerIsNotDrawn() {
        model::Session plain, marked;
        ingestUpTo(plain, 46);
        auto keep = frames_;
        const std::string hello = "Hello there 1";
        for (auto& [type, body] : frames_) {
            if (type != S2C::Appearance) continue;
            auto at = std::search(body.begin(), body.end(), hello.begin(), hello.end());
            if (at != body.end()) body.insert(at, {0xff, 0x16});
        }
        ingestUpTo(marked, 46);
        frames_ = keep;
        Scene a = assemble(plain, kT0 + 10 * 1'000'000'000LL), b = assemble(marked, kT0 + 10 * 1'000'000'000LL);
        const render::DrawLayer* ma = onPlane(a.dl, 25, 96, 16);
        const render::DrawLayer* mb = onPlane(b.dl, 25, 96, 16);
        QVERIFY(ma && mb);
        QVERIFY(ma->sprite->px == mb->sprite->px);
    }

    // client.images -= bubble (0x0034 [0]) takes the bubble away and leaves the runechat.
    void bubbleLeavesWithItsDelete() {
        model::Session s;
        ingestUpTo(s, 48);
        QCOMPARE(int(s.world().images.size()), 1);
        Scene sc = assemble(s, kT0 + 10 * 1'000'000'000LL);
        QVERIFY(!onPlane(sc.dl, 16, 32, 32));
        QVERIFY(onPlane(sc.dl, 25, 96, 16));
    }
};

QTEST_GUILESS_MAIN(Runechat)
#include "runechat_test.moc"
