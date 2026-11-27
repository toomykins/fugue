#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "shell/entitymap.h"
#include "shell/statview.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/opacityfixture.h"
using namespace fugue;
using namespace fugue::shell;
namespace {
model::World world() {
    model::World w;
    w.bounds = {true,20,20,0,3,3,3,3};
    w.resync.haveOrigin = true; w.resync.originX = w.resync.originY = 4;
    w.resources = {{100,0},{101,12}};
    auto& ap = w.appearances[1]; setFields(ap, "{}"); ap.fields.iconResID = 0; ap.fields.iconState = "pulse";
    ap.fields.layerBits = std::bit_cast<uint32_t>(3.f);
    auto& a = w.atoms[42]; a.hasPos = true; a.x = a.y = 6; a.appid = 1;
    a.haveKey = true; a.key = 77; a.renderIdentity = 1001;
    return w;
}
IconCache::Read reader() {
    return [](uint32_t id,QByteArray& b) { b = id == 101 ? opacityfixture::recipe(0) : iconfixture::flickAsset(); return true; };
}
struct Fixture {
    model::World w = world();
    TurfMap terrain;
    EntityMap scene;
    AtomTrace trace = *AtomTrace::parse("obj:42", "0");
    Fixture() { scene.setDiagnostics(&trace); }
    MapSprites prepare(const IconCache::Read& read = reader(), int64_t ns = 0) {
        return scene.prepare(w,terrain.prepare(w,{},ns,false),read,ns);
    }
};
}
class AtomDiagnosticTest : public QObject {
    Q_OBJECT
private slots:
    void ordinaryAndOpacity_data() {
        QTest::addColumn<bool>("mob"); QTest::addColumn<bool>("recipe");
        QTest::newRow("object") << false << false;
        QTest::newRow("mob") << true << false;
        QTest::newRow("opacity-object") << false << true;
        QTest::newRow("opacity-mob") << true << true;
    }
    void ordinaryAndOpacity() {
        QFETCH(bool,mob); QFETCH(bool,recipe);
        Fixture f;
        if (mob) { f.trace.key |= model::kRegistryA; f.w.atoms[f.trace.key] = f.w.atoms[42]; f.w.atoms.erase(42); }
        if (recipe) f.w.appearances[1].fields.iconResID = 1;
        const auto first = f.prepare(); QCOMPARE(first.size(),1);
        QCOMPARE(f.trace.map.placement,TraceReason::Prepared); QCOMPARE(f.trace.map.parts,size_t(1));
        QCOMPARE(f.trace.map.prepared,1); QCOMPARE(f.trace.map.lifetime,uint64_t(1001));
        QCOMPARE(f.trace.map.wireKey,uint16_t(77)); QVERIFY(f.trace.map.detailsObserved);
        const auto& node = f.trace.map.tree.nodes[0];
        QCOMPARE(node.path,std::vector<uint32_t>{1}); QCOMPARE(node.requested,QString("pulse"));
        QCOMPARE(node.assets[0].result.status,IconCache::Status::Ready);
        QCOMPARE(node.assets.size(),size_t(recipe ? 2 : 1));
        QCOMPARE(f.prepare(),first); // actual cache hits retain authoritative evidence
        QCOMPARE(f.trace.map.placement,TraceReason::Prepared); QVERIFY(f.trace.map.detailsObserved);
        EntityMap disabled; const auto untraced = disabled.prepare(f.w,f.terrain.prepare(f.w,{},0,false),reader());
        QCOMPARE(untraced.size(),first.size());
        for (qsizetype i=0;i<first.size();++i) {
            QCOMPARE(untraced[i].image,first[i].image);
            auto copy = untraced[i]; copy.image = first[i].image; QCOMPARE(copy,first[i]);
        }
    }
    void lateResourceAndRemap() {
        Fixture f; bool available = false;
        auto read = [&](uint32_t id,QByteArray& b) {
            if (id==100 && !available) return false;
            b = id==101 ? opacityfixture::recipe(0) : iconfixture::flickAsset(); return true;
        };
        f.w.appearances[1].fields.iconResID = 1;
        QVERIFY(f.prepare(read).empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::AssetUnavailable);
        QCOMPARE(f.trace.map.tree.nodes[0].assets[1].result.status,IconCache::Status::Missing);
        available = true; QCOMPARE(f.prepare(read).size(),1);
        QCOMPARE(f.trace.map.first.reason,TraceReason::Unobserved);
        const auto rev = f.trace.map.revision;
        f.w.resources[0].first = 102; QCOMPARE(f.prepare(read).size(),1);
        QVERIFY(f.trace.map.revision != rev);
        QCOMPARE(f.trace.map.tree.nodes[0].assets[1].result.id,uint32_t(102));
    }
    void unsupportedVersusMissingMalformedAndState() {
        Fixture f;
        auto bad = [](uint32_t,QByteArray& b) { b = iconfixture::recipe(QByteArray::fromHex("fe")); return true; };
        QVERIFY(f.prepare(bad).empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::EmptyAsset);
        auto asset = f.trace.map.tree.nodes[0].assets[0];
        QCOMPARE(asset.result.status,IconCache::Status::UnsupportedRecipe);
        QCOMPARE(asset.failure.offset,8); QCOMPARE(asset.failure.operation,254);
        // Ready remains true for stable empty icons, exactly as before diagnostics.
        QVERIFY(asset.result.ready);
        f.w.resources[0].first = 102;
        QVERIFY(f.prepare([](uint32_t,QByteArray&){return false;}).empty());
        QCOMPARE(f.trace.map.first.reason,TraceReason::AssetUnavailable);
        QCOMPARE(f.trace.map.tree.nodes[0].assets[0].result.status,IconCache::Status::Missing);
        QVERIFY(f.prepare([](uint32_t,QByteArray& b){b="not an image";return true;}).empty());
        QCOMPARE(f.trace.map.tree.nodes[0].assets[0].result.status,IconCache::Status::MalformedSource);
        f.w.resources[0].first = 103; f.w.appearances[1].fields.iconState = "absent-state";
        const auto sprites = f.prepare();
        // Fixture has a default state; selection reports its real fallback, not missing bytes.
        QCOMPARE(sprites.size(),1); QVERIFY(f.trace.map.tree.nodes[0].fallback);
        QCOMPARE(f.trace.map.tree.nodes[0].selected,QString());
        f.w.resources[0].first = 104;
        QVERIFY(f.prepare([](uint32_t,QByteArray& b){b=iconfixture::sheet();return true;}).empty());
        QCOMPARE(f.trace.map.first.reason,TraceReason::MissingState);
    }
    void geometryAndGates() {
        Fixture f; f.trace.key |= model::kRegistryA;
        f.w.atoms[f.trace.key] = f.w.atoms[42]; f.w.atoms.erase(42);
        setFields(f.w.appearances[1], affinefixture::fields({0,1,0,-1,0,0}));
        QCOMPARE(f.prepare().size(),1); QVERIFY(f.trace.map.affine); QVERIFY(!f.trace.map.bounds.isEmpty());
        f.w.appearances[2] = f.w.appearances[1]; setFields(f.w.appearances[2], "{}");
        setFields(f.w.appearances[1], affinefixture::fields({0,1,0,-1,0,0},",\"overlays\":[2]"));
        QCOMPARE(f.prepare().size(),2); QCOMPARE(f.trace.map.templateStatus,TraceReason::TemplateComplete);
        setFields(f.w.appearances[1], affinefixture::fields({0,0,0,0,1,0},",\"overlays\":[2]")); // singular: still refused
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::TransformTree);
        QVERIFY(!f.trace.map.complete); QCOMPARE(f.trace.map.first.appearance,uint32_t(1));
        QCOMPARE(f.trace.map.templateStatus,TraceReason::TemplateRefused);
        setFields(f.w.appearances[1], "{\"overlays\":[2]}");
        setFields(f.w.appearances[2], affinefixture::fields({0,0,0,0,1,0})); // invalid child matrix
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.first.appearance,uint32_t(2));
        QCOMPARE(f.trace.map.first.parent,uint32_t(1));
        QCOMPARE(f.trace.map.tree.nodes[1].path,(std::vector<uint32_t>{1,2}));
        setFields(f.w.appearances[1], affinefixture::fields({0,0,0,0,1,0}));
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::InvalidTransform);
        setFields(f.w.appearances[1], R"({"vars":[{"bit":4,"data":"65"}]})");
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::Invisible);
        setFields(f.w.appearances[1], "{\"hasColor\":true,\"colorA\":0}");
        QCOMPARE(f.prepare().size(),1); QCOMPARE(f.trace.map.first.reason,TraceReason::ZeroAlpha);
        QCOMPARE(f.trace.map.placement,TraceReason::Prepared); // invisible but still carries source-alpha input
        setFields(f.w.appearances[1], "{}"); f.w.atoms[f.trace.key].x = 1;
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.placement,TraceReason::OffView);
        setFields(f.w.appearances[1], affinefixture::fields({1,0,160,0,1,0}));
        QCOMPARE(f.prepare().size(),1); QCOMPARE(f.trace.map.placement,TraceReason::Prepared);
        f.w.atoms[f.trace.key].hasPos = false;
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.placement,TraceReason::Placement);
    }
    void identityAndReconnect() {
        Fixture f; const auto mob = model::kRegistryA|42;
        f.w.atoms[mob] = f.w.atoms[42]; f.w.atoms[mob].renderIdentity = 222;
        f.w.atoms[mob].appid = 2; f.w.appearances[2] = f.w.appearances[1];
        setFields(f.w.appearances[2], R"({"vars":[{"bit":4,"data":"65"}]})");
        QCOMPARE(f.prepare().size(),1); QCOMPARE(f.trace.map.lifetime,uint64_t(1001));
        f.trace.key = mob; f.prepare(); QCOMPARE(f.trace.map.lifetime,uint64_t(222));
        QCOMPARE(f.trace.map.first.reason,TraceReason::Invisible);
        f.w.atoms[mob].renderIdentity = 223; f.w.atoms[mob].appid = 1;
        QCOMPARE(f.prepare().size(),2); QCOMPARE(f.trace.map.lifetime,uint64_t(223));
        QCOMPARE(f.trace.map.first.reason,TraceReason::Unobserved);
        f.w.resync.freed.insert(mob); f.prepare(); QCOMPARE(f.trace.map.placement,TraceReason::Freed);
        f.w.atoms.erase(mob); f.prepare(); QCOMPARE(f.trace.map.placement,TraceReason::AbsentModel);
        QCOMPARE(f.trace.map.lifetime,uint64_t(0)); QVERIFY(f.trace.map.tree.nodes.empty());
        AtomTraceOutput output;
        QVERIFY(!output.sample(f.trace,1,1,0,0,0).isEmpty());
        QVERIFY(output.sample(f.trace,1,2,0,0,1000).isEmpty());
        QVERIFY(!output.sample(f.trace,2,1,0,0,2000).isEmpty()); // session key, not old local lifetime
    }
    void infoIndependent() {
        Fixture f; StatThumbnails thumbnails;
        f.w.stat.tabs = {"Contents"};
        f.w.stat.lines.push_back({0,"not logged",model::statValue(R"({"kind":2,"head":42,"appid":2,"hasAppid":true})")});
        f.w.stat.lines[0].identity = 333;
        f.w.appearances[2] = f.w.appearances[1];
        setFields(f.w.appearances[1], R"({"vars":[{"bit":4,"data":"65"}]})");
        f.prepare(); QCOMPARE(f.trace.map.first.reason,TraceReason::Invisible);
        auto info = [&] { thumbnails.begin(f.w,reader(),0); return buildStatView(f.w,thumbnails,&f.trace); };
        QVERIFY(!info().rows[0].thumbnail.isNull()); QCOMPARE(f.trace.info.outcome,TraceReason::Composed);
        QCOMPARE(f.trace.info.appearance,uint32_t(2)); QCOMPARE(f.trace.info.lifetime,uint64_t(333));
        QCOMPARE(f.trace.info.atomKey,uint32_t(42));
        f.w.atoms.clear(); f.prepare(); QVERIFY(!info().rows[0].thumbnail.isNull());
        QCOMPARE(f.trace.map.placement,TraceReason::AbsentModel);
        f.w.atoms[42] = world().atoms.at(42); f.w.atoms[42].appid = 2;
        f.w.appearances[3] = f.w.appearances[2];
        setFields(f.w.appearances[2], affinefixture::fields({0,0,0,0,1,0},",\"overlays\":[3]")); // singular: still refused
        QVERIFY(f.prepare().empty()); QCOMPARE(f.trace.map.first.reason,TraceReason::TransformTree);
        QVERIFY(!info().rows[0].thumbnail.isNull()); QCOMPARE(f.trace.info.outcome,TraceReason::Composed);
        QVERIFY(f.trace.info.tree.nodes[0].transformIgnored);
        setFields(f.w.appearances[2], "{\"filters\":[{\"type\":99,\"flags\":1}]}");
        QVERIFY(info().rows[0].thumbnail.isNull()); QCOMPARE(f.trace.info.outcome,TraceReason::InfoUnsupported);
        setFields(f.w.appearances[2], "{}"); f.w.appearances[2].fields.iconResID = 1;
        thumbnails.begin(f.w,[](uint32_t,QByteArray& b){b=QByteArray(4*1024*1024+1,'x');return true;},0);
        QVERIFY(buildStatView(f.w,thumbnails,&f.trace).rows[0].thumbnail.isNull());
        QCOMPARE(f.trace.info.tree.nodes[0].assets[0].result.status,IconCache::Status::WorkLimit);
        f.w.stat.lines.clear(); info(); QCOMPARE(f.trace.info.outcome,TraceReason::NoMatchingRow);
        QCOMPARE(f.trace.info.lifetime,uint64_t(0));
    }
    void cacheWorkEvidenceDoesNotChangeReadiness() {
        IconCache cache; IconCache::Resources resources{{100,0}};
        cache.begin(resources,reader()); const auto ready = cache.lookup(0); QVERIFY(ready.ready);
        for (unsigned i=1;i<65536;++i) cache.lookup(0);
        const auto deferred = cache.lookup(0);
        QVERIFY(!deferred.ready); QCOMPARE(deferred.status,IconCache::Status::WorkLimit);
        const auto evidence = cache.observations(0,&deferred);
        QCOMPARE(evidence[0].result.status,IconCache::Status::WorkLimit); QVERIFY(!evidence[0].result.ready);
        cache.begin(resources,reader()); QCOMPARE(cache.lookup(0),ready);
        auto statusOnly = ready; statusOnly.status = IconCache::Status::WorkLimit;
        QVERIFY(statusOnly == ready); // never a new rendering/cache signature
    }
    void boundsAndDisabledWork() {
        Fixture f;
        for (uint32_t id=1;id<=40;++id) {
            f.w.appearances[id] = f.w.appearances[1];
            setFields(f.w.appearances[id], "{\"overlays\":["+std::to_string(id+1)+"]}");
        }
        f.prepare(); QVERIFY(f.trace.map.tree.truncated); QCOMPARE(f.trace.map.tree.nodes.size(),size_t(16));
        QCOMPARE(f.trace.map.first.reason,TraceReason::TraversalLimit);
        // Candidate exhaustion: deterministic whether selected sorts first or last by choosing it after the fact.
        f.w = world(); f.w.atoms.clear();
        for (uint32_t id=1;id<=65537;++id) { auto a=world().atoms.at(42); f.w.atoms[id]=a; }
        auto last = f.w.atoms.begin(); std::advance(last,65536); f.trace.key = last->first;
        f.prepare(); QCOMPARE(f.trace.map.placement,TraceReason::SceneWork);
        int readsOn=0,readsOff=0;
        auto on=[&](uint32_t id,QByteArray& b){++readsOn;return reader()(id,b);};
        auto off=[&](uint32_t id,QByteArray& b){++readsOff;return reader()(id,b);};
        Fixture traced; EntityMap disabled;
        traced.prepare(on); disabled.prepare(traced.w,traced.terrain.prepare(traced.w,{},0,false),off);
        QCOMPARE(readsOn,readsOff);
    }
    void outputLimitsAndPixels() {
        QVERIFY(!AtomTrace::parse("42")); QVERIFY(!AtomTrace::parse("mob:2147483648"));
        QVERIFY(!AtomTrace::parse("obj:-1")); QVERIFY(!AtomTrace::parse("obj:1","65536"));
        Fixture f; f.w.appearances[1].fields.iconResID = 1;
        auto zero = [](uint32_t id,QByteArray& b) { b=id==101 ? opacityfixture::recipe(0,0) : iconfixture::flickAsset();return true;};
        const auto sprites=f.prepare(zero); QCOMPARE(sprites.size(),1); f.trace.inspectPixels = true;
        AtomTraceOutput output;
        auto bytes=output.sample(f.trace,1,1,1,0,0,&sprites);
        QVERIFY(bytes.size()<=16385); auto json=QJsonDocument::fromJson(bytes).object();
        QCOMPARE(json["rendered"].toString(),QString("unobserved"));
        QCOMPARE(json["publishedSourceAlpha"].toString(),QString("transparent"));
        QVERIFY(output.sample(f.trace,1,2,1,0,1000,&sprites).isEmpty());
        f.trace.map.x++;
        QVERIFY(output.sample(f.trace,1,2,1,0,1500,&sprites).isEmpty());
        QVERIFY(!output.sample(f.trace,1,2,1,0,2000,&sprites).isEmpty());
        for (int i=3;i<300;++i) { f.trace.map.x=i; bytes=output.sample(f.trace,1,i,1,0,i*1000,&sprites); QVERIFY(bytes.size()<=16385); }
        QCOMPARE(output.emitted(),256u);
        QVERIFY(output.sample(f.trace,1,999,1,0,999000,&sprites).isEmpty());
        AtomTraceOutput bounded;
        auto node = f.trace.map.tree.nodes[0]; node.assets.resize(16,node.assets[0]);
        node.requested = QString(64,QChar('\\n')); node.selected = node.requested;
        node.path.resize(32,UINT32_MAX); f.trace.map.tree.nodes.assign(16,node);
        f.trace.info.tree = f.trace.map.tree;
        bytes = bounded.sample(f.trace,1,1,1,0,0);
        QVERIFY(bytes.size()<=16385); QVERIFY(QJsonDocument::fromJson(bytes).object()["outputTruncated"].toBool());
        auto large = sprites;
        large[0].image = QImage(257,257,QImage::Format_RGBA8888); large[0].image.fill(Qt::transparent);
        AtomTraceOutput pixelBudget;
        bytes = pixelBudget.sample(f.trace,1,1,1,0,0,&large);
        QCOMPARE(QJsonDocument::fromJson(bytes).object()["publishedSourceAlpha"].toString(),QString("budget"));
        Fixture retry;
        retry.prepare([](uint32_t,QByteArray&){return false;});
        AtomTraceOutput retries;
        QVERIFY(!retries.sample(retry.trace,1,1,0,0,0).isEmpty());
        retry.trace.map.revision++;
        retry.trace.map.tree.nodes[0].assets[0].result.revision++;
        QVERIFY(retries.sample(retry.trace,1,2,0,0,1000).isEmpty());
    }
};
FUGUE_TEST_MAIN(AtomDiagnosticTest)
#include "atomdiagnostic_test.moc"
