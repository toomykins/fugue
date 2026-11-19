// DDMI recipes for the /icon procs a world builds icons with (DrawBox, Blend at an offset, SwapColor, MapColors,
// SetIntensity, Shift, Flip, Turn, Scale, Crop, Insert, icon() of a state, dir, frame or movement, icon arithmetic, and
// chains of them), run as this client runs them.
//
// Scene (tests/data/ddmiops-world): one object per case, each showing an /icon built at runtime (world.dm). frames.txt and
// res/ are what stock DreamDaemon sent (`fuguedump record`); cases.tsv names each case's resource and turf. Four cases
// grow past what DreamDaemon ships as a recipe and arrive as rasters; they draw like any icon.
//   dmi/<case>.dmi     DreamDaemon's own result (fcopy of the finished /icon): every state, dir, frame and delay must match.
//   dreamseeker.png    stock DreamSeeker's map control for the same world: the whole drawn map must match.
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include "model/world.h"
#include "net/bytes.h"
#include "render/ddmi.h"
#include "render/dmi.h"
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
// cases.tsv: name, resource id, and the turf (x, y) its object stands on, in the order world.dm builds them.
struct Case {
    std::string name, res;
    int x = 0, y = 0;
};
std::vector<Case> cases(const std::string& p) {
    std::vector<Case> out;
    std::ifstream f(p);
    Case c;
    while (f >> c.name >> c.res >> c.x >> c.y) out.push_back(c);
    return out;
}

}  // namespace

