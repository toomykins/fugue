#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QBuffer>
#include "shell/statview.h"
#include "model/statpanel.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/opacityfixture.h"
using namespace fugue;
using namespace fugue::shell;
namespace {
QByteArray png(QColor color, const QString& metadata = {}) {
    QImage image(32, 32, QImage::Format_ARGB32); image.fill(color);
    if (!metadata.isEmpty()) image.setText("Description", metadata);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
Bytes row(uint32_t head, uint32_t appid, bool clear = false, uint16_t rowWord = 0, uint8_t kind = 2) {
    Bytes bytes{0,0}; putLe16(bytes, clear ? 0 : 1);
    if (!clear) { bytes.push_back(0); bytes.push_back(kind); putLe32(bytes, head); putLe16(bytes, rowWord); putLe32(bytes, appid); putLe16(bytes, 0); }
    putLe16(bytes, clear ? 1 : 0); if (clear) putLe16(bytes, 0); return bytes;
}
void setup(model::World& w) {
    w.stat.tabs = {"Contents"}; w.resources = {{100,0}, {101,0}};
    w.appearances[7].fields.iconResID = 0;
    setFields(w.appearances[7], R"({"name":"row appearance"})");
}
}
class StatViewTest : public QObject {
    Q_OBJECT
private slots:
    void ignoredStaticTransformsAndVisualReuse() {
        model::Session session; auto& w = session.worldMut(); setup(w);
        auto read = [](uint32_t id,QByteArray& b) { b=png(id==100 ? Qt::red : Qt::green); return true; };
        StatThumbnails images; images.begin(w,read,0); const auto original=images.resolve(7,0);
        QVERIFY(!original.isNull());
        const std::array<std::array<float,6>,6> matrices{{
            {1,0,0,0,1,0}, {0,-1,0,1,0,0}, {1,0,9,0,1,5}, {-1,0,0,0,1,0}, {1,.5f,0,0,1,0}, {1.5f,0,0,0,.75f,0}}};
        for (const auto& m:matrices) {
            w.appearances[8]=w.appearances[7]; setFields(w.appearances[8], affinefixture::fields(m));
            images.begin(w,read,0); QCOMPARE(images.resolve(8,0).cacheKey(),original.cacheKey());
        }
        setFields(w.appearances[7], "{\"overlays\":[9]}");
        w.appearances[9].fields.iconResID=1; setFields(w.appearances[9], "{\"pixelX\":16}");
        images.begin(w,read,0); const auto overlay=images.resolve(7,0); QVERIFY(!overlay.isNull());
        setFields(w.appearances[7], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[9]"));
        images.begin(w,read,0); QCOMPARE(images.resolve(7,0).cacheKey(),overlay.cacheKey());
        setFields(w.appearances[9], affinefixture::fields({0,-1,0,1,0,0},",\"pixelX\":16"));
        images.begin(w,read,0); QCOMPARE(images.resolve(7,0).cacheKey(),overlay.cacheKey());
        setFields(w.appearances[9], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[10]"));
        w.appearances[10]=w.appearances[8]; setFields(w.appearances[10], "{}");
        images.begin(w,read,0); QVERIFY(images.resolve(7,0).isNull()); // deeper tree remains gated
        setFields(w.appearances[7], affinefixture::fields({1,0,9,0,1,5},",\"underlays\":[10]"));
        images.begin(w,read,0); QVERIFY(images.resolve(7,0).isNull());
    }
    void wholeIconOpacityMobRow() {
        model::Session session; auto& w=session.worldMut(); setup(w);
        w.appearances[7].fields.iconResID=1; w.appearances[7].fields.iconState="pulse";
        setFields(w.appearances[7], "{\"dir\":8}");
        session.setIngestClockNs(1'000'000'000); session.ingest(0x006a,row(34,7,false,0,3));
        bool ready=false; int reads=0;
        auto read=[&](uint32_t id,QByteArray& out) {
            ++reads; if(id==100 && !ready) return false;
            out=id==100 ? iconfixture::flickAsset() : opacityfixture::recipe(0); return true;
        };
        StatThumbnails images; images.begin(w,read,1'000'000'000);
        QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull());
        ready=true; images.begin(w,read,1'200'000'000); const auto first=buildStatView(w,images);
        QCOMPARE(first.rows[0].thumbnail.pixelColor(16,16),QColor(210,90,20)); // retained frame/direction, ingest start
        const auto key=first.rows[0].thumbnail.cacheKey(); const int warmedReads=reads;
        setFields(w.appearances[7], affinefixture::fields({0,1,0,-1,0,0},",\"dir\":8"));
        images.begin(w,read,1'200'000'000); QCOMPARE(buildStatView(w,images).rows[0].thumbnail.cacheKey(),key);
        QCOMPARE(reads,warmedReads); QVERIFY(w.atoms.empty()); // INFO does not depend on MAP admission
        setFields(w.appearances[7], affinefixture::fields({0,1,0,-1,0,0},",\"underlays\":[8]"));
        w.appearances[8]=w.appearances[7]; setFields(w.appearances[8], "{}");
        images.begin(w,read,1'200'000'000); QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull());
    }
    void authoritativeRowAppearanceAndReuse() {
        model::Session session; auto& w = session.worldMut(); setup(w);
        session.ingest(0x006a, row(99,7));
        // No MAP atom 99 exists. The row-supplied appearance is sufficient.
        QVERIFY(w.atoms.empty());
        int reads = 0; auto read = [&](uint32_t, QByteArray& b) { ++reads; b = png(Qt::red); return true; };
        StatThumbnails images; images.begin(w,read,0); auto first = buildStatView(w,images);
        QCOMPARE(first.rows.size(), 1); QCOMPARE(first.rows[0].text, QString("row appearance"));
        QVERIFY(first.rows[0].clickable); QCOMPARE(first.rows[0].thumbnail.pixelColor(16,16), QColor(Qt::red));
        const auto key = first.rows[0].thumbnail.cacheKey();
        images.begin(w,read,100); auto next = buildStatView(w,images);
        QCOMPARE(next.rows[0].thumbnail.cacheKey(), key); QVERIFY(first == next); QCOMPARE(reads,1);
        w.stat.lines.push_back({1,"Label",model::statValue(R"({"kind":6,"text":"text only"})")});
        images.begin(w,read,200); next = buildStatView(w,images);
        QVERIFY(next.rows[1].thumbnail.isNull()); QVERIFY(!next.rows[1].clickable); QCOMPARE(next.rows[1].text,QString("text only"));
    }
    void delayedAndChangedDependenciesNeverLeaveStalePixels() {
        model::Session session; auto& w = session.worldMut(); setup(w); session.ingest(0x006a,row(99,7));
        bool ready = false; auto read = [&](uint32_t id, QByteArray& b) {
            if (!ready) return false; b = png(id == 100 ? Qt::red : Qt::green); return true;
        };
        StatThumbnails images; images.begin(w,read,0); QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull());
        ready = true; images.begin(w,read,1); auto first = buildStatView(w,images);
        QCOMPARE(first.rows[0].thumbnail.pixelColor(16,16),QColor(Qt::red));
        setFields(w.appearances[7], R"({"name":"changed","overlays":[8]})");
        images.begin(w,read,2); auto missing = buildStatView(w,images);
        QVERIFY(missing.rows[0].thumbnail.isNull()); QCOMPARE(missing.rows[0].text,QString("changed"));
        setFields(w.appearances[8], R"({"pixelX":16})"); w.appearances[8].fields.iconResID = 1;
        ready = false; images.begin(w,read,3); QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull());
        ready = true; images.begin(w,read,4); auto changed = buildStatView(w,images);
        QCOMPARE(changed.rows[0].thumbnail.pixelColor(8,16),QColor(Qt::red));
        QCOMPARE(changed.rows[0].thumbnail.pixelColor(24,16),QColor(Qt::green));
        QCOMPARE(first.rows[0].thumbnail.pixelColor(24,16),QColor(Qt::red)); // immutable old publication
        setFields(w.appearances[8], R"({"filters":[{"type":99,"flags":1}]})");
        images.begin(w,read,5); QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull()); // no arbitrary base fallback
        session.ingest(0x006a,row(99,7,true));
        images.begin(w,read,6); QVERIFY(buildStatView(w,images).rows.empty());
        images.begin(w,read,7); QCOMPARE(images.cached(),size_t(0));
    }
    void resourceRemappingAndInstancePlayback() {
        model::Session session; auto& w = session.worldMut(); setup(w);
        session.setIngestClockNs(1000000000); session.ingest(0x006a,row(99,7));
        QImage sheet(64,32,QImage::Format_ARGB32); sheet.fill(Qt::red);
        for (int y=0;y<32;++y) for (int x=32;x<64;++x) sheet.setPixelColor(x,y,Qt::green);
        sheet.setText("Description","width = 32\nheight = 32\nstate = \"\"\ndirs = 1\nframes = 2\ndelay = 1,1\nloop = 1\n");
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(sheet.save(&buffer,"PNG"));
        bool ready = false; auto read = [&](uint32_t id,QByteArray& b) {
            if (!ready) return false; b = id == 100 ? bytes : png(Qt::blue); return true;
        };
        StatThumbnails images; images.begin(w,read,1000000000); QVERIFY(buildStatView(w,images).rows[0].thumbnail.isNull());
        ready = true; images.begin(w,read,1150000000); auto late = buildStatView(w,images);
        QCOMPARE(late.rows[0].thumbnail.pixelColor(16,16),QColor(Qt::green)); // arrival did not restart playback
        images.begin(w,read,1150000000);
        QCOMPARE(images.resolve(7,1100000000).pixelColor(16,16),QColor(Qt::red)); // shared asset, separate instance start
        w.appearances[8] = w.appearances[7]; setFields(w.appearances[8], R"({"name":"renamed"})");
        session.setIngestClockNs(1200000000); session.ingest(0x006a,row(99,8));
        QCOMPARE(w.stat.lines[0].icon.ns,int64_t(1000000000)); // appearance-only update preserves ingest timing
        w.resources[0].first = 101;
        images.begin(w,read,1300000000); auto remapped = buildStatView(w,images);
        QCOMPARE(remapped.rows[0].thumbnail.pixelColor(16,16),QColor(Qt::blue));
        session.setIngestClockNs(1400000000); session.ingest(0x006a,row(100,8));
        QCOMPARE(w.stat.lines[0].icon.ns,int64_t(1400000000)); // replacement gets its own start
    }
    void publicationAndLifetimeRevalidation() {
        model::Session session; auto& w = session.worldMut(); setup(w); session.ingest(0x006a,row(99,7));
        StatThumbnails images; images.begin(w,{},0); auto published = buildStatView(w,images); published.generation = 10;
        QVERIFY(statRowCurrent(w,published,0,10)); QVERIFY(!statRowCurrent(w,published,0,9));
        session.ingest(0x006a,row(99,7)); QVERIFY(statRowCurrent(w,published,0,10)); // identical update retains lifetime
        session.ingest(0x006a,row(100,7)); // same name, same appearance, different atom
        QVERIFY(!statRowCurrent(w,published,0,10));
        session.ingest(0x006a,row(99,7)); QVERIFY(!statRowCurrent(w,published,0,10)); // replacement then ABA
        published = buildStatView(w,images); published.generation = 11;
        QVERIFY(statRowCurrent(w,published,0,11));
        session.ingest(0x006a,row(99,7,true)); QVERIFY(!statRowCurrent(w,published,0,11));
        session.ingest(0x006a,row(99,7)); QVERIFY(!statRowCurrent(w,published,0,11));
        published = buildStatView(w,images); published.generation = 12;
        session.ingest(0x006a,row(99,7,false,123)); // same visible row, changed opaque wire data
        QVERIFY(!statRowCurrent(w,published,0,12));
        published = buildStatView(w,images); published.generation = 13;
        w.stat.tabs[0] = "Other"; QVERIFY(!statRowCurrent(w,published,0,13));
    }
    void changedAppearanceAndBoundedRetention() {
        model::Session session; auto& w = session.worldMut(); setup(w); session.ingest(0x006a,row(99,7));
        auto read = [&](uint32_t id, QByteArray& b) { b = png(id == 100 ? Qt::red : Qt::green); return true; };
        StatThumbnails images; images.begin(w,read,0); auto first = buildStatView(w,images);
        w.appearances[8] = w.appearances[7]; w.appearances[8].fields.iconResID = 1;
        session.ingest(0x006a,row(99,8)); images.begin(w,read,1); auto changed = buildStatView(w,images);
        QCOMPARE(changed.rows[0].thumbnail.pixelColor(16,16),QColor(Qt::green));
        QVERIFY(!(first == changed));
        for (uint32_t id = 10; id < 530; ++id) {
            w.appearances[id] = w.appearances[7];
            setFields(w.appearances[id], "{\"pixelX\":" + std::to_string(id) + "}"); // distinct visual signatures
            images.begin(w,read,id);
            QVERIFY(!images.resolve(id,0).isNull()); QVERIFY(images.cached() <= 512);
        }
        // Also exercise the capacity bound within one epoch, not just expiry.
        images.begin(w,read,1000);
        for (uint32_t id = 10; id < 530; ++id) { images.resolve(id,0); QVERIFY(images.cached() <= 512); }
        QCOMPARE(images.cached(),size_t(512));
    }
};
FUGUE_TEST_GUILESS_MAIN(StatViewTest)
#include "statview_test.moc"
