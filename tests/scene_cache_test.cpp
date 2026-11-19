// Exact differential coverage for parsed-appearance reuse: draw lists AND mouse metadata.
#include <QtTest>
#include <functional>
#include <filesystem>
#include <fstream>
#include <tuple>
#include "render/scene.h"

using namespace fugue;
namespace {
constexpr int64_t kClock = 1'000'000'000'000'000'000;
struct World {
    model::Session session;
    render::ResStore resources;
};

bool resources(World& w, const std::string& fixture) {
    const auto root = std::filesystem::path(FUGUE_TEST_DATA_DIR) / fixture / "res";
    if (!std::filesystem::exists(root)) return true;
    for (const auto& file : std::filesystem::directory_iterator(root)) {
        const auto name = file.path().filename().string();
        if (name.size() != 8 || name.find_first_not_of("0123456789abcdef") != std::string::npos) continue;
        std::ifstream f(file.path(), std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(f)), {});
        Bytes bytes(data.begin(), data.end());
        const auto id = uint32_t(std::stoul(name, nullptr, 16));
        std::string error;
        if (!w.resources.add(bytes, &id, &error)) return false;
        w.session.noteResource(id, bytes, "");
    }
    return true;
}
bool load(World& w, const std::string& fixture, const std::string& frames = "frames.txt", bool withResources = true) {
    w.session.setIngestClockNs(kClock);
    std::ifstream f(std::string(FUGUE_TEST_DATA_DIR) + '/' + fixture + '/' + frames);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("0x", 0) != 0) continue;
        const auto type = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
        const auto at = line.find(' ', line.find("len="));
        auto bytes = hexDecode(at == std::string::npos ? "" : line.substr(at + 1));
        if (!bytes) return false;
        w.session.ingest(type, *bytes);
    }
    return !withResources || resources(w, fixture);
}
}  // namespace

class SceneCacheTest : public QObject {
    Q_OBJECT
    render::DrawList last_;
    render::SceneStats stats_;

