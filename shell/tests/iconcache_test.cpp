#include <QtTest>
#include "tests/testmain.h"
#include <limits>
#include "shell/iconcache.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/opacityfixture.h"
using namespace fugue::shell;
using namespace iconfixture;
// A recipe gets a 2 ms slice per preparation and is finished by later ones, so a test that wants a generated icon
// prepares until it is ready.
static IconCache::Result finish(IconCache& cache,const IconCache::Resources& resources,const IconCache::Read& read,uint32_t index) {
    IconCache::Result done; int preparations=0;
    do { cache.begin(resources,read); done=cache.lookup(index); } while(!done.ready && ++preparations<1000);
    return done;
}
class IconCacheTest : public QObject {
    Q_OBJECT
private slots:
    void dmiPlayback() {
        const auto icon = DmiIcon::decode(animation());
        const auto* s = icon.state("missing-name"); QVERIFY(s); QCOMPARE(s->frames,3);
        QCOMPARE(s->frameAt(1,0),0); QCOMPARE(s->frameAt(49999999,0),0);
        QCOMPARE(s->frameAt(50000000,0),1); QCOMPARE(s->frameAt(199999999,0),1);
        QCOMPARE(s->frameAt(200000000,0),2); QCOMPARE(s->frameAt(400000000,0),0);
        QCOMPARE(s->frameAt(INT64_MAX,INT64_MIN),s->frameAt(INT64_MAX % 400000000,0));
        QCOMPARE(s->frameAt(-1,0),0);
        for (int d = 0; d < 4; ++d) for (int f = 0; f < 3; ++f)
            QCOMPARE(icon.frame("missing",std::array{2,1,4,8}[d],f).pixelColor(16,0),QColor(50+80*f,30+20*d,20));
        const auto eight = DmiIcon::decode(animation(0,false,8));
        for (int d=0;d<8;++d) QCOMPARE(eight.frame("",std::array{2,1,4,8,6,10,5,9}[d],2).pixelColor(16,0),QColor(210,30+20*d,20));
        const auto finite = DmiIcon::decode(animation(2)); const auto& f = *finite.state("");
        QCOMPARE(f.frameAt(1000000000,1000000000),0);
        QCOMPARE(f.frameAt(1050000000,1000000000),1);
        QCOMPARE(f.frameAt(1400000000,1000000000),0);
        QCOMPARE(f.frameAt(1800000000,1000000000),2);
        QCOMPARE(f.frameAt(INT64_MAX,1),2); QCOMPARE(f.frameAt(1,INT64_MAX),0);
        const auto reverse = DmiIcon::decode(animation(1,true)); const auto& r = *reverse.state("");
        QCOMPARE(r.frameAt(1200000000,1000000000),2);
        QCOMPARE(r.frameAt(1400000000,1000000000),1);
        QCOMPARE(r.frameAt(1549999999,1000000000),1);
        QCOMPARE(r.frameAt(1550000000,1000000000),0);
        const auto repeat = DmiIcon::decode(animation(0,true));
        QCOMPARE(repeat.state("")->frameAt(550000000,0),0);
        QCOMPARE(repeat.state("")->frameAt(600000000,0),1);
    }
    void diagonalDirections_data() {
        QTest::addColumn<int>("dirs");
        QTest::addColumn<int>("dir");
        QTest::addColumn<int>("cell");
        const int directions[] = {2,1,4,8,6,10,5,9};
        // Stock Dream Seeker 516.1688: four-direction diagonals use E/W,
        // not S/N. One-direction states ignore direction; eight keep it.
        const int four[] = {0,1,2,3,2,3,2,3};
        for (int dirs : {1,4,8}) for (int i = 0; i < 8; ++i)
            QTest::newRow(qPrintable(QString("%1-dirs-%2").arg(dirs).arg(directions[i])))
                << dirs << directions[i] << (dirs == 1 ? 0 : dirs == 4 ? four[i] : i);
    }
    void diagonalDirections() {
        QFETCH(int,dirs); QFETCH(int,dir); QFETCH(int,cell);
        const auto icon = DmiIcon::decode(animation(0,false,dirs));
        for (const auto& name : {QString(),QString("missing")}) {
            const auto* state = icon.state(name); QVERIFY(state);
            for (int f = 0; f < 3; ++f) {
                QCOMPARE(icon.index(*state,dir,f),cell + f * dirs);
                QCOMPARE(icon.frame(name,dir,f).pixelColor(16,0),QColor(50+80*f,30+20*cell,20));
            }
            // Ordinary and one-shot playback must retain the directional cell
            // while advancing frames (without changing either timeline).
            QCOMPARE(icon.index(*state,dir,state->frameAt(200000000,0)),cell + 2 * dirs);
            QCOMPARE(icon.index(*state,dir,state->flickFrameAt(200000000,0)),cell + 2 * dirs);
        }
        if (dirs == 4) {
            const auto named = DmiIcon::decode(flickAsset());
            const auto* state = named.state("pulse"); QVERIFY(state);
            QCOMPARE(named.index(*state,dir,2),state->first + cell + 8);
            QCOMPARE(named.frame("pulse",dir,2).pixelColor(16,0),QColor(210,30+20*cell,20));
        }
    }
    void flickTiming() {
        for (int loops : {0,1,2,65535}) for (bool rewind : {false,true}) {
            const auto icon=DmiIcon::decode(animation(loops,rewind)); const auto& s=*icon.state("");
            QCOMPARE(s.flickFrameAt(1000000000,1000000000),0);
            QCOMPARE(s.flickFrameAt(1049999999,1000000000),0);
            QCOMPARE(s.flickFrameAt(1050000000,1000000000),1);
            QCOMPARE(s.flickFrameAt(1200000000,1000000000),2);
            QCOMPARE(s.flickFrameAt(1399999999,1000000000),2);
            QCOMPARE(s.flickFrameAt(1400000000,1000000000),-1);
            QCOMPARE(s.flickFrameAt(INT64_MAX,INT64_MIN),-1);
            QCOMPARE(s.flickFrameAt(INT64_MIN,INT64_MAX),0);
        }
        const auto single=DmiIcon::decode(flickAsset());
        QCOMPARE(single.state("")->flickFrameAt(199999999,0),0);
        QCOMPARE(single.state("")->flickFrameAt(200000000,0),-1);
        QImage image(1,1,QImage::Format_RGBA8888); image.fill(Qt::red);
        const auto plain=DmiIcon::decode(png(image));
        QCOMPARE(plain.state("")->flickFrameAt(100000000,0),-1);
    }
    void dmiMalformedAndBudgets() {
        for (QString delay : {"0,-1,nan", "inf,1e100,bad", ""}) {
            const auto icon = DmiIcon::decode(animation(0,false,1,delay));
            QVERIFY(icon.state("")); QCOMPARE(icon.state("")->frameAt(100000000,0),1);
        }
        auto tiny = DmiIcon::decode(animation(0,false,1,"0.000001,0.000001,0.000001"));
        QCOMPARE(tiny.state("")->frameAt(1000000,0),1);
        QImage image(32,32,QImage::Format_RGBA8888); image.fill(Qt::red);
        for (QString text : QStringList{"width = 0\nstate = \"\"", "state = \"\"\nframes = 2147483647", "state = \"\"\nframes = 2",
                             "state = \"\"\ndirs = 7", QString(1024*1024+1,'x')}) {
            image.setText("Description",text); QVERIFY(DmiIcon::decode(png(image)).sheet.isNull());
        }
        // A valid-sized sheet cannot force overflowing prefix sums via long delays.
        QImage longCycle(400,1,QImage::Format_RGBA8888); longCycle.fill(Qt::red);
        longCycle.setText("Description",QString("width = 1\nheight = 1\nstate = \"\"\nframes = 400\nrewind = 1\ndelay = ") + QString("864000,").repeated(399) + "864000");
        QVERIFY(DmiIcon::decode(png(longCycle)).sheet.isNull());
        // Ordinary and movement states have separate names even when metadata puts
        // the movement variant first. Ordinary selection must not use movement pixels.
        QImage variants(64,32,QImage::Format_RGBA8888); variants.fill(Qt::red);
        for(int y=0;y<32;++y) for(int x=32;x<64;++x) variants.setPixelColor(x,y,Qt::blue);
        variants.setText("Description","state = \"\"\nmovement = 1\nstate = \"\"\n");
        QCOMPARE(DmiIcon::decode(png(variants)).frame("",2).pixelColor(0,0),QColor(Qt::blue));
        image.setText("Description","state = \"only\"\nframes = 1");
        auto named=DmiIcon::decode(png(image)); QVERIFY(named.frame("absent",2).isNull());
        image.setText("Description",""); auto plain=DmiIcon::decode(png(image));
        QCOMPARE(plain.frame("any",2),image.convertToFormat(plain.sheet.format()));
    }
    void wholeIconOpacity() {
        // Pixel alpha oracle exported by the stock opacity fixture, including half ties.
        QImage source = QImage::fromData(flickAsset()).convertToFormat(QImage::Format_RGBA8888);
        const int alphas[] = {0,1,63,64,127,128,254,255};
        for (int i=0;i<8;++i) source.setPixelColor(i,0,QColor(255,255,255,alphas[i]));
        const auto bytes = png(source);
        const int expected[][8] = {{0,0,0,0,0,0,0,0}, {0,1,32,32,64,64,127,128},
            {0,1,63,64,127,128,254,255}, {0,2,126,128,254,255,255,255}, {0,255,255,255,255,255,255,255}};
        int row = 0;
        for (float factor : {0.f,.5f,1.f,2.f,10000.f}) {
            IconCache cache; IconCache::Resources resources{{10,3},{20,12}};
            auto read = [&](uint32_t id,QByteArray& out) { out = id==10 ? bytes : opacityfixture::recipe(0,factor); return true; };
            const auto result = finish(cache,resources,read,1); QVERIFY(result.ready);
            const auto& icon = cache.icon(20); QVERIFY(!icon.sheet.isNull()); QVERIFY(icon.generated);
            QCOMPARE(icon.cell,QSize(32,32));
            for (int i=0;i<8;++i) QCOMPARE(icon.sheet.pixelColor(i,0).alpha(),expected[row][i]);
            const auto original = DmiIcon::decode(bytes);
            for (const auto& name : {QString(),QString("pulse"),QString("other"),QString("short")}) {
                const auto* state=icon.state(name); const auto* controlState=original.state(name); QVERIFY(state); QVERIFY(controlState);
                QCOMPARE(state->first,controlState->first); QCOMPARE(state->dirs,controlState->dirs); QCOMPARE(state->frames,controlState->frames);
                QCOMPARE(state->loop,controlState->loop); QCOMPARE(state->rewind,controlState->rewind); QCOMPARE(*state->ends,*controlState->ends);
                for (int dir : {2,1,4,8}) for (int frame=0;frame<icon.state(name)->frames;++frame) {
                    const auto pixel=icon.frame(name,dir,frame).pixelColor(16,0);
                    const auto control=original.frame(name,dir,frame).pixelColor(16,0);
                    QCOMPARE(pixel.red(),control.red()); QCOMPARE(pixel.green(),control.green()); QCOMPARE(pixel.blue(),control.blue());
                }
            }
            const auto key=icon.sheet.cacheKey();
            cache.begin(resources,read); QCOMPARE(cache.lookup(1),result); QCOMPARE(cache.reads(),uint64_t(0));
            QCOMPARE(cache.icon(20).sheet.cacheKey(),key); ++row;
        }
    }
    void wholeIconOpacityBoundariesAndDependencies() {
        const auto valid=opacityfixture::recipe(1);
        QList<QByteArray> invalid;
        for (int size=8;size<valid.size();++size) invalid << valid.left(size);
        invalid << opacityfixture::recipe(1,std::numeric_limits<float>::infinity())
                << opacityfixture::recipe(1,std::numeric_limits<float>::quiet_NaN());
        invalid << recipe(valid.mid(8)+QByteArray::fromHex("4a14")); // no partial prefix / arbitrary matrix chains
        for (const auto& bad : invalid) {
            IconCache cache; IconCache::Resources resources{{10,12},{20,3}};
            cache.begin(resources,[&](uint32_t id,QByteArray& out){out=id==10?bad:animation();return true;});
            QVERIFY(cache.lookup(0).ready); QVERIFY(cache.icon(10).sheet.isNull()); QCOMPARE(cache.reads(),uint64_t(1));
        }
        IconCache cache; IconCache::Resources resources{{10,12}};
        std::map<uint32_t,QByteArray> data{{10,valid}};
        auto read=[&](uint32_t id,QByteArray& out) { if(!data.count(id)) return false; out=data.at(id);return true; };
        cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready);
        resources.push_back({20,3}); cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready);
        data[20]=animation(); const auto first=finish(cache,resources,read,0); QVERIFY(first.ready);
        const auto retained=cache.icon(10).sheet;
        resources[1]={30,3}; data[30]=flickAsset(); const auto second=finish(cache,resources,read,0);
        QVERIFY(second.ready); QVERIFY(second.revision != first.revision); QVERIFY(cache.icon(10).state("pulse"));
        QCOMPARE(retained.size(),QSize(128,96));
        resources[1]={10,12}; cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready); // cycle
        // Whole-sheet allocation, not just the selected cell, is preflighted.
        QImage large(4096,4097,QImage::Format_RGBA8888); large.fill(Qt::red);
        large.setText("Description","width = 32\nheight = 1\nstate = \"\"\n");
        IconCache bounded; resources={{10,12},{20,3}}; data[20]=png(large);
        bounded.begin(resources,read); QCOMPARE(bounded.lookup(0).status,IconCache::Status::SafetyLimit);
        QVERIFY(bounded.icon(10).sheet.isNull());
    }
    void denseResidentSources() {
        // 192 MiB of live sheets: a 128 MiB retention policy reloads hot assets
        // each epoch, changing revisions and downstream frame/texture keys.
        QImage source(2048,2048,QImage::Format_RGBA8888); source.fill(Qt::red);
        const auto bytes = png(source);
        IconCache cache; IconCache::Resources resources;
        for (uint32_t id = 0; id < 12; ++id) resources.push_back({id,3});
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        cache.begin(resources,read);
        std::vector<IconCache::Result> results;
        std::vector<qint64> keys;
        for (uint32_t id = 0; id < 12; ++id) {
            results.push_back(cache.lookup(id)); QVERIFY(results.back().ready);
            keys.push_back(cache.icon(id).sheet.cacheKey());
        }
        QCOMPARE(cache.reads(),uint64_t(12));
        const QImage retained = cache.icon(0).sheet;
        for (int epoch = 0; epoch < 3; ++epoch) {
            cache.begin(resources,read);
            for (uint32_t id = 0; id < 12; ++id) {
                QCOMPARE(cache.lookup(id),results[id]);
                QCOMPARE(cache.icon(id).sheet.cacheKey(),keys[id]);
            }
            QCOMPARE(cache.reads(),uint64_t(0));
        }
        // Retention remains bounded: 288 MiB forces oldest-entry eviction at
        // the next boundary, without invalidating already published images.
        for (uint32_t id = 12; id < 18; ++id) resources.push_back({id,3});
        cache.begin(resources,read);
        for (uint32_t id = 12; id < 18; ++id) QVERIFY(cache.lookup(id).ready);
        cache.begin(resources,read);
        QVERIFY(cache.lookup(17).ready); QCOMPARE(cache.reads(),uint64_t(0));
        const auto reloaded = cache.lookup(0); QVERIFY(reloaded.ready);
        QVERIFY(reloaded.revision != results[0].revision);
        QCOMPARE(cache.reads(),uint64_t(1));
        QCOMPARE(retained.cacheKey(),keys[0]);
        QCOMPARE(retained.pixelColor(0,0),QColor(Qt::red));
    }
    void generatedAnimationRetainsPlayback() {
        IconCache cache; IconCache::Resources resources{{10,3},{20,12}};
        QVERIFY(finish(cache,resources,[](uint32_t id,QByteArray& out){out=id==10?animation(2,true):recipe(select(0,"",2));return true;},1).ready);
        const auto& icon = cache.icon(20);
        QCOMPARE(icon.state("")->frames,3);
        QCOMPARE(icon.state("")->loop,2); QVERIFY(icon.state("")->rewind);
        QCOMPARE(*icon.state("")->ends,*cache.icon(10).state("")->ends);
        for (int f=0;f<3;++f) QCOMPARE(icon.frame("",2,f).pixelColor(16,0),QColor(50+80*f,70,20));
    }
    void operationsAndDependencies() {
        IconCache cache; IconCache::Resources resources{{10,12}};
        std::map<uint32_t,QByteArray> data{{10,masked(1)}};
        auto read = [&](uint32_t id,QByteArray& out) { if (!data.count(id)) return false; out=data.at(id); return true; };
        cache.begin(resources,read);
        QVERIFY(!cache.lookup(0).ready); // missing ordinal, then missing resource bytes
        resources.push_back({20,3}); cache.begin(resources,read);
        const auto missing=cache.lookup(0); QVERIFY(!missing.ready);
        cache.begin(resources,read); QCOMPARE(cache.lookup(0),missing);
        data[20]=sheet();
        const auto result=finish(cache,resources,read,0); QVERIFY(result.ready);
        const auto first=cache.icon(10).frame("retained-old-state",8);
        QCOMPARE(first.size(),QSize(32,32)); QCOMPARE(first.pixelColor(0,0),QColor(Qt::transparent));
        QCOMPARE(first.pixelColor(20,0),QColor(Qt::red));
        cache.begin(resources,read); QCOMPARE(cache.lookup(0),result); QCOMPARE(cache.reads(),uint64_t(0));
        resources[1]={30,3}; cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready);
        data[30]=sheet(Qt::blue); const auto changed=finish(cache,resources,read,0);
        QVERIFY(changed.ready); QVERIFY(changed.revision!=result.revision);
        QCOMPARE(cache.icon(10).frame("",2).pixelColor(20,0),QColor(Qt::blue));
        QCOMPARE(first.pixelColor(20,0),QColor(Qt::red));
    }
    void fixedSelection() {
        QImage source(256,32,QImage::Format_RGBA8888);
        for(int y=0;y<32;++y) for(int x=0;x<256;++x) source.setPixelColor(x,y,QColor(x/32*30,50,100));
        source.setText("Description","width = 32\nheight = 32\nstate = \"eight\"\ndirs = 8\nframes = 1\n");
        std::map<uint32_t,QByteArray> data{{10,png(source)}};
        IconCache cache; IconCache::Resources resources{{10,3}};
        for(int i=0;i<8;++i) { resources.push_back({uint32_t(20+i),12}); data[20+i]=recipe(select(0,"eight",i)); }
        const IconCache::Read read=[&](uint32_t id,QByteArray& out){out=data.at(id);return true;};
        for(int i=0;i<8;++i) {
            QVERIFY(finish(cache,resources,read,uint32_t(i+1)).ready);
            for(int dir:{1,2,4,8}) QCOMPARE(cache.icon(20+i).frame("old-state",dir).pixelColor(0,0),QColor(i*30,50,100));
        }
    }
    void capturedOverlayAndExactSwap() {
        // Synthetic pixels and expected results exported by a 516.1688 DM probe.
        QImage input(32,64,QImage::Format_RGBA8888);
        const QColor colors[] = {QColor(255,0,255,255),QColor(100,200,70,128),QColor(33,47,83,0),QColor(21,54,231,255),
            QColor(200,30,100,64),QColor(40,80,120,192),QColor(0,0,0,255),QColor(255,255,255,255)};
        for(int y=0;y<64;++y) for(int x=0;x<32;++x) input.setPixelColor(x,y,colors[y<32 ? x/4 : 7-x/4]);
        input.setText("Description","width = 32\nheight = 32\nstate = \"a\"\ndirs = 1\nframes = 1\nstate = \"b\"\ndirs = 1\nframes = 1\n");
        auto overlay=recipe(select(0,"a",0)+select(0,"b",0)+QByteArray::fromHex("4207"));
        auto swap=recipe(select(0,"a",0)+QByteArray::fromHex("03ff00ffff03123456784801"));
        std::map<uint32_t,QByteArray> data{{10,png(input)},{20,overlay},{30,swap}};
        IconCache cache; IconCache::Resources resources{{10,3},{20,12},{30,12}};
        const IconCache::Read read=[&](uint32_t id,QByteArray& out){out=data.at(id);return true;};
        QVERIFY(finish(cache,resources,read,1).ready); QVERIFY(finish(cache,resources,read,2).ready);
        const auto image=cache.icon(20).sheet;
        const QColor expected[]={Qt::white,Qt::black,QColor(40,80,120,192),QColor(66,48,198,255),QColor(21,54,231,255),
            QColor(40,80,120,192),QColor(50,100,35,255),QColor(255,0,255,255)};
        for(int i=0;i<8;++i) QCOMPARE(image.pixelColor(i*4,0),expected[i]);
        QCOMPARE(cache.icon(30).sheet.pixelColor(0,0),QColor(18,52,86,120));
        QCOMPARE(cache.icon(30).sheet.pixelColor(4,0),colors[1]);
    }
    void rejectWholeProgram() {
        const auto valid=masked(1);
        QList<QByteArray> invalid;
        for(int i=4;i<valid.size();++i) invalid.push_back(valid.left(i));
        invalid << valid+char(0) << recipe(select(1,"base")+QByteArray::fromHex("4204"))
                << recipe(select(1,"base")+QByteArray::fromHex("06"))
                << recipe(QByteArray::fromHex("4207"));
        QByteArray deep; for(int i=0;i<33;++i) deep+=select(1,"base");
        for(int i=0;i<32;++i) deep+=QByteArray::fromHex("4207");
        invalid << recipe(deep);
        for(const auto& bytes:invalid) {
            IconCache cache; IconCache::Resources resources{{10,12},{20,3}}; int reads=0;
            cache.begin(resources,[&](uint32_t id,QByteArray& out){++reads;out=id==10?bytes:sheet();return true;});
            QVERIFY(cache.lookup(0).ready); QVERIFY(cache.icon(10).sheet.isNull()); QCOMPARE(reads,1);
        }
    }
    void nestedRecipesAndDepthBound() {
        IconCache cache; IconCache::Resources resources{{10,12},{20,12},{30,3}};
        std::map<uint32_t,QByteArray> data{{10,recipe(select(1,""))},{20,masked(2)},{30,sheet()}};
        auto read=[&](uint32_t id,QByteArray& out){out=data.at(id);return true;};
        QVERIFY(finish(cache,resources,read,0).ready);
        QCOMPARE(cache.icon(10).sheet.pixelColor(20,0),QColor(Qt::red));
        const auto retained=cache.icon(10).sheet;
        // A remap deep in the graph invalidates every dependent image, not just the leaf.
        resources[2]={40,3}; data[40]=sheet(Qt::blue);
        QVERIFY(finish(cache,resources,read,0).ready); QCOMPARE(cache.icon(10).sheet.pixelColor(20,0),QColor(Qt::blue));
        QCOMPARE(retained.pixelColor(20,0),QColor(Qt::red));
        IconCache deep; resources.clear(); data.clear();
        for(uint32_t i=0;i<64;++i) {resources.push_back({100+i,12});data[100+i]=recipe(select(i+1,""));}
        resources.push_back({164,3}); data[164]=sheet();
        deep.begin(resources,read); QVERIFY(!deep.lookup(0).ready); QVERIFY(deep.reads()<=32);
    }
    void softWorkBudget() {
        QImage large(256,256,QImage::Format_RGBA8888); large.fill(Qt::red);
        QByteArray program=select(1,"");
        for(int i=0;i<120;++i) program+=QByteArray::fromHex("4402");
        const auto bytes=recipe(program), source=png(large);
        IconCache cache; IconCache::Resources resources{{10,12},{20,3}};
        auto read=[&](uint32_t id,QByteArray& out){out=id==10?bytes:source;return true;};
        cache.begin(resources,read); auto result=cache.lookup(0);
        int preparations=1;
        while (!result.ready && preparations++ < 1000) {
            QCOMPARE(result.status,IconCache::Status::Deferred);
            QVERIFY(cache.icon(10).sheet.isNull()); // never publish a partial stack
            QVERIFY(cache.lookup(1).ready); // independent work remains available
            const auto work=cache.metrics().workPixels;
            cache.lookup(0); QCOMPARE(cache.metrics().workPixels,work);
            cache.begin(resources,read); result=cache.lookup(0);
        }
        QCOMPARE(result.status,IconCache::Status::Ready);
        QCOMPARE(cache.icon(10).sheet,large);
        QVERIFY(cache.metrics().lastRecipePixels > 16*1024*1024);
        QCOMPARE(cache.metrics().starts,uint64_t(1));
        QVERIFY(cache.metrics().yields > 0);
        QVERIFY(cache.metrics().peakBytes <= 64*1024*1024);
        qInfo() << "recipe work/slices/maxSliceNs" << cache.metrics().lastRecipePixels
                << preparations << cache.metrics().maxSliceNs;
        const auto retained=cache.icon(10).sheet;
        const auto work=cache.metrics().workPixels;
        for(int i=0;i<5;++i) {
            cache.begin(resources,read); QCOMPARE(cache.lookup(0).revision,result.revision);
            QCOMPARE(cache.reads(),uint64_t(0)); QCOMPARE(cache.metrics().workPixels,work);
        }
        QCOMPARE(cache.icon(10).sheet.cacheKey(),retained.cacheKey());
        QVERIFY(cache.metrics().cacheHits >= 5);
        QCOMPARE(cache.metrics().warnings,uint64_t(1));
    }
    void largeSheetSelection() {
        // Independent screen-map-scope stock fixture: 341 states / 1366 cells
        // on a 1184-square sheet, two four-direction extractions and an overlay.
        // Same work shape as the recorded TG refusal, with no game assets.
        QImage image(1184,1184,QImage::Format_RGBA8888); image.fill(Qt::red);
        for(int y=0;y<32;++y) for(int x=128;x<256;++x) image.setPixelColor(x,y,Qt::green);
        QString description="width = 32\nheight = 32\n";
        for(int i=0;i<341;++i) description+=QString("state = \"s%1\"\ndirs = %2\nframes = %3\n").arg(i).arg(i<339?4:1).arg(i==338?3:1);
        image.setText("Description",description);
        const auto source=png(image);
        const auto bytes=recipe(select(1,"s0",255)+select(1,"s1",255)+QByteArray::fromHex("4207"));
        IconCache cache; IconCache::Resources resources{{10,12},{20,3}};
        auto read=[&](uint32_t id,QByteArray& out){out=id==10?bytes:source;return true;};
        IconCache::Result result;
        int preparations=0;
        do { cache.begin(resources,read); result=cache.lookup(0); } while(!result.ready && ++preparations<1000);
        QCOMPARE(result.status,IconCache::Status::Ready);
        QCOMPARE(cache.metrics().lastRecipePixels,uint64_t(25274368));
        // Reproduce the old precise refusal: after the dependency copies and
        // first extraction, the second extraction cannot fit the shared 16M.
        constexpr uint64_t copies=2803712, extraction=11202560;
        QVERIFY(copies+extraction <= 16*1024*1024);
        QVERIFY(copies+2*extraction > 16*1024*1024);
        for(int dir : {1,2,4,8}) {
            const auto frame=cache.icon(10).frame("",dir);
            QCOMPARE(frame.size(),QSize(32,32));
            for(int y=0;y<32;++y) for(int x=0;x<32;++x) QCOMPARE(frame.pixelColor(x,y),QColor(Qt::green));
        }
        qInfo() << "large sheet work/peakBytes/maxSliceNs" << cache.metrics().lastRecipePixels
                << cache.metrics().peakBytes << cache.metrics().maxSliceNs;
        const auto retained=cache.icon(10).sheet;
        const auto work=cache.metrics().workPixels;
        cache.begin(resources,read); QCOMPARE(cache.lookup(0).revision,result.revision);
        QCOMPARE(cache.metrics().workPixels,work); QCOMPARE(cache.icon(10).sheet.cacheKey(),retained.cacheKey());
    }
    void recipeMemoryDeferral() {
        const auto bytes=masked(1);
        fugue::assets::DdmiRecipe program; std::string error;
        QVERIFY(fugue::assets::parseDdmi(reinterpret_cast<const uint8_t*>(bytes.constData()),size_t(bytes.size()),program,&error));
        const auto input=DmiIcon::decode(sheet()).toAsset();
        fugue::assets::DdmiEvaluation deferred(program,{input,input}), immediate(program,{input,input});
        using Clock=fugue::assets::DdmiEvaluation::Clock;
        // Force each operation's temporary-memory yield, including two popped
        // SwapColor arguments. Resumption must not repeat work or lose arguments.
        bool done=false;
        for(int i=0;i<32 && !done;++i) {
            const auto work=deferred.workPixels();
            done=deferred.step(Clock::time_point::max(),0);
            QCOMPARE(deferred.workPixels(),work);
            if(!done) done=deferred.step(Clock::time_point::min());
        }
        QVERIFY(done); QVERIFY(immediate.step(Clock::time_point::max()));
        QCOMPARE(deferred.workPixels(),immediate.workPixels());
        const auto a=deferred.takeResult(), b=immediate.takeResult();
        QVERIFY(a.icon()); QVERIFY(b.icon()); QCOMPARE(a.icon()->sheet.px,b.icon()->sheet.px);
    }
    void generatedSafetyAndCancellation() {
        QImage large(256,256,QImage::Format_RGBA8888); large.fill(Qt::red);
        auto bytes=recipe(select(1,"")+QByteArray::fromHex("4b00200020")); // legal axes, unsafe live storage
        auto source=png(large);
        IconCache cache; IconCache::Resources resources{{10,12},{20,3}};
        auto read=[&](uint32_t id,QByteArray& out){out=id==10?bytes:source;return true;};
        auto result=finish(cache,resources,read,0); QCOMPARE(result.status,IconCache::Status::SafetyLimit);
        QVERIFY(cache.icon(10).sheet.isNull());
        const auto work=cache.metrics().workPixels;
        for(int i=0;i<3;++i) { cache.begin(resources,read); QCOMPARE(cache.lookup(0).revision,result.revision); }
        QCOMPARE(cache.metrics().workPixels,work); QCOMPARE(cache.metrics().starts,uint64_t(1));
        cache.trim(0,0);
        QByteArray body=select(1,""); for(int i=0;i<500;++i) body+=QByteArray::fromHex("4402");
        bytes=recipe(body); cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready);
        // Change a dependency while the job is suspended: no stale red publication.
        large.fill(Qt::blue); source=png(large); resources[1]={30,3};
        result=finish(cache,resources,read,0); QCOMPARE(result.status,IconCache::Status::Ready);
        QCOMPARE(cache.icon(10).sheet,large);
        QVERIFY(cache.metrics().starts >= 3);
        // Evicting a suspended job must also release it, not permanently block the cache.
        cache.trim(0,0); cache.begin(resources,read); cache.lookup(0);
        cache.trim(0,0); result=finish(cache,resources,read,0); QCOMPARE(result.status,IconCache::Status::Ready);
    }
    void emptyIconsAndRecipeLimits() {
        IconCache cache; IconCache::Resources resources{{10,12},{20,12},{30,3}};
        // Empty icon as a separately cached dependency, then Insert + Flip as a
        // whole recipe; cannot confuse a valid empty icon with a missing source.
        const auto empty=recipe(QByteArray::fromHex("01ffff000001"));
        const auto insert=recipe(QByteArray::fromHex("010000000008010200000008050041ffff00000000004402"));
        const auto source=animation(2,true);
        auto read=[&](uint32_t id,QByteArray& out) { out=id==10?empty:id==20?insert:source;return true; };
        const auto result=finish(cache,resources,read,1);
        QCOMPARE(result.status,IconCache::Status::Ready); QVERIFY(result.ready);
        QCOMPARE(cache.icon(20).state("")->frames,3); QCOMPARE(cache.icon(20).state("")->dirs,4);
        QCOMPARE(cache.icon(20).state("")->loop,2); QVERIFY(cache.icon(20).state("")->rewind);
        QCOMPARE(*cache.icon(20).state("")->ends,*cache.icon(30).state("")->ends);
        const auto retained=cache.icon(20).sheet;
        cache.trim(0,0); QVERIFY(finish(cache,resources,read,1).ready);
        QCOMPARE(cache.icon(20).sheet,retained);
        for (const auto& body : {
                select(2,"")+QByteArray::fromHex("4bff7fff7f"), // huge Scale
                select(2,"")+QByteArray::fromHex("4c00800080ff7fff7f"), // huge Crop
                select(2,"")+QByteArray(1024,char(0x3f)), // operation cap
                select(2,"")+QByteArray::fromHex("450000c07f"), // NaN Turn
                select(2,"")+QByteArray::fromHex("4a10")+QByteArray(64,'\0'), // unsupported matrix
                select(2,"")+QByteArray::fromHex("06")}) {
            IconCache bounded;
            const auto refused=finish(bounded,resources,[&](uint32_t id,QByteArray& out){out=id==20?recipe(body):animation();return true;},1);
            QVERIFY(refused.status==IconCache::Status::SafetyLimit || refused.status==IconCache::Status::UnsupportedRecipe);
            QVERIFY(bounded.icon(20).sheet.isNull());
        }
    }
    void cyclesAndRecovery() {
        IconCache cache; IconCache::Resources resources{{10,12},{20,12},{30,3}};
        std::map<uint32_t,QByteArray> data{{10,recipe(select(1,""))},{20,recipe(select(0,""))},{30,sheet()}};
        auto read=[&](uint32_t id,QByteArray& out){out=data.at(id);return true;};
        for(int i=0;i<3;++i) { cache.begin(resources,read); QVERIFY(!cache.lookup(0).ready); }
        resources[1]={30,3}; QVERIFY(finish(cache,resources,read,0).ready);
        // Missing-state icon() leaves the source unchanged (f_nostate corpus);
        // ordinary selection still has no default state to draw.
        QVERIFY(cache.icon(10).frame("",2).isNull());
    }
};
FUGUE_TEST_MAIN(IconCacheTest)
#include "iconcache_test.moc"