class DdmiOps : public QObject {
    Q_OBJECT
    std::string dir_;
    model::Session sess_;
    render::ResStore res_;

private slots:
    void initTestCase() {
        dir_ = std::string(FUGUE_TEST_DATA_DIR) + "/ddmiops-world";
        std::ifstream f(dir_ + "/frames.txt");
        QVERIFY(f);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            size_t sp = line.find(' ', line.find("len="));
            sess_.ingest(uint16_t(std::stoul(line.substr(2, 4), nullptr, 16)),
                         sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1)));
        }
        QDir rd(QString::fromStdString(dir_ + "/res"));
        for (const QString& n : rd.entryList(QDir::Files)) {
            std::string b = slurp(dir_ + "/res/" + n.toStdString());
            uint32_t id = n.toUInt(nullptr, 16);
            std::string err;
            QVERIFY2(res_.add(Bytes(b.begin(), b.end()), &id, &err), err.c_str());
        }
    }

    void everyCaseEqualsDreamDaemon_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("res");
        for (const Case& c : cases(std::string(FUGUE_TEST_DATA_DIR) + "/ddmiops-world/cases.tsv"))
            QTest::newRow(c.name.c_str()) << QString::fromStdString(c.name) << QString::fromStdString(c.res);
    }
    void everyCaseEqualsDreamDaemon() {
        QFETCH(QString, name);
        QFETCH(QString, res);
        uint32_t id = res.toUInt(nullptr, 16);
        const render::Icon* ic = res_.iconResolved(id, sess_.world().resources);
        QVERIFY2(ic, res_.refusedRecipes().count(id) ? res_.refusedRecipes().at(id).c_str() : "unresolved");
        std::string dmi = slurp(dir_ + "/dmi/" + name.toStdString() + ".dmi");
        render::ParseResult ref = render::parseIcon(reinterpret_cast<const uint8_t*>(dmi.data()), dmi.size());
        QVERIFY(ref.ok());
        const render::Icon& want = *ref.icon;
        QCOMPARE(ic->width, want.width);
        QCOMPARE(ic->height, want.height);
        QCOMPARE(ic->states.size(), want.states.size());
        int bad = 0;
        QString first;
        for (size_t si = 0; si < want.states.size(); si++) {
            const render::IconState &w = want.states[si], &g = ic->states[si];
            QCOMPARE(QString::fromStdString(g.name), QString::fromStdString(w.name));
            QCOMPARE(g.dirs, w.dirs);
            QCOMPARE(g.frames, w.frames);
            QCOMPARE(g.movement, w.movement);
            if (w.frames > 1)
                for (int64_t f = 0; f < w.frames; f++)
                    QVERIFY2(std::fabs((f < int64_t(g.delays.size()) ? g.delays[size_t(f)] : 1.0) -
                                       w.delays[size_t(f)]) < 1e-5,
                             qPrintable(QString("frame %1 delay: DD %2 ours %3")
                                            .arg(f)
                                            .arg(w.delays[size_t(f)])
                                            .arg(f < int64_t(g.delays.size()) ? g.delays[size_t(f)] : 1.0)));
            for (int64_t d = 0; d < w.dirs; d++)
                for (int64_t f = 0; f < w.frames; f++) {
                    std::vector<uint8_t> gc, wc;
                    std::string err;
                    QVERIFY2(ic->frame(g, d, f, gc, &err), err.c_str());
                    QVERIFY2(want.frame(w, d, f, wc, &err), err.c_str());
                    for (size_t i = 0; i + 3 < wc.size(); i += 4) {
                        bool same = wc[i + 3] == 0 ? gc[i + 3] == 0 : std::memcmp(&wc[i], &gc[i], 4) == 0;
                        if (!same && bad++ == 0) {
                            size_t px = i / 4;
                            first = QString("state %1 dir %2 frame %3 (%4,%5): DD %6,%7,%8,%9 ours %10,%11,%12,%13")
                                        .arg(si)
                                        .arg(d)
                                        .arg(f)
                                        .arg(px % size_t(want.width))
                                        .arg(px / size_t(want.width))
                                        .arg(wc[i])
                                        .arg(wc[i + 1])
                                        .arg(wc[i + 2])
                                        .arg(wc[i + 3])
                                        .arg(gc[i])
                                        .arg(gc[i + 1])
                                        .arg(gc[i + 2])
                                        .arg(gc[i + 3]);
                        }
                    }
                }
        }
        // DreamDaemon rotates in x87 precision and DreamSeeker in float32, so the two settle a 45 degree turn's exact ties
        // differently; this client follows DreamSeeker (mapEqualsDreamSeeker).
        const int allowed = name == "c_turn45" ? 2 : 0;
        QVERIFY2(bad == allowed, qPrintable(QString("%1 px differ, first %2").arg(bad).arg(first)));
    }

    // What this client cannot run is refused (the icon is not drawn), never approximated; the test-only mutation that draws
    // the first operand instead proves each refusal is observable.
    void unsupportedRecipesAreRefused_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("op");  // refused at an op (the mutation applies), not while extracting an operand
        const QByteArray grad = QByteArray::fromHex("010100000008056772616400");  // ref 1 (src.dmi), state "grad"
        QTest::newRow("op 0x06") << grad + QByteArray::fromHex("40ffffff06") << true;
        QTest::newRow("a value left on the stack") << grad + QByteArray::fromHex("40ffffff03ff0000ff") << true;
        QTest::newRow("MapColors with 16 coefficients")
            << grad + QByteArray::fromHex("40ffffff4a10") + QByteArray(64, '\0') << true;
        QTest::newRow("Blend with a number") << grad + QByteArray::fromHex("40ffffff04000000004200") << true;
        QTest::newRow("Insert of another size") << grad + QByteArray::fromHex("40ffffff010200000008056669") +
                                                       QByteArray::fromHex(
                                                           "6c6c0040ffffff057800"
                                                           "41ffff0000000000")
                                                << true;
    }
    void unsupportedRecipesAreRefused() {
        QFETCH(QByteArray, body);
        QFETCH(bool, op);
        QByteArray raw = QByteArray("DDMI") + QByteArray(4, '\0') + body;
        const uint32_t n = uint32_t(body.size());
        for (int k = 0; k < 4; k++) raw[4 + k] = char((n >> (8 * k)) & 0xff);
        render::DdmiRecipe rec;
        std::string err;
        QVERIFY2(render::parseDdmi(reinterpret_cast<const uint8_t*>(raw.constData()), size_t(raw.size()), rec, &err),
                 err.c_str());
        std::vector<const render::Icon*> ops;
        for (const render::DdmiRef& r : rec.refs)
            ops.push_back(res_.iconResolved(sess_.world().resources[r.ordinal].first, sess_.world().resources));
        for (const render::Icon* o : ops) QVERIFY(o);
        render::DdmiResult r = render::evalDdmi(rec, ops);
        QVERIFY(!r.icon());
        QVERIFY(!r.err.empty());
        if (!op) return;
        render::setDdmiMutation(18);
        render::DdmiResult m = render::evalDdmi(rec, ops);
        render::setDdmiMutation(0);
        QVERIFY(m.icon());
    }

    // The whole map as this client draws it against stock DreamSeeker's own frame of the same world (map control only,
    // over the map's #ff00ff background). Each case owns a 2x2 block; animated cases are left out.
    void mapEqualsDreamSeeker() {
        std::string png = slurp(dir_ + "/dreamseeker.png");
        render::ParseResult ds = render::parseIcon(reinterpret_cast<const uint8_t*>(png.data()), png.size());
        QVERIFY(ds.ok());
        const render::RgbaImage& d = ds.icon->sheet;
        render::DrawList dl;
        render::SceneStats stats;
        std::string err;
        QVERIFY2(render::assembleMapControl(sess_, res_, 0, dl, stats, &err, false), err.c_str());
        dl.groundBlack = false;
        render::RgbaImage g = render::execute(dl);
        for (size_t k = 0; k < g.px.size(); k += 4) {  // over the map's background
            const int a = g.px[k + 3];
            const uint8_t bg[3] = {255, 0, 255};
            for (int c = 0; c < 3; c++) g.px[k + c] = uint8_t((g.px[k + c] * a + bg[c] * (255 - a) + 127) / 255);
            g.px[k + 3] = 255;
        }
        QCOMPARE(g.w, d.w);
        QCOMPARE(g.h, d.h);
        const std::set<std::string> animated{"d_insanim", "e_insdelay",  "e_insframe3", "f_walkboth",
                                             "f_walkmv",  "f_walkstill", "f_wholemov"};
        int compared = 0;
        QStringList bad;
        for (const Case& c : cases(dir_ + "/cases.tsv")) {
            if (animated.count(c.name)) continue;
            int x0 = (c.x - 1) * 32, y0 = d.h - (c.y + 1) * 32, n = 0, worst = 0;
            for (int y = y0; y < y0 + 64; y++)
                for (int x = x0; x < x0 + 64; x++) {
                    const uint8_t* a = &g.px[(size_t(y) * size_t(g.w) + size_t(x)) * 4];
                    const uint8_t* b = &d.px[(size_t(y) * size_t(d.w) + size_t(x)) * 4];
                    int m = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
                    if (m > 1) n++;
                    worst = std::max(worst, m);
                }
            compared++;
            if (n) bad << QString("%1: %2 px, max %3").arg(QString::fromStdString(c.name)).arg(n).arg(worst);
        }
        QCOMPARE(compared, 111);
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join("; ")));
    }
};

QTEST_GUILESS_MAIN(DdmiOps)
#include "ddmiops_test.moc"