    void sameLayer(const render::HitLayer& a, const render::HitLayer& b) {
        QVERIFY(std::tie(a.x, a.y, a.atom, a.own, a.plane, a.maptext) ==
                std::tie(b.x, b.y, b.atom, b.own, b.plane, b.maptext));
        const auto &f = a.frame, &g = b.frame;
        QVERIFY(std::tie(f.x, f.y, f.w, f.h, f.hasXf, f.m.a, f.m.b, f.m.c, f.m.d, f.m.e, f.m.f) ==
                std::tie(g.x, g.y, g.w, g.h, g.hasXf, g.m.a, g.m.b, g.m.c, g.m.d, g.m.e, g.m.f));
        QCOMPARE(bool(a.sprite), bool(b.sprite));
        if (a.sprite) {
            QCOMPARE(a.sprite->w, b.sprite->w);
            QCOMPARE(a.sprite->h, b.sprite->h);
            QCOMPARE(a.sprite->px, b.sprite->px);
        }
    }
    void sameHits(const std::shared_ptr<const render::HitScene>& a, const std::shared_ptr<const render::HitScene>& b) {
        QCOMPARE(bool(a), bool(b));
        if (!a) return;
        QVERIFY(std::tie(a->tileW, a->tileH, a->cols, a->rows, a->originX, a->originY) ==
                std::tie(b->tileW, b->tileH, b->cols, b->rows, b->originX, b->originY));
        QCOMPARE(a->atoms.size(), b->atoms.size());
        for (size_t i = 0; i < a->atoms.size(); ++i) {
            const auto &x = a->atoms[i], &y = b->atoms[i];
            QVERIFY(std::tie(x.name, x.mouseOpacity, x.kind, x.ref, x.worldX, x.worldY, x.screen, x.appid, x.dir) ==
                    std::tie(y.name, y.mouseOpacity, y.kind, y.ref, y.worldX, y.worldY, y.screen, y.appid, y.dir));
        }
        QCOMPARE(a->world.size(), b->world.size());
        for (size_t i = 0; i < a->world.size(); ++i) sameLayer(a->world[i], b->world[i]);
        QCOMPARE(a->hud.size(), b->hud.size());
        for (size_t i = 0; i < a->hud.size(); ++i) {
            const auto &x = a->hud[i], &y = b->hud[i];
            QVERIFY(std::tie(x.atom, x.plane, x.layer, x.tileW, x.tileH, x.originX, x.originY) ==
                    std::tie(y.atom, y.plane, y.layer, y.tileW, y.tileH, y.originX, y.originY));
            QCOMPARE(x.layers.size(), y.layers.size());
            for (size_t j = 0; j < x.layers.size(); ++j) sameLayer(x.layers[j], y.layers[j]);
            QCOMPARE(x.dests.size(), y.dests.size());
            for (size_t j = 0; j < x.dests.size(); ++j) {
                QCOMPARE(x.dests[j].x, y.dests[j].x);
                QCOMPARE(x.dests[j].y, y.dests[j].y);
            }
        }
    }
    void compare(World& w, render::SceneCache& cache, int64_t clock = kClock, bool hit = true,
                 const std::string& map = {}, std::string_view style = {}) {
        render::DrawList reference, actual;
        render::SceneStats before, after;
        std::string e1, e2;
        const bool ok1 =
            map.empty()
                ? render::assembleMapControl(w.session, w.resources, clock, reference, before, &e1, hit, style)
                : render::assembleSecondaryMap(w.session, w.resources, clock, map, reference, before, &e1, style);
        const bool ok2 =
            map.empty()
                ? render::assembleMapControl(w.session, w.resources, clock, actual, after, &e2, hit, style, &cache)
                : render::assembleSecondaryMap(w.session, w.resources, clock, map, actual, after, &e2, style, &cache);
        QCOMPARE(ok2, ok1);
        QCOMPARE(e2, e1);
        QVERIFY(render::sameDrawList(reference, actual));
        QCOMPARE(before.summary(), after.summary());
        sameHits(reference.hit, actual.hit);
        last_ = std::move(actual);
        stats_ = after;
    }
private slots:
    void replays_data() {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("frames");
        for (const char* name : {"hudhits-world", "hudoverlay-world"})
            for (const char* frame : {"frames-pm.txt", "frames-nopm.txt"})
                QTest::newRow(qPrintable(QString(name) + '/' + frame)) << QString(name) << QString(frame);
        for (const char* name : {"holomap-world", "progbar-world", "runechat-world", "ddmiops-world"})
            QTest::newRow(name) << QString(name) << QString("frames.txt");
    }
    void replays() {
        QFETCH(QString, fixture);
        QFETCH(QString, frames);
        World w;
        QVERIFY(load(w, fixture.toStdString(), frames.toStdString()));
        render::SceneCache cache;
        compare(w, cache);
        QVERIFY(stats_.appearanceParses > 0);
        for (bool hit : {false, true})
            for (int frame = 0; frame < 3; ++frame) {
                compare(w, cache, kClock + frame * 100'000'000, hit);
                QCOMPARE(stats_.appearanceParses, uint64_t(0));
                QVERIFY(stats_.appearanceCacheHits > 0);
            }
        const auto pixels = render::execute(last_).px;
        cache.clear();
        QCOMPARE(cache.retainedBytes(), size_t(0));
        QCOMPARE(cache.entries(), size_t(0));
        compare(w, cache, kClock + 200'000'000);
        QCOMPARE(render::execute(last_).px, pixels);
        QVERIFY(stats_.appearanceParses > 0);
    }
    void changedMissingAndMalformedFields() {
        World w;
        QVERIFY(load(w, "hudhits-world", "frames-pm.txt"));
        render::SceneCache cache;
        compare(w, cache);
        QVERIFY(last_.hit && !last_.hit->atoms.empty());
        const auto id = last_.hit->atoms.front().appid;
        auto& appearances = w.session.worldMut().appearances;
        const auto original = appearances.at(id);
        using F = proto::Appearance;
        const std::pair<const char*, std::function<void(F&)>> changes[] = {
            {"name", [](F& f) { f.name = "new cached name"; }},
            {"appearanceFlags", [](F& f) { f.appearanceFlags = 8192; }},
            {"pixelX", [](F& f) { f.pixelX = 7; }},
            {"pixelY", [](F& f) { f.pixelY = -3; }},
            {"pixelW", [](F& f) { f.pixelW = 2; }},
            {"pixelZ", [](F& f) { f.pixelZ = -2; }},
            {"hasColor", [](F& f) { f.hasColor = true; }},
            {"colorR", [](F& f) { f.color[0] = 77; }},
            {"colorG", [](F& f) { f.color[1] = 78; }},
            {"colorB", [](F& f) { f.color[2] = 79; }},
            {"colorA", [](F& f) { f.color[3] = 80; }},
            {"hasPlane", [](F& f) { f.hasPlane = true; }},
            {"plane", [](F& f) { f.plane = 5; }},
            {"dir", [](F& f) { f.dir = 4; }},
            {"layer", [](F& f) { f.layerBits = 0x3f800000; }},
            {"hasTransform", [](F& f) { f.hasTransform = true; }},
            {"transform", [](F& f) { f.transformBits = {0x40000000, 0, 0, 0, 0x3f800000, 0}; }},
            {"renderSource", [](F& f) { f.renderSource = "missing"; }},
            {"renderTarget", [](F& f) { f.renderTarget = "target"; }},
            {"screenLoc", [](F& f) { f.screenLoc = "Camera:1,1"; }},
            {"maptext", [](F& f) { f.maptext = "hello"; }},
            {"maptextW", [](F& f) { f.maptextW = 16; }},
            {"maptextH", [](F& f) { f.maptextH = 16; }},
            {"maptextX", [](F& f) { f.maptextX = 1; }},
            {"maptextY", [](F& f) { f.maptextY = 2; }},
            {"vars", [](F& f) { f.vars = {{4, {0xff}}, {256, {0x03}}}; }},
            {"invisibility",
             [](F& f) {
                 f.hasInvisibility = true;
                 f.invisibility = 0xff;
             }},
            {"blend",
             [](F& f) {
                 f.hasBlend = true;
                 f.blend = 3;
             }},
            {"colorMatrix",
             [](F& f) {
                 f.hasColorMatrix = true;
                 f.colorMatrixBits[0] = 0x3f800000;
             }},
            {"filters", [](F& f) {
                 F::Filter flt;
                 flt.type = 1;
                 flt.paramMask = 1;
                 flt.params = {0x00, 0x00, 0x80, 0x3f};
                 f.filters = {flt};
             }}};
        for (const auto& [key, change] : changes) {
            appearances[id] = original;
            compare(w, cache);
            change(appearances[id].fields);
            compare(w, cache);
            QVERIFY2(stats_.appearanceParses > 0, key);
            compare(w, cache);
            QCOMPARE(stats_.appearanceParses, uint64_t(0));
        }
        appearances[id].fields = proto::Appearance{};
        compare(w, cache);
        QVERIFY(stats_.appearanceParses > 0);
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(0));
        appearances.erase(id);
        compare(w, cache);
        appearances[id] = original;
        compare(w, cache);
        QVERIFY(stats_.appearanceParses > 0);
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(0));
    }
    void matricesChildrenAndOldSnapshots() {
        World w;
        QVERIFY(load(w, "hudhits-world", "frames-pm.txt"));
        render::SceneCache cache;
        compare(w, cache);
        QVERIFY(last_.hit && !last_.hit->atoms.empty());
        const auto parent = last_.hit->atoms.front().appid;
        auto& appearances = w.session.worldMut().appearances;
        Bytes matrix(80, 0);
        for (size_t i : {0, 5, 10, 15}) {
            matrix[i * 4 + 2] = 0x80;
            matrix[i * 4 + 3] = 0x3f;
        }
        auto setMatrix = [&] {
            proto::Appearance& f = appearances[parent].fields;
            f.hasColorMatrix = true;
            for (size_t k = 0; k < 20; k++) f.colorMatrixBits[k] = le32(&matrix[k * 4]);
        };
        setMatrix();
        compare(w, cache);
        compare(w, cache);
        const auto old = last_;
        const auto oldPixels = render::execute(old).px;
        matrix[16 * 4 + 3] = 0x3f;  // +0.5 red; the old DrawList must still hold the old immutable matrix
        setMatrix();
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(1));
        QCOMPARE(render::execute(old).px, oldPixels);
        QVERIFY(render::execute(last_).px != oldPixels);
        constexpr uint32_t child = 200000;
        QVERIFY(!appearances.count(child));
        appearances[child] = appearances[parent];
        appearances[parent].fields.overlays = {200000};
        compare(w, cache);
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(0));
        appearances[child].fields.pixelX = 1;  // unchanged parent must still consult changed child fields
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(1));
        appearances[child].fields.overlays = {parent};  // cycle guard remains per build
        compare(w, cache);
        render::SceneCache tiny(8 * 1024 * 1024, 1);
        compare(w, tiny);
        compare(w, tiny);
        QCOMPARE(tiny.entries(), size_t(1));
        appearances[parent].fields.overlays = {};
        appearances[parent].fields.underlays = {200000};
        compare(w, cache);
        appearances.erase(child);
        compare(w, cache);
    }

    void boundsAndActiveEviction() {
        World w;
        QVERIFY(load(w, "hudhits-world", "frames-pm.txt"));
        // A one-entry cache evicts parents while overlays are being built. The local pin must keep them valid.
        for (size_t count : {0, 1, 2, 8}) {
            render::SceneCache cache(8 * 1024 * 1024, count);
            for (int frame = 0; frame < 3; ++frame) {
                compare(w, cache);
                QVERIFY(cache.entries() <= count);
                QVERIFY(cache.retainedBytes() <= 8 * 1024 * 1024);
            }
        }
        for (size_t bytes : {0, 1, 1024, 4096}) {
            render::SceneCache cache(bytes);
            for (int frame = 0; frame < 3; ++frame) {
                compare(w, cache);
                QVERIFY(cache.retainedBytes() <= bytes);
            }
        }
    }
    void resourcesAndSecondaryMaps() {
        World w;
        QVERIFY(load(w, "hudhits-world", "frames-pm.txt", false));
        render::SceneCache cache;
        compare(w, cache);
        QVERIFY(!w.resources.takeWanted().empty());
        compare(w, cache);
        QVERIFY(!w.resources.takeWanted().empty());  // hits must still request missing resources
        QVERIFY(resources(w, "hudhits-world"));      // appearance cache must not remember unresolved icons
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(0));
        const auto pixels = render::execute(last_).px;
        auto& ordinals = w.session.worldMut().resources;
        QVERIFY(ordinals.size() >= 2);
        std::swap(ordinals[0], ordinals[1]);
        compare(w, cache);
        QCOMPARE(stats_.appearanceParses, uint64_t(0));  // resolve ordinals anew, not into cached icon pointers
        std::swap(ordinals[0], ordinals[1]);
        compare(w, cache);
        QCOMPARE(render::execute(last_).px, pixels);
        for (auto& [id, a] : w.session.worldMut().appearances)
            if (!a.fields.screenLoc.empty()) a.fields.screenLoc = "Camera:" + a.fields.screenLoc;
        compare(w, cache);
        compare(w, cache, kClock, false, "camera");
        QVERIFY(last_.outW > 0);
        QVERIFY(stats_.appearanceCacheHits > 0);
        compare(w, cache, kClock + 100'000'000, false, "Camera");
        QCOMPARE(stats_.appearanceParses, uint64_t(0));
        World other;
        QVERIFY(load(other, "hudoverlay-world", "frames-nopm.txt"));
        compare(other, cache);  // same IDs in a different model; content, not ID/address alone, is authoritative
        World again;
        QVERIFY(load(again, "hudhits-world", "frames-pm.txt"));
        compare(again, cache);
        QCOMPARE(render::execute(last_).px, pixels);
    }
    void benchmark() {
        if (!qEnvironmentVariableIntValue("FUGUE_SCENE_BENCHMARK"))
            QSKIP("Set FUGUE_SCENE_BENCHMARK=1 for assembly A/B timings");
        World w;
        QVERIFY(load(w, "hudhits-world", "frames-pm.txt"));
        render::SceneCache cache;
        compare(w, cache);
        QVERIFY(last_.hit && !last_.hit->atoms.empty());
        auto& world = w.session.worldMut();
        const auto base = world.appearances.at(last_.hit->atoms.front().appid);
        QVERIFY(!world.hudOrder.empty());
        const auto parent = world.hud.at(world.hudOrder.front()).appid;
        std::vector<uint32_t> overlays;
        for (uint32_t i = 0; i < 1024; ++i) {
            const uint32_t id = 100000 + i;
            QVERIFY(!world.appearances.count(id));
            auto a = base;
            a.fields.name = "bench-" + std::to_string(i);
            world.appearances.emplace(id, std::move(a));
            overlays.push_back(id);
        }
        world.appearances.at(parent).fields.overlays = overlays;
        compare(w, cache);
        const auto reference = last_;
        for (bool enabled : {false, true, true, false}) {
            double ms = 0;
            uint64_t parses = 0, hits = 0;
            constexpr int warmups = 8, count = 60;
            for (int i = 0; i < warmups + count; ++i) {
                render::DrawList dl;
                render::SceneStats stats;
                std::string error;
                QElapsedTimer timer;
                timer.start();
                QVERIFY2(render::assembleMapControl(w.session, w.resources, kClock, dl, stats, &error, true, {},
                                                    enabled ? &cache : nullptr),
                         error.c_str());
                const double elapsed = timer.nsecsElapsed() / 1e6;
                QVERIFY(render::sameDrawList(reference, dl));
                sameHits(reference.hit, dl.hit);
                if (i >= warmups) {
                    ms += elapsed;
                    parses += stats.appearanceParses;
                    hits += stats.appearanceCacheHits;
                }
            }
            qInfo().nospace() << (enabled ? "cached" : "uncached") << " prepare=" << ms / count
                              << "ms parses=" << double(parses) / count << " hits=" << double(hits) / count
                              << " retained-bytes=" << (enabled ? cache.retainedBytes() : 0);
        }
    }
};
QTEST_GUILESS_MAIN(SceneCacheTest)
#include "scene_cache_test.moc"
