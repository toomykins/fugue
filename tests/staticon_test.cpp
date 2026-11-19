// The INFO control's per-row atom icon (render::statRowIcon) against what stock DreamSeeker 516.1684 draws for the same rows.
// The world (tests/data/staticons-world/world.dm) is the alt-click turf tab: statpanel(T) then statpanel(A) for each obj on the tile,
// each varying one appearance property, plus one obj in nullspace. A 16x16 icon draws at native size, top-left of its cell;
// a 64x64 icon shrinks to 32x32; an icon-less atom shows only its overlay.
#include <QtTest>
#include <filesystem>
#include <fstream>
#include "model/statpanel.h"
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
struct Px {
    int r, g, b, a;
};
bool operator==(const Px& x, const Px& y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
constexpr Px kRed{255, 0, 0, 255}, kWhite{255, 255, 255, 255}, kBlack{0, 0, 0, 255}, kMagenta{255, 0, 255, 255},
    kDot{0, 160, 0, 255}, kCyan{0, 255, 255, 255}, kOrange{255, 128, 0, 255}, kBrown{128, 64, 0, 255},
    kNavy{0, 0, 128, 255}, kYellow{255, 255, 0, 255}, kClear{0, 0, 0, 0};
}  // namespace

class StatIcon : public QObject {
    Q_OBJECT
    model::Session sess_;
    render::ResStore res_;

    bool build() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/staticons-world";
        std::ifstream f(dir + "/frames.txt");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(typ, sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        for (const auto& e : std::filesystem::directory_iterator(dir + "/res")) {
            std::ifstream rf(e.path(), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
            Bytes b(bytes.begin(), bytes.end());
            uint32_t claimed = uint32_t(std::stoul(e.path().filename().string(), nullptr, 16));
            std::string err;
            if (!res_.add(b, &claimed, &err)) return false;
            sess_.noteResource(claimed, b, "");
        }
        return true;
    }

    // The appearance the row named `name` carries (the value column's atom; the turf's is appid 0), false = no such row.
    bool rowAppid(const std::string& name, uint32_t& appid) const {
        const model::World& w = sess_.world();
        for (const auto& l : w.stat.lines) {
            model::StatValue v = model::statValueOf(l.value);
            if (v.isAtom() && model::statText(w, l) == name) {
                appid = v.appid;
                return true;
            }
        }
        return false;
    }
    // The row's icon cell at the world's icon size, as the client builds it for the INFO control.
    bool icon(const std::string& name, std::vector<uint8_t>& px, int64_t clockNs = 0) {
        uint32_t a = 0;
        return rowAppid(name, a) && render::statRowIcon(sess_, res_, a, model::iconSizeOf(sess_.world()), clockNs, px);
    }
    static Px at(const std::vector<uint8_t>& px, int x, int y, int cell = 32) {
        const uint8_t* p = &px[(size_t(y) * size_t(cell) + size_t(x)) * 4];
        return {p[0], p[1], p[2], p[3]};
    }

private slots:
    void initTestCase() {
        QVERIFY2(build(), "the staticons world");
        QCOMPARE(model::iconSizeOf(sess_.world()), 32);
        // the Tile tab: the turf, the eleven objs on it, the nullspace obj
        int atoms = 0;
        for (const auto& l : sess_.world().stat.lines)
            if (model::statValueOf(l.value).isAtom()) atoms++;
        QCOMPARE(atoms, 13);
    }

    void rawSpriteLikeStockDreamSeeker() {
        std::vector<uint8_t> px;
        QVERIFY(icon("floor", px));
        QCOMPARE(int(px.size()), 32 * 32 * 4);
        QVERIFY(at(px, 0, 0) == (Px{64, 64, 64, 255}));
        QVERIFY(at(px, 1, 0) == (Px{96, 96, 96, 255}));
        QVERIFY(icon("o_red", px));
        QVERIFY(at(px, 2, 2) == kWhite);
        QVERIFY(at(px, 10, 10) == kRed);
        QVERIFY(at(px, 31, 31) == kRed);
        QVERIFY(icon("o_dirE", px));
        QVERIFY(at(px, 10, 10) == kMagenta);  // the appearance's dir
        QVERIFY(icon("o_ovl", px));
        QVERIFY(at(px, 10, 10) == kRed);
        QVERIFY(at(px, 28, 28) == kDot);
        QVERIFY(icon("o_col", px));
        QVERIFY(at(px, 10, 10) == kWhite);
        QVERIFY(at(px, 2, 2) == kBlack);  // colour ignored
        QVERIFY(icon("o_alpha", px));
        QVERIFY(at(px, 10, 10) == kRed);  // alpha ignored
        QVERIFY(icon("o_px", px));
        QVERIFY(at(px, 2, 2) == kWhite);
        QVERIFY(at(px, 10, 10) == kRed);  // pixel_x ignored
        QVERIFY(icon("o_null_dirW", px));
        QVERIFY(at(px, 10, 10) == kYellow);  // never on the map; still its west frame
    }

    void iconlessRows() {
        std::vector<uint8_t> px;
        uint32_t a = 0;
        QVERIFY(rowAppid("o_noicon", a));
        QVERIFY2(!icon("o_noicon", px), "no icon, no overlay: stock draws the name alone");
        QVERIFY(icon("o_noicon_ovl", px));  // the overlay alone in the cell
        QVERIFY(at(px, 10, 10) == kClear);
        QVERIFY(at(px, 28, 28) == kDot);
    }

    void cellIsTheWorldIconSize() {
        std::vector<uint8_t> px;
        QVERIFY(icon("o_16", px));  // native size, top-left
        QCOMPARE(int(px.size()), 32 * 32 * 4);
        QVERIFY(at(px, 1, 1) == kWhite);
        QVERIFY(at(px, 10, 10) == kBrown);
        QVERIFY(at(px, 15, 15) == kBrown);
        QVERIFY(at(px, 16, 10) == kClear);
        QVERIFY(at(px, 10, 16) == kClear);
        QVERIFY(at(px, 20, 20) == kClear);
        QVERIFY(icon("o_64", px));  // shrunk into the cell
        QVERIFY(at(px, 7, 7) == kWhite);
        QVERIFY(at(px, 8, 8) == kNavy);
        QVERIFY(at(px, 31, 31) == kNavy);
        // a 16-pixel cell (a world.icon_size = 16 world): every 32x32 icon shrinks, AVERAGING (stock's stripes read 88 grey)
        uint32_t floor = 0;
        QVERIFY(rowAppid("floor", floor));
        QVERIFY(render::statRowIcon(sess_, res_, floor, 16, 0, px));
        QCOMPARE(int(px.size()), 16 * 16 * 4);
        QVERIFY(at(px, 0, 0, 16) == (Px{88, 88, 88, 255}));  // a 2x2 block holding one 64 and three 96
        QVERIFY(at(px, 1, 0, 16) == (Px{96, 96, 96, 255}));
    }

    void animatesWithTheClock() {
        std::vector<uint8_t> px;
        QVERIFY(icon("o_anim", px, 0));
        QVERIFY(at(px, 10, 10) == kCyan);
        QVERIFY(icon("o_anim", px, int64_t(1'500'000'000)));
        QVERIFY(at(px, 10, 10) == kOrange);  // delay 10,10 ds: frame 2 at 1.5 s
    }
};

QTEST_GUILESS_MAIN(StatIcon)
#include "staticon_test.moc"
