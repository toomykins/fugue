// The SS13 do_after progress bar reaches the map: a client.image on the player's own mob (or a crate), drawn by render/scene.cpp imagesOn.
// Scene (tests/data/progbar-world): the bar arrives as 0x0033, updates with 0x0035, is removed by 0x0034, and the next do_after moves it to the crate.
// Asserted at 32 px a tile on the composited frame. Stock DreamSeeker draws the bar one tile above its target and leaves the target's
// colour out of it (do_after sets RESET_COLOR).
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
std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
constexpr int64_t kT0 = 1'000'000'000'000'000'000;
constexpr uint32_t kGreen = 0x00ff00, kEmpty = 0x500000, kFrame = 0x000000;  // bar colours
constexpr uint32_t kResDoafter = 0x51262fcb, kResThing = 0x90df3876;
}  // namespace

class Progbar : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    // Every frame through frames.txt line `lastLine`, all ingested at kT0.
    void ingestUpTo(model::Session& s, size_t lastLine) {
        s.setIngestClockNs(kT0);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++)  // frames_[0] is file line 2
            s.ingest(frames_[i].first, frames_[i].second);
        for (uint32_t id : {kResDoafter, kResThing}) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            s.noteResource(id, Bytes(b.begin(), b.end()), "");
        }
    }

    struct Frame {
        render::RgbaImage img;
        int tileW = 0, tileH = 0, originX = 0, originY = 0, pixH = 0;
    };
    Frame frame(const model::Session& s) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        Frame f;
        if (!render::assembleMapControl(s, res_, kT0 + 10 * 1'000'000'000LL, dl, st, &err, true)) {
            qWarning("assemble: %s", err.c_str());
            return f;
        }
        f.img = render::execute(dl);
        if (dl.hit) {
            f.tileW = dl.hit->tileW;
            f.tileH = dl.hit->tileH;
            f.originX = dl.hit->originX;
            f.originY = dl.hit->originY;
            f.pixH = dl.hit->pixH();
        }
        return f;
    }
    static uint32_t at(const Frame& f, int x, int y) {
        const uint8_t* p = &f.img.px[(size_t(y) * size_t(f.img.w) + size_t(x)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    // The bar's middle row (icon row 28) over the tile at world (wx, wy), drawn one tile up by pixel_z = 32: columns 4..27
    // as 'F'rame / 'G'reen / 'e'mpty / '.' anything else (the floor).
    static std::string barRow(const Frame& f, int wx, int wy) {
        int x0 = (wx - f.originX) * f.tileW;
        int top = f.pixH - (wy - f.originY + 1) * f.tileH;  // the target tile's top edge
        int y = top - 32 + 28;
        std::string r;
        for (int x = 4; x <= 27; x++) {
            uint32_t c = at(f, x0 + x, y);
            r += c == kGreen ? 'G' : c == kEmpty ? 'e' : c == kFrame ? 'F' : '.';
        }
        return r;
    }
    static std::string bar(int n) {  // what prog_bar_<n> reads as in barRow
        int fill = 4 + 24 * n / 100;
        std::string r = "F";
        for (int x = 5; x <= 26; x++) r += x < fill ? 'G' : 'e';
        return r + "F";
    }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/progbar-world";
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
        QCOMPARE(int(frames_.size()), 145);
        QCOMPARE(frames_[35 - 2].first, uint16_t(S2C::ImageCreate));
        QCOMPARE(frames_[85 - 2].first, uint16_t(S2C::ImageDelete));
        QCOMPARE(frames_[86 - 2].first, uint16_t(S2C::ImageCreate));
        QCOMPARE(frames_[126 - 2].first, uint16_t(S2C::ImageDelete));
        for (uint32_t id : {kResDoafter, kResThing}) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            std::string err;
            QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &id, &err), err.c_str());
        }
    }

    // On the player's own mob (5,5): absent before client.images |=, then prog_bar_10, _50 and _100 as each 0x0035 lands.
    void barOnOwnMobFills() {
        std::string blank(24, '.');
        for (auto [line, want] : std::vector<std::pair<size_t, std::string>>{
                 {34, blank}, {35, bar(10)}, {55, bar(50)}, {80, bar(100)}, {85, blank}}) {
            model::Session s;
            ingestUpTo(s, line);
            Frame f = frame(s);
            QVERIFY(f.tileW == 32 && f.tileH == 32);
            QCOMPARE(QString::fromStdString(barRow(f, 5, 5)), QString::fromStdString(want));
            QCOMPARE(QString::fromStdString(barRow(f, 6, 5)), QString::fromStdString(blank));
        }
    }

    // The next do_after puts a fresh image on the crate (6,5): drawn there, untinted by the crate's colour (RESET_COLOR), and gone with its 0x0034.
    void barOnCrateThenGone() {
        std::string blank(24, '.');
        for (auto [line, want] :
             std::vector<std::pair<size_t, std::string>>{{86, bar(10)}, {122, bar(100)}, {126, blank}}) {
            model::Session s;
            ingestUpTo(s, line);
            Frame f = frame(s);
            QCOMPARE(QString::fromStdString(barRow(f, 6, 5)), QString::fromStdString(want));
            QCOMPARE(QString::fromStdString(barRow(f, 5, 5)), QString::fromStdString(blank));
        }
    }
};

QTEST_GUILESS_MAIN(Progbar)
#include "progbar_test.moc"
