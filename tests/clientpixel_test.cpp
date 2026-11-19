// client.pixel_x/y/w/z and animate(client) against stock DreamDaemon 516.1684 (tests/data/clientpixel-world). Stock
// DreamSeeker moves the map, not the HUD, by -(pixel_x + pixel_w) across and pixel_y + pixel_z up; tiles beyond the
// view fill the edge the move reveals. The frames are split by the chat markers T10 .. END, each segment ingested at its
// scene time; the login segment 20 ds before T0. The animate(client) segments (T30, T60) carry no arrival times for the
// frames that close a chain, so their timing is tested on tests/data/clientanim-world instead.
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
constexpr int kLogin = -20;
constexpr int kMarks[] = {10, 20, 30, 50, 60, 65, 200};  // T10 .. T65, END
constexpr uint32_t kRes = 0x163d55fd;
constexpr uint32_t kFloor = 0x606060, kPlayer = 0xc82828, kHud = 0x2828dc;
int64_t at(int ds) { return kT0 + ds * kDs; }
}  // namespace

class ClientPixel : public QObject {
    Q_OBJECT
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    std::string resBytes_;
    render::ResStore res_;
    std::map<int, model::Session> upTo_;

    const model::Session& s(int ds) {
        int last = 0;
        for (size_t i = 0; i < std::size(kMarks); i++)
            if (kMarks[i] <= ds) last = int(i) + 1;
        auto [it, fresh] = upTo_.try_emplace(last);
        if (!fresh) return it->second;
        int seg = 0;
        for (const auto& [typ, body] : frames_) {
            if (typ == S2C::Text && ++seg > last) break;
            it->second.setIngestClockNs(at(seg == 0 ? kLogin : kMarks[seg - 1]));
            it->second.ingest(typ, body);
        }
        it->second.noteResource(kRes, Bytes(resBytes_.begin(), resBytes_.end()), "");
        return it->second;
    }

    // The colour of image pixel (x, y), y down, of the 11x11-tile map at scene time ds.
    uint32_t px(int ds, int x, int y) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s(ds), res_, at(ds), dl, st, &err)) {
            qWarning("assemble: %s", err.c_str());
            return 0xffffffffu;
        }
        render::RgbaImage img = render::execute(dl);
        if (img.w != 352 || img.h != 352) return 0xfffffffeu;
        const uint8_t* p = &img.px[(size_t(y) * size_t(img.w) + size_t(x)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    // The player's tile is at (160,160)-(191,191) unmoved; checks its top-left corner sits at (160 + dx, 160 + dy).
    void playerAt(int ds, int dx, int dy) {
        int x = 160 + dx, y = 160 + dy;
        QCOMPARE(px(ds, x, y), kPlayer);
        QCOMPARE(px(ds, x + 31, y + 31), kPlayer);
        QCOMPARE(px(ds, x - 1, y), kFloor);
        QCOMPARE(px(ds, x, y - 1), kFloor);
        QCOMPARE(px(ds, x + 32, y + 31), kFloor);
        QCOMPARE(px(ds, 0, 320), kHud);  // the screen object at 1,1 never moves
        QCOMPARE(px(ds, 31, 351), kHud);
        QCOMPARE(px(ds, 32, 351), kFloor);
    }

private slots:
    void initTestCase() {
        std::string dir = std::string(FUGUE_TEST_DATA_DIR) + "/clientpixel-world";
        std::ifstream f(dir + "/frames.txt");
        QVERIFY(f);
        std::string line;
        std::getline(f, line);  // header
        while (std::getline(f, line)) {
            QVERIFY(line.rfind("0x", 0) == 0);
            size_t sp = line.find(' ', line.find("len="));
            frames_.push_back({uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                               sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1))});
        }
        QCOMPARE(int(frames_.size()), 68);
        char n[16];
        std::snprintf(n, sizeof n, "%08x", kRes);
        resBytes_ = slurp(dir + "/res/" + n);
        std::string err;
        uint32_t id = kRes;
        QVERIFY2(res_.add(Bytes(resBytes_.begin(), resBytes_.end()), &id, &err), err.c_str());
    }

    void unmoved() { playerAt(kLogin + 5, 0, 0); }

    // pixel_x = 8, pixel_y = -12: the map moves 8 left and 12 up, and the rows and columns beyond the view fill the edges.
    void pixelXY() {
        const model::World& w = s(11).world();
        QCOMPARE(int(w.viewPixelX), 8);
        QCOMPARE(int(w.viewPixelY), -12);
        playerAt(11, -8, -12);
        QCOMPARE(px(11, 351, 100), kFloor);
        QCOMPARE(px(11, 100, 351), kFloor);
    }

    // pixel_w = 5, pixel_z = 7 move it like pixel_x / pixel_y.
    void pixelWZ() { playerAt(21, -5, 7); }
};

QTEST_GUILESS_MAIN(ClientPixel)
#include "clientpixel_test.moc"
