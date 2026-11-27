#include <QtTest>
#include "tests/testmain.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "shell/recipefiler.h"
#include "shell/localorigin.h"
#include "shell/tests/iconfixture.h"
using namespace fugue;
using namespace fugue::shell;
namespace {
bool offer(BrowseRecipeFiler& filer, uint32_t id, const std::string& name, const QByteArray& bytes) {
    return filer.onResource(id, name, ByteSpan(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size())));
}
IconCache::Read reader(const std::map<uint32_t, QByteArray>& data, int* reads = nullptr) {
    return [&data, reads](uint32_t id, QByteArray& bytes) {
        if (reads) ++*reads;
        const auto it = data.find(id);
        if (it == data.end()) return false;
        bytes = it->second; return true;
    };
}
void finish(BrowseRecipeFiler& filer, const IconCache::Resources& resources,
            const IconCache::Read& read, BrowseRscStore& store) {
    for (int i = 0; i < 1000 && filer.pending(); ++i) filer.pump(resources, read, store);
}
}
class RecipeFilerTest : public QObject {
    Q_OBJECT
private slots:
    void firstCellOverHttp() {
        BrowseRecipeFiler filer;
        auto store = std::make_shared<BrowseRscStore>();
        store->declare("avatar.png", 10);
        store->alias("alias.png", 10);
        IconCache::Resources resources{{10,12},{20,3}};
        const auto recipe = iconfixture::recipe(iconfixture::select(1,"pulse",255));
        const auto source = iconfixture::flickAsset();
        const std::map<uint32_t,QByteArray> data{{10,recipe},{20,source}};
        QVERIFY(offer(filer,10,"runtime/avatar.png",recipe));
        int reads = 0;
        finish(filer,resources,reader(data,&reads),*store);
        QCOMPARE(filer.pending(),size_t(0));
        std::string png, alias;
        QVERIFY(store->get("avatar.png",png)); QVERIFY(store->get("alias.png",alias)); QCOMPARE(png,alias);
        const auto image = QImage::fromData(QByteArray::fromStdString(png));
        // Independent expected first pulse cell, not the sheet's unnamed cell or a later direction/frame.
        QCOMPARE(image.size(),QSize(32,32)); QCOMPARE(image.pixelColor(20,10),QColor(50,30,20));
        const int before = reads;
        for (int i = 0; i < 10; ++i) filer.pump(resources,reader(data,&reads),*store);
        QCOMPARE(reads,before);
        LocalOrigin origin(store); QVERIFY(origin.listening());
        QNetworkAccessManager network;
        auto* reply = network.get(QNetworkRequest(QUrl(origin.urlFor("alias.png"))));
        QSignalSpy done(reply,&QNetworkReply::finished);
        QVERIFY(done.wait(3000)); QCOMPARE(reply->error(),QNetworkReply::NoError);
        QCOMPARE(reply->header(QNetworkRequest::ContentTypeHeader).toString(),QString("image/png"));
        QCOMPARE(QImage::fromData(reply->readAll()),image); delete reply;
    }
    void ordinaryDmiOverHttp_data() {
        QTest::addColumn<QByteArray>("source");
        QTest::addColumn<QColor>("firstColor");
        QTest::newRow("two-cells") << iconfixture::sheet(Qt::red) << QColor(Qt::red);
        QTest::newRow("directions-and-frames") << iconfixture::animation() << QColor(50,30,20);
        QImage ordered(64,32,QImage::Format_RGBA8888); ordered.fill(Qt::blue);
        for (int y=0;y<32;++y) for (int x=32;x<64;++x) ordered.setPixelColor(x,y,Qt::red);
        ordered.setText("Description", "width = 32\nheight = 32\nstate = \"z-first\"\ndirs = 1\nframes = 1\nstate = \"a-second\"\ndirs = 1\nframes = 1\n");
        QTest::newRow("metadata-not-sorted-order") << iconfixture::png(ordered) << QColor(Qt::blue);
    }
    void ordinaryDmiOverHttp() {
        QFETCH(QByteArray, source); QFETCH(QColor, firstColor);
        BrowseRecipeFiler filer;
        auto store = std::make_shared<BrowseRscStore>();
        store->declare("sheet.png",10); store->alias("alias.png",10);
        const std::map<uint32_t,QByteArray> data{{10,source}};
        QVERIFY(offer(filer,10,"icons/sheet.png",source));
        std::string bytes; QVERIFY(!store->get("sheet.png",bytes)); // never publish the sheet first
        int reads=0;
        finish(filer,{},reader(data,&reads),*store); // browse_rsc need not have an ordinal
        QCOMPARE(filer.pending(),size_t(0)); QCOMPARE(reads,1);
        LocalOrigin origin(store); QVERIFY(origin.listening());
        QNetworkAccessManager network;
        for (const auto* name : {"sheet.png","alias.png","sheet.png"}) {
            auto* reply=network.get(QNetworkRequest(QUrl(origin.urlFor(name))));
            QSignalSpy done(reply,&QNetworkReply::finished);
            QVERIFY(done.wait(3000)); QCOMPARE(reply->error(),QNetworkReply::NoError);
            QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),200);
            QCOMPARE(reply->header(QNetworkRequest::ContentTypeHeader).toString(),QString("image/png"));
            const auto image=QImage::fromData(reply->readAll()); delete reply;
            QCOMPARE(image.size(),QSize(32,32));
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) QCOMPARE(image.pixelColor(x,y),firstColor);
            filer.pump({},reader(data,&reads),*store);
        }
        QCOMPARE(reads,1); // HTTP requests and subsequent ticks do not decode again
    }
    void ordinaryImagesAndBounds() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        QImage plain(64,32,QImage::Format_RGBA8888); plain.fill(Qt::cyan);
        plain.setText("Comment","keep metadata and original encoding");
        QImage single(32,32,QImage::Format_RGBA8888); single.fill(Qt::red);
        single.setText("Description","width = 32\nheight = 32\nstate = \"\"\nframes = 1\ndirs = 1\n");
        const std::map<uint32_t,QByteArray> data{{1,iconfixture::png(plain)},{2,iconfixture::png(single)},
            {3,QByteArray("\x89PNG\r\n\x1a\ntruncated")}};
        for (const auto& [id,bytes] : data) QVERIFY(offer(filer,id,std::to_string(id)+".png",bytes));
        int reads=0;
        filer.pump({},reader(data,&reads),store); QCOMPARE(reads,1); QCOMPARE(filer.pending(),size_t(2));
        finish(filer,{},reader(data,&reads),store); QCOMPARE(reads,3);
        for (const auto& [id,original] : data) {
            std::string bytes; QVERIFY(store.get(std::to_string(id)+".png",bytes));
            QCOMPARE(QByteArray::fromStdString(bytes),original);
        }
        for (const auto* bytes : {"GIF89a", "\xff\xd8\xff", "OggS", "ordinary text"})
            QVERIFY(!offer(filer,20,"media.bin",QByteArray(bytes)));
    }
    void oversizedPngIsNotRetried() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        QImage large(3072,3072,QImage::Format_RGBA8888); large.fill(Qt::red);
        QVERIFY(uint64_t(large.width())*large.height()>BrowseRecipeFiler::maxExportPixels);
        large.setText("Description","width = 32\nheight = 32\nstate = \"\"\ndirs = 1\nframes = 2\n");
        const auto source=iconfixture::png(large);
        const std::map<uint32_t,QByteArray> data{{10,source}};
        QVERIFY(offer(filer,10,"large.png",source));
        int reads=0; finish(filer,{},reader(data,&reads),store);
        QCOMPARE(filer.pending(),size_t(0)); QCOMPARE(reads,1);
        std::string bytes; QVERIFY(store.get("large.png",bytes)); QCOMPARE(QByteArray::fromStdString(bytes),source);
        filer.pump({},reader(data,&reads),store); QCOMPARE(reads,1);
    }
    void lateDependenciesAndMapping() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        store.declare("late.png",10);
        const auto recipe=iconfixture::recipe(iconfixture::select(1,"base"));
        std::map<uint32_t,QByteArray> data{{10,recipe}};
        QVERIFY(offer(filer,10,"late.png",recipe));
        IconCache::Resources resources;
        filer.pump(resources,reader(data),store); QCOMPARE(filer.pending(),size_t(1));
        resources={{10,12},{20,3}};
        filer.pump(resources,reader(data),store);
        std::string bytes; QVERIFY(!store.get("late.png",bytes)); QVERIFY(store.shouldWait("late.png"));
        // Remap the missing operand before it arrives; no second recipe offer is needed.
        resources[1]={30,3}; data[30]=iconfixture::sheet(Qt::blue);
        finish(filer,resources,reader(data),store);
        QCOMPARE(filer.pending(),size_t(0)); QVERIFY(store.get("late.png",bytes));
        QCOMPARE(QImage::fromData(QByteArray::fromStdString(bytes)).pixelColor(20,10),QColor(Qt::blue));
    }
    void cooperativeContinuation() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        QImage large(256,256,QImage::Format_RGBA8888); large.fill(Qt::red);
        QByteArray program=iconfixture::select(1,"");
        for(int i=0;i<120;++i) program+=QByteArray::fromHex("4402");
        const auto recipe=iconfixture::recipe(program);
        const std::map<uint32_t,QByteArray> data{{10,recipe},{20,iconfixture::png(large)}};
        const IconCache::Resources resources{{10,12},{20,3}};
        QVERIFY(offer(filer,10,"slow.png",recipe));
        filer.pump(resources,reader(data),store);
        std::string bytes;
        if (filer.pending()) QVERIFY(!store.get("slow.png",bytes)); // no partial export
        finish(filer,resources,reader(data),store); // tick-only continuation, no resource observer calls
        QCOMPARE(filer.pending(),size_t(0)); QVERIFY(store.get("slow.png",bytes));
        QCOMPARE(QImage::fromData(QByteArray::fromStdString(bytes)).convertToFormat(large.format()),large);
    }
    void refusalAndBounds() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        QVERIFY(!offer(filer,1,"ordinary.txt",QByteArray("plain text")));
        const auto bad=iconfixture::recipe(QByteArray::fromHex("06"));
        QVERIFY(offer(filer,10,"bad.png",bad));
        const std::map<uint32_t,QByteArray> data{{10,bad}};
        finish(filer,{{10,12}},reader(data),store);
        QCOMPARE(filer.pending(),size_t(0)); std::string bytes; QVERIFY(!store.get("bad.png",bytes));
        QVERIFY(offer(filer,11,"truncated.png",QByteArray("DDMI")));
        finish(filer,{{11,12}},reader({{11,QByteArray("DDMI")}}),store);
        QCOMPARE(filer.pending(),size_t(0)); QVERIFY(!store.get("truncated.png",bytes));
        for(uint32_t id=0;id<BrowseRecipeFiler::maxPending+10;++id) QVERIFY(offer(filer,100+id,"pending.png",bad));
        QCOMPARE(filer.pending(),BrowseRecipeFiler::maxPending);
        // A full queue still lets ordinary PNGs through unchanged, never drops them.
        QVERIFY(!offer(filer,5000,"overflow.png",iconfixture::sheet()));
        QVERIFY(!offer(filer,5001,std::string(5000,'a')+".png",iconfixture::sheet()));
        QCOMPARE(filer.pending(),BrowseRecipeFiler::maxPending);
    }
    void incrementalOrdinalScan() {
        BrowseRecipeFiler filer; BrowseRscStore store;
        const auto recipe=iconfixture::recipe(iconfixture::select(0,"base"));
        IconCache::Resources resources(BrowseRecipeFiler::maxOrdinalScan+2,{99,3});
        resources[0]={20,3}; resources.back()={10,12};
        const std::map<uint32_t,QByteArray> data{{10,recipe},{20,iconfixture::sheet()}};
        QVERIFY(offer(filer,10,"far.png",recipe));
        filer.pump(resources,reader(data),store); QCOMPARE(filer.pending(),size_t(1));
        finish(filer,resources,reader(data),store); QCOMPARE(filer.pending(),size_t(0));
        std::string bytes; QVERIFY(store.get("far.png",bytes));
    }
};
FUGUE_TEST_GUILESS_MAIN(RecipeFilerTest)
#include "recipefiler_test.moc"
