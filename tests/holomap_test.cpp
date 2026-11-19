// vg's station holomap and bhangmeter reach the map: a client.image attached to an iconless client.screen object.
// Scene (tests/data/holomap-world): the image is an icon drawn at runtime (sent as a DDMI resource) with a marker overlay
// at pixel offset 64,64, its loc a screen object at "SOUTH,WEST" with no icon, beside a plane master. Stock DreamSeeker
// draws the image from that screen cell's bottom-left corner, as it would an overlay of the screen object.
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
constexpr uint32_t kFloor = 0x606060, kDrawn = 0x00ff00, kMarker = 0xff00ff;
constexpr uint32_t kResIDs[] = {0x31476ae9, 0xf2a2bdea, 0xb0ae6a88};  // canvas.dmi, thing.dmi, the drawn icon
constexpr size_t kImageLine = 34;  // frames.txt line of the 0x0033 that adds the image to client.images
}  // namespace

class Holomap : public QObject {
    Q_OBJECT
    std::string dir_;
    std::vector<std::pair<uint16_t, Bytes>> frames_;
    render::ResStore res_;

    void ingestUpTo(model::Session& s, size_t lastLine) {
        s.setIngestClockNs(kT0);
        for (size_t i = 0; i < frames_.size() && i + 2 <= lastLine; i++)  // frames_[0] is file line 2
            s.ingest(frames_[i].first, frames_[i].second);
        for (uint32_t id : kResIDs) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            s.noteResource(id, Bytes(b.begin(), b.end()), "");
        }
    }
    render::RgbaImage frame(const model::Session& s) {
        render::DrawList dl;
        render::SceneStats st;
        std::string err;
        if (!render::assembleMapControl(s, res_, kT0 + 10 * 1'000'000'000LL, dl, st, &err, false)) {
            qWarning("assemble: %s", err.c_str());
            return {};
        }
        return render::execute(dl);
    }
    // The colour at (x, y) counted from the viewport's bottom-left corner.
    static uint32_t at(const render::RgbaImage& img, int x, int y) {
        const uint8_t* p = &img.px[(size_t(img.h - 1 - y) * size_t(img.w) + size_t(x)) * 4];
        return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/holomap-world";
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
        QCOMPARE(int(frames_.size()), 35);
        QCOMPARE(frames_[kImageLine - 2].first, uint16_t(S2C::ImageCreate));
        for (uint32_t id : kResIDs) {
            char n[16];
            std::snprintf(n, sizeof n, "%08x", id);
            std::string b = slurp(dir_ + "/res/" + n);
            std::string err;
            QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &id, &err), err.c_str());
        }
    }

    void absentBeforeClientImages() {
        model::Session s;
        ingestUpTo(s, kImageLine - 1);
        render::RgbaImage img = frame(s);
        QCOMPARE(img.w, 288);
        QCOMPARE(img.h, 288);
        QCOMPARE(at(img, 48, 48), kFloor);
        QCOMPARE(at(img, 80, 80), kFloor);
    }

    // The drawn box (icon pixels 33..64) and the marker overlay (ink 8..23, at +64,+64), from the SOUTH,WEST cell.
    void drawnFromItsScreenCell() {
        model::Session s;
        ingestUpTo(s, frames_.size() + 1);
        render::RgbaImage img = frame(s);
        QCOMPARE(img.w, 288);
        QCOMPARE(at(img, 16, 16), kFloor);
        QCOMPARE(at(img, 32, 32), kDrawn);
        QCOMPARE(at(img, 63, 63), kDrawn);
        QCOMPARE(at(img, 64, 64), kFloor);
        QCOMPARE(at(img, 31, 48), kFloor);
        QCOMPARE(at(img, 72, 72), kMarker);
        QCOMPARE(at(img, 87, 87), kMarker);
        QCOMPARE(at(img, 88, 80), kFloor);
    }
};

QTEST_GUILESS_MAIN(Holomap)
#include "holomap_test.moc"
