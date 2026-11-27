#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QFontDatabase>
#include <QJsonObject>
#include <future>
#include "shell/entitymap.h"
#include "net/measuretext.h"
#include "skin/liveskin.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/affinefixture.h"
using namespace fugue;
namespace {
QRect ink(const QImage& image) {
    QRect result;
    for (int y=0; y<image.height(); ++y) for (int x=0; x<image.width(); ++x)
        if (image.pixelColor(x,y).alpha()) result = result.united(QRect(x,y,1,1));
    return result;
}
std::string fields(QString text, int width=96, int height=32, int x=0, int y=0) {
    return QJsonDocument(QJsonObject{{"maptext",text},{"maptextW",width},{"maptextH",height},
        {"maptextX",x},{"maptextY",y}}).toJson(QJsonDocument::Compact).toStdString();
}
model::World world() {
    model::World w;
    w.bounds={true,20,20,0,3,3,3,3};
    w.resync.haveOrigin=true; w.resync.originX=w.resync.originY=4;
    auto& ap=w.appearances[1]; ap.fields.iconResID=UINT32_MAX; setFields(ap, fields("Hello"));
    auto& atom=w.atoms[42]; atom.hasPos=true; atom.x=atom.y=6; atom.appid=1;
    atom.haveKey=true; atom.key=99; atom.renderIdentity=123;
    return w;
}
}
class MaptextTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        // Windows' offscreen plugin does not consult installed system fonts.
        // Use the host's fonts for real glyph tests; never check them into fixtures.
        if (QGuiApplication::platformName()=="offscreen") {
            const auto fonts=qEnvironmentVariable("WINDIR")+"/Fonts";
            qputenv("QT_QPA_FONTDIR",fonts.toUtf8());
            QVERIFY(QFontDatabase::addApplicationFont(fonts+"/arial.ttf")>=0);
        }
#endif
    }
    void measureUsesRasterLayoutAndWireFraming() {
        shell::MapTextCache cache; cache.setStyle({".caption {font-size:12pt;line-height:1.5}","Arial",0});
        const QString text="<span class='caption'>Hello there<br>Second line</span>";
        const auto size=cache.measure(text,{},96); QCOMPARE(size.width(),96); QVERIFY(size.height()>20 && size.height()<100);
        auto raster=cache.lookup(text,size); QVERIFY(raster.image); const auto bounds=ink(*raster.image);
        QVERIFY(!bounds.isEmpty()); QVERIFY(bounds.bottom()<size.height());
        const auto tight=cache.measure(text,{},32); QVERIFY(tight.height()>size.height());
        const auto intrinsic=cache.measure("Hello",{},0); QVERIFY(intrinsic.width()>0 && intrinsic.width()<96);
        QCOMPARE(cache.measure("<img src='x'>",{},96),QSize(96,0)); // an image measures nothing; it is not a refusal
        QCOMPARE(cache.measure(QString(1000,'W'),{},0).height(),cache.measure("W",{},0).height()); // one line, however long
        QCOMPARE(cache.measure("",{},96),QSize(-1,-1)); // DreamSeeker's "cannot measure"
        Bytes request{'H','i',0,0,96,0,0x78,0x56,0x34,0x12};
        auto reply=net::measureTextReply(request,[&](const std::string& html,const std::string& style,uint16_t width) {
            const auto m=cache.measure(QString::fromStdString(html),QString::fromStdString(style),width);
            return std::pair<uint16_t,uint16_t>{uint16_t(m.width()),uint16_t(m.height())};
        });
        QVERIFY(reply); QCOMPARE(reply->size(),size_t(8)); QCOMPARE((*reply)[0],uint8_t(96)); QVERIFY((*reply)[2]>0);
        QCOMPARE(Bytes(reply->begin()+4,reply->end()),(Bytes{0x78,0x56,0x34,0x12}));
        const auto unsupported=net::measureTextReply(request,{}); QVERIFY(unsupported);
        QCOMPARE(Bytes(unsupported->begin(),unsupported->begin()+4),(Bytes{255,255,255,255}));
        for(size_t n=0;n<request.size();++n) QVERIFY(!net::measureTextReply(ByteSpan(request.data(),n),{}));
        request.push_back(0); QVERIFY(!net::measureTextReply(request,{}));
    }
    void rasterAndReuse() {
        shell::MapTextCache cache; cache.begin();
        const auto a=cache.lookup("Hello",{96,32});
        QCOMPARE(a.status,shell::MapTextCache::Status::Ready); QVERIFY(a.image);
        QCOMPARE(a.image->format(),QImage::Format_RGBA8888); QVERIFY(!ink(*a.image).isEmpty());
        QCOMPARE(a.image->pixelColor(95,31),QColor(Qt::transparent));
        QCOMPARE(cache.rasterizations(),1u);
        cache.begin(); const auto b=cache.lookup("Hello",{96,32});
        QCOMPARE(a.image,b.image); QCOMPARE(cache.rasterizations(),0u);
        const auto changed=cache.lookup("World",{96,32});
        QVERIFY(changed.image->cacheKey()!=a.image->cacheKey());
        QCOMPARE(a.image->size(),QSize(96,32));
    }
    void richTextAndAlignment() {
        shell::MapTextCache cache; cache.begin();
        const auto left=cache.lookup("<span style='font-size:12px;color:#ff0000;vertical-align:top'>Hi</span>",{100,60});
        const auto right=cache.lookup("<span style='font-size:12px;color:#ff0000;text-align:right;vertical-align:bottom'>Hi</span>",{100,60});
        const auto center=cache.lookup("<span style='text-align:center;vertical-align:middle'>Hi</span>",{100,60});
        QVERIFY(left.image); QVERIFY(right.image); QVERIFY(center.image);
        const auto l=ink(*left.image), r=ink(*right.image), c=ink(*center.image);
        QCOMPARE(l.size(),r.size()); QVERIFY(r.left()>l.left()+50); QVERIFY(r.top()>l.top()+30);
        QVERIFY(c.left()>l.left()); QVERIFY(c.left()<r.left()); QVERIFY(c.top()>l.top()); QVERIFY(c.top()<r.top());
        for (int y=0;y<60;++y) for(int x=0;x<100;++x) {
            const auto pixel=left.image->pixelColor(x,y);
            if(pixel.alpha()) { QCOMPARE(pixel.red(),255); QCOMPARE(pixel.green(),0); QCOMPARE(pixel.blue(),0); }
        }
        for (const auto& html : {"<b>bold</b> <i>italic</i>","<font face='Arial' size='3' color='blue'>font</font>",
                 "A<br>B", "A<br/>B", "&lt;b&gt; &amp; &#65;", "<u>underline</u>", "<p>one</p><p>two</p>"}) {
            const auto result=cache.lookup(html,{100,60});
            QCOMPARE(result.status,shell::MapTextCache::Status::Ready); QVERIFY(!ink(*result.image).isEmpty());
        }
    }
    void boundsAndRefusals() {
        shell::MapTextCache cache;
        for (const auto& html : {"<img src='file:///secret'>", "<img src='https://example.com/a'>", "<a href='x'>link</a>",
                 "<style>span {color:red}</style>", "<span class='x[y]'>x</span>", "<span style='text-shadow:1px 1px red'>x</span>",
                 "<span style='font-size:99999px'>x</span>", "<span style='background:url(file:///x)'>x</span>",
                 "<span style='color:bogus'>x</span>", "<span style='color:#12345678'>x</span>", "<b>x</i>", "<b", "<script>x</script>",
                 "<span style='text-align:right'>one</span>two"}) {
            cache.begin(); QCOMPARE(cache.lookup(html,{96,32}).status,shell::MapTextCache::Status::Ready);
        }
        cache.begin();
        for (QSize size : {QSize(0,32),QSize(-1,32),QSize(32,0),QSize(1025,1),QSize(1024,1024)})
            QCOMPARE(cache.lookup("x",size).status,shell::MapTextCache::Status::Unsupported);
        QCOMPARE(cache.lookup(QString(16385,'x'),{32,32}).status,shell::MapTextCache::Status::Unsupported);
        const auto clipped=cache.lookup("A<br>B<br>C<br>D",{8,8}); QVERIFY(clipped.image); QCOMPARE(clipped.image->size(),QSize(8,8));
    }
    void budgetRetriesAndPinnedCache() {
        shell::MapTextCache cache; cache.begin();
        for(int i=0;i<16;++i) QCOMPARE(cache.lookup(QString::number(i),{32,32}).status,shell::MapTextCache::Status::Ready);
        QCOMPARE(cache.lookup("later",{32,32}).status,shell::MapTextCache::Status::Deferred);
        cache.begin(); QCOMPARE(cache.lookup("later",{32,32}).status,shell::MapTextCache::Status::Ready);
        std::vector<std::shared_ptr<const QImage>> pinned;
        shell::MapTextCache full;
        for(int i=0;i<128;++i) {
            full.begin(); auto result=full.lookup(QString::number(i),{32,32});
            QCOMPARE(result.status,shell::MapTextCache::Status::Ready); pinned.push_back(result.image);
        }
        full.begin(); QCOMPARE(full.lookup("extra",{32,32}).status,shell::MapTextCache::Status::Deferred);
        pinned.clear(); full.begin(); QCOMPARE(full.lookup("extra",{32,32}).status,shell::MapTextCache::Status::Ready);
    }
    void workerThread() {
        auto future=std::async(std::launch::async,[] {
            shell::MapTextCache cache; cache.begin(); return cache.lookup("<b>worker</b>",{96,32});
        });
        auto result=future.get(); QVERIFY(result.image); QVERIFY(!ink(*result.image).isEmpty());
    }
    void scenePlacementInvalidationAndImmutability() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame=terrain.prepare(w,{},0,false);
        auto prepare=[&] {return scene.prepare(w,frame,{},0,&stats);};
        const auto first=prepare(); QCOMPARE(first.size(),1); QCOMPARE(stats.maptextRasters,uint64_t(1));
        QCOMPARE(first[0].position,first[0].iconBottom-QPoint(0,32)); QCOMPARE(first[0].targetIdentity,uint64_t(123));
        QVERIFY(first[0].maptext); QVERIFY(first[0].mouseTransparent);
        // Stock mouse-opacity=1 maptext-only glyphs do not extend the icon hit mask.
        for(int y=0;y<96;++y) for(int x=0;x<96;++x) {
            const auto hit=shell::mapPointerAt({double(x),double(y)},{96,96},{3,3},frame.source,frame.geometry,"map",true,first);
            QVERIFY(hit); QVERIFY(hit->click.over.ref!=42);
        }
        w.atoms[42].pixelX=1; const auto step=prepare(); QVERIFY(step[0].mouseTransparent);
        QCOMPARE(step[0].image.cacheKey(),first[0].image.cacheKey()); w.atoms[42].pixelX=0;
        const auto again=prepare(); QVERIFY(first==again); QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.maptextRasters,uint64_t(0));
        setFields(w.appearances[1], fields("Hello",96,32,-7,9));
        const auto moved=prepare(); QCOMPARE(moved.size(),1); QCOMPARE(moved[0].position,first[0].position+QPoint(-7,-9));
        QCOMPARE(moved[0].image.cacheKey(),first[0].image.cacheKey()); QCOMPARE(stats.maptextRasters,uint64_t(0));
        setFields(w.appearances[1], fields("Changed",80,48,2,-3));
        const auto changed=prepare(); QCOMPARE(changed.size(),1); QCOMPARE(changed[0].rect().size(),QSize(80,48));
        QCOMPARE(changed[0].position,first[0].iconBottom+QPoint(2,-45));
        QVERIFY(changed[0].image.cacheKey()!=first[0].image.cacheKey()); QCOMPARE(first[0].image.size(),QSize(96,32));
        setFields(w.appearances[1], fields("")); QVERIFY(prepare().empty());
        setFields(w.appearances[1], fields("Hello")); w.atoms[42].x=1;
        QVERIFY(prepare().empty()); // the text box is outside the view
        setFields(w.appearances[1], fields("Hello",96,32,160)); QCOMPARE(prepare().size(),1);
    }
    void opacityTwoTextBoxSurvivesZeroAlpha() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        const auto visible=scene.prepare(w,frame,{}); QCOMPARE(visible.size(),1);
        auto f=fields("Hello"); f.pop_back();
        setFields(w.appearances[1], f+R"(,"appearanceFlags":4096,"hasColor":true,"colorA":0})");
        const auto invisible=scene.prepare(w,frame,{}); QCOMPARE(invisible.size(),1);
        QCOMPARE(invisible[0].image.cacheKey(),visible[0].image.cacheKey());
        QCOMPARE(invisible[0].tint[3],0.f); QCOMPARE(invisible[0].mouseOpacity,2);
        const auto hit=shell::mapPointerAt({90,33},{96,96},{3,3},frame.source,frame.geometry,"map",true,invisible);
        QVERIFY(hit); QCOMPARE(hit->click.over.ref,42u); // empty portion of the text box, not glyph ink
        QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    }
    void defaultsTurfAndOverlayOrder() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        setFields(w.appearances[1], fields("Hello",0,0));
        const auto frame=terrain.prepare(w,{},0,false);
        auto s=scene.prepare(w,frame,{}); QCOMPARE(s.size(),1); QCOMPARE(s[0].image.size(),QSize(32,32));
        const auto bounds=ink(s[0].image); QVERIFY2(bounds.top()>16, qPrintable(QString("ink %1,%2 %3x%4").arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height()))); // stock defaults to bottom alignment
        w.tiles[{5,5,0}]=1; w.atoms.clear();
        s=scene.prepare(w,frame,{},0,nullptr,true); QCOMPARE(s.size(),1); QCOMPARE(s[0].kind,shell::MapSprite::Turf);
        w=world(); w.resources={{123,0}};
        w.appearances[1].fields.iconResID=0;
        auto json=QJsonDocument::fromJson(QByteArray::fromStdString(fields("Hello",32,32))).object();
        json["overlays"]=QJsonArray{2}; setFields(w.appearances[1], QJsonDocument(json).toJson().toStdString());
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], "{}");
        auto read=[](uint32_t,QByteArray& out){QImage im(32,32,QImage::Format_RGBA8888);im.fill(Qt::red);out=iconfixture::png(im);return true;};
        s=scene.prepare(w,frame,read); QCOMPARE(s.size(),3);
        QVERIFY(!s[0].maptext); QVERIFY(s[1].maptext); QVERIFY(!s[2].maptext);
        json.remove("overlays"); json["maptext"]="<img src='file:///never-load'>";
        setFields(w.appearances[1], QJsonDocument(json).toJson().toStdString());
        s=scene.prepare(w,frame,read); QCOMPARE(s.size(),2); QVERIFY(s[1].maptext); // an empty box; the image is never loaded
        QCOMPARE(ink(s[1].image),QRect());
    }
    void stylesheetCascadeAndNestedLayout() {
        shell::MapTextCache cache;
        const shell::MapTextStyle style{
            "img.icon { width:auto; height:auto } span { font-family:Arial } .caption { color:red; font-size:8pt; line-height:1.0; -dm-text-outline:1px black } span.caption { color:lime } .unused { unknown:decl }", "Arial", 8};
        QVERIFY(cache.setStyle(style)); cache.begin();
        const QString text="<span class='caption'><span style='text-align:center;vertical-align:middle'>Station<br />2 players<br />04:00 in<br /></span></span>";
        auto result=cache.lookup(text,{80,75}); QCOMPARE(result.status,shell::MapTextCache::Status::Ready);
        int green=0, black=0;
        for(int y=0;y<75;++y) for(int x=0;x<80;++x) {
            const auto pixel=result.image->pixelColor(x,y);
            if(pixel.alpha() && pixel.green()>100) ++green;
            if(pixel.alpha() && pixel.red()==0 && pixel.green()==0 && pixel.blue()==0) ++black;
        }
        QVERIFY(green>10); QVERIFY(black>10);
        const auto bounds=ink(*result.image); QVERIFY(bounds.left()>0); QVERIFY(bounds.top()>0); QVERIFY(bounds.bottom()<74);
        QVERIFY(!cache.setStyle(style)); cache.begin(); QCOMPARE(cache.lookup(text,{80,75}).image,result.image); QCOMPARE(cache.rasterizations(),0u);
        auto changed=style; changed.sheet += " span.caption { color:red }"; QVERIFY(cache.setStyle(changed));
        const auto red=cache.lookup(text,{80,75}); QVERIFY(red.image); QVERIFY(red.image->cacheKey()!=result.image->cacheKey());
        const auto explicitColor=cache.lookup("<span class='caption' style='color:cyan'>X</span>",{80,75}); QVERIFY(explicitColor.image);
        bool cyan=false;
        for(int y=0;y<75;++y) for(int x=0;x<80;++x) {
            const auto p=explicitColor.image->pixelColor(x,y); if(p.alpha() && p.green()>200 && p.blue()>200) cyan=true;
        }
        QVERIFY(cyan);
        QCOMPARE(cache.lookup("<span class='unused'>X</span>",{80,75}).status,shell::MapTextCache::Status::Ready);
        const auto siblings=cache.lookup("<span class='caption'><span style='text-align:center;vertical-align:middle;font-size:18px'>Soon</span><br /><span style='text-align:center;vertical-align:middle'>2 players</span></span>",{80,75});
        QVERIFY(siblings.image);
        QVERIFY(cache.setStyle({".a .b { color:red }",{},0}));
        QCOMPARE(cache.lookup("X",{80,75}).status,shell::MapTextCache::Status::Ready);
    }
    void rasterPreservesInheritedSize() {
        shell::MapTextCache cache;
        cache.setStyle({".caption {font-family:Arial;font-size:6pt}","Arial",6}); cache.begin();
        const auto nested=cache.lookup("<span class='caption'><span style='text-align:center;vertical-align:middle'>Runtime Station</span></span>",{80,75});
        const auto direct=cache.lookup("<span style='font-family:Arial;font-size:6pt;text-align:center;vertical-align:middle'>Runtime Station</span>",{80,75});
        QVERIFY(nested.image); QVERIFY(direct.image); QCOMPARE(*nested.image,*direct.image);
        QVERIFY(ink(*nested.image).height()<12); // one small line, not a wrapped document-default font
    }
    void rasterFallbackAndColors() {
        shell::MapTextCache cache; cache.begin();
        const auto requested=cache.lookup("<span style='font-family:Missing Fixture Font 123;font-size:12pt'>X0123</span>",{96,64});
        const auto arial=cache.lookup("<span style='font-family:Arial;font-size:12pt'>X0123</span>",{96,64});
        QVERIFY(requested.image); QVERIFY(arial.image);
#ifdef Q_OS_WIN
        QCOMPARE(*requested.image,*arial.image); // no game/font-name special case
#endif
        const auto mixed=cache.lookup("<b style='color:red'>RED </b><i style='color:blue'>BLUE</i><u style='color:transparent'>hidden</u>",{160,32});
        QVERIFY(mixed.image); int red=0, blue=0;
        for(int y=0;y<32;++y) for(int x=0;x<160;++x) {
            const auto pixel=mixed.image->pixelColor(x,y);
            if(!pixel.alpha()) continue;
            QCOMPARE(pixel.green(),0); red+=pixel.red()>100; blue+=pixel.blue()>100;
        }
        QVERIFY(red>10); QVERIFY(blue>10);
        const auto hidden=cache.lookup("<span style='color:transparent'>hidden</span>",{96,64});
        QVERIFY(hidden.image); QVERIFY(ink(*hidden.image).isEmpty());
    }
    void inlineFontOverridesControlDefault() {
        shell::MapTextCache cache; cache.setStyle({{},"Arial",6}); cache.begin();
        const auto small=cache.lookup("<span style='font-size:12px'>X</span>",{96,64});
        const auto large=cache.lookup("<span style='font-size:24px'>X</span>",{96,64});
        QVERIFY(small.image); QVERIFY(large.image);
        const auto points=cache.lookup("<span style='font-size:24pt'>X</span>",{96,64}); QVERIFY(points.image);
        QCOMPARE(*large.image,*points.image);
        const auto s=ink(*small.image), l=ink(*large.image);
        QVERIFY2(l.height()>s.height()*1.5,qPrintable(QString("small=%1 large=%2").arg(s.height()).arg(l.height())));
    }
    void outlineAndLineHeight() {
        shell::MapTextCache cache; cache.begin();
        const auto plain=cache.lookup("<span style='font-size:12px'>X</span>",{64,64}); QVERIFY(plain.image);
        const auto outline=cache.lookup("<span style='font-size:12px;-dm-text-outline:1px black'>X</span>",{64,64}); QVERIFY(outline.image);
        const auto p=ink(*plain.image), o=ink(*outline.image);
        // The outline reaches past the box, as stock's does: the image is the box with `pad` px round it.
        QCOMPARE(outline.pad,1); QCOMPARE(outline.image->size(),QSize(66,66)); QCOMPARE(o.translated(-1,-1),p.adjusted(-1,-1,1,1));
        const auto normal=cache.lookup("<span style='font-size:12px;line-height:1.0;vertical-align:top'>X<br>X</span>",{64,64});
        const auto spaced=cache.lookup("<span style='font-size:12px;line-height:1.5;vertical-align:top'>X<br>X</span>",{64,64});
        QVERIFY(normal.image); QVERIFY(spaced.image); QVERIFY(ink(*spaced.image).height()>ink(*normal.image).height());
        QCOMPARE(cache.lookup("<span style='-dm-text-outline:2px black'>X</span>",{64,64}).status,shell::MapTextCache::Status::Ready);
        QCOMPARE(cache.lookup("<span style='line-height:99'>X</span>",{64,64}).status,shell::MapTextCache::Status::Ready);
    }
    void liveSkinStyleAppliesOnTextChange() {
        skin::LiveSkin live;
        QVERIFY(live.load("window \"main\"\n elem \"main\"\n  type = MAIN\n elem \"map\"\n  type = MAP\n  is-default = true\n  font-family = \"Arial\"\n  font-size = 8\n  style = \".caption { color:red }\"\n").empty());
        auto settings=live.defaultMapTextSettings(); QCOMPARE(settings.family,std::string("Arial")); QCOMPARE(settings.size,std::string("8"));
        auto w=world(); setFields(w.appearances[1], fields("<span class='caption'>Test</span>"));
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame=terrain.prepare(w,{},0,false);
        auto apply=[&] { settings=live.defaultMapTextSettings(); scene.setMapTextStyle({QString::fromStdString(settings.style),QString::fromStdString(settings.family),QString::fromStdString(settings.size).toDouble()}); };
        apply(); const auto before=scene.prepare(w,frame,{},0,&stats); QCOMPARE(before.size(),1);
        apply(); QVERIFY(before==scene.prepare(w,frame,{},0,&stats)); QCOMPARE(stats.templateBuilds,uint64_t(0));
        live.winset("main.map","style=\".caption { color:lime }\""); apply();
        QVERIFY(before==scene.prepare(w,frame,{},0,&stats)); QCOMPARE(stats.maptextRasters,uint64_t(0));
        setFields(w.appearances[1], fields("<span class='caption'>Test</span> "));
        const auto after=scene.prepare(w,frame,{},0,&stats); QCOMPARE(after.size(),1); QCOMPARE(stats.maptextRasters,uint64_t(1));
        QVERIFY(after[0].image!=before[0].image); QCOMPARE(after[0].position,before[0].position);
        QVERIFY(live.load("window \"main\"\n elem \"main\"\n  type = MAIN\n").empty());
        settings=live.defaultMapTextSettings(); QVERIFY(settings.style.empty()); QVERIFY(settings.family.empty());
    }
    void liveSkinDefaultColor_data() {
        QTest::addColumn<QString>("fileColor"); QTest::addColumn<QString>("winset");
        QTest::addColumn<QString>("css"); QTest::addColumn<QString>("html");
        QTest::addColumn<QColor>("expected");
        auto row=[](const char* name, QString file, QString winset, QString css, QString html, QColor color) {
            QTest::newRow(name) << file << winset << css << html << color;
        };
        row("file", "#ff0000", "", "", "Test", Qt::red);
        row("winset", "#0000ff", "text-color=#ff0000", "", "Test", Qt::red);
        row("body-css", "#ff0000", "", "body {color:lime}", "Test", Qt::green);
        row("class-css", "#ff0000", "", ".caption {color:blue}", "<span class='caption'>Test</span>", Qt::blue);
        row("inline-css", "#ff0000", "", "body {color:lime}", "<span style='color:blue'>Test</span>", Qt::blue);
        row("html-font", "#ff0000", "", "", "<font color='blue'>Test</font>", Qt::blue);
        row("file-empty", "", "", "", "Test", Qt::white);
        row("file-none", "none", "", "", "Test", Qt::white);
        // Empty winset colors have an established stock value, unlike file-empty/none.
        row("winset-empty", "#ff0000", "text-color=", "", "Test", QColor("#b47229"));
        row("winset-none", "#ff0000", "text-color=NoNe", "", "Test", Qt::white);
        row("none-with-css", "none", "", "body {color:blue}", "Test", Qt::blue);
    }
    void liveSkinDefaultColor() {
        QFETCH(QString,fileColor); QFETCH(QString,winset); QFETCH(QString,css);
        QFETCH(QString,html); QFETCH(QColor,expected);
        skin::LiveSkin live;
        const auto dmf=QString("window \"main\"\n elem \"main\"\n  type = MAIN\n elem \"map\"\n  type = MAP\n  is-default = true\n  text-color = \"%1\"\n  style = \"%2\"\n").arg(fileColor,css);
        QVERIFY(live.load(dmf.toStdString()).empty());
        if (!winset.isEmpty()) live.winset("main.map",winset.toStdString());
        std::string reported; QVERIFY(live.winget("main.map","text-color",reported));
        QCOMPARE(QString::fromStdString(reported), winset.isEmpty() ? fileColor :
            winset == "text-color=" ? QString("#b47229") : winset.mid(11));
        auto w=world(); setFields(w.appearances[1], fields(html));
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        const auto settings=live.defaultMapTextSettings();
        scene.setMapTextStyle({QString::fromStdString(settings.style),QString::fromStdString(settings.family),QString::fromStdString(settings.size).toDouble()});
        const auto sprites=scene.prepare(w,frame,{}); QCOMPARE(sprites.size(),1);
        const auto& image=sprites[0].image; QVERIFY(!ink(image).isEmpty());
        int samples=0;
        for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
            const auto p=image.pixelColor(x,y);
            // Unpremultiplication can round non-primary colors at antialiased edges.
            if (p.alpha() >= 128) {
                ++samples;
                QVERIFY(std::abs(p.red()-expected.red())<=1);
                QVERIFY(std::abs(p.green()-expected.green())<=1);
                QVERIFY(std::abs(p.blue()-expected.blue())<=1);
            }
        }
        QVERIFY(samples>0);
    }
    void liveSkinColorChangesWaitForAppearance() {
        skin::LiveSkin live;
        QVERIFY(live.load("window \"main\"\n elem \"main\"\n  type = MAIN\n elem \"map\"\n  type = MAP\n  is-default = true\n  text-color = #ff0000\n").empty());
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame=terrain.prepare(w,{},0,false);
        auto prepare=[&] {
            const auto s=live.defaultMapTextSettings();
            scene.setMapTextStyle({QString::fromStdString(s.style),QString::fromStdString(s.family),QString::fromStdString(s.size).toDouble()});
            return scene.prepare(w,frame,{},0,&stats);
        };
        const auto before=prepare(); QCOMPARE(before.size(),1);
        live.winset("main.map","text-color=#0000ff");
        QVERIFY(before==prepare()); QCOMPARE(stats.maptextRasters,uint64_t(0));
        setFields(w.appearances[1], fields("Changed"));
        const auto after=prepare(); QCOMPARE(after.size(),1); QCOMPARE(stats.maptextRasters,uint64_t(1));
        QVERIFY(!ink(after[0].image).isEmpty());
        for (int y=0;y<after[0].image.height();++y) for (int x=0;x<after[0].image.width();++x) {
            const auto p=after[0].image.pixelColor(x,y);
            if (p.alpha()) { QCOMPARE(p.red(),0); QCOMPARE(p.green(),0); QCOMPARE(p.blue(),255); }
        }
    }
    void transformedTextTurnsAboutIcon() {
        // Maptext takes its atom's matrix, pivoting on the icon's center rather
        // than the text box's: a 96x32 box turned 90 degrees becomes a 32-wide
        // column over the icon, not a box rotated in place.
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        const auto flat=scene.prepare(w,frame,{}); QCOMPARE(flat.size(),1); QVERIFY(!flat[0].affine);
        const QPoint iconTop=flat[0].iconBottom-QPoint(0,32);
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},R"(,"maptext":"Hello","maptextW":96,"maptextH":32)"));
        const auto turned=scene.prepare(w,frame,{}); QCOMPARE(turned.size(),1);
        // The matrix is on the surface the text is drawn in, which smooths it premultiplied (stock 516.1689).
        QVERIFY(turned[0].maptext); QVERIFY(!turned[0].affine); QVERIFY(turned[0].appearanceGroup);
        const QPoint pivot=turned[0].groupOrigin;
        const QRect r=turned[0].appearanceGroup->transform.mapRect(QRectF(turned[0].rect().translated(-pivot))).toAlignedRect().translated(pivot);
        QCOMPARE(r.size(),QSize(32,96)); QCOMPARE(r.left(),iconTop.x());
        QVERIFY(r.contains(iconTop+QPoint(16,16)));
        QCOMPARE(turned[0].image.cacheKey(),flat[0].image.cacheKey()); // same raster, transformed at draw time
        // Inside a turned tree (child maptext) the text turns too.
        w.appearances[2]=w.appearances[1]; setFields(w.appearances[2], fields("Hello"));
        setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},R"(,"overlays":[2])"));
        const auto tree=scene.prepare(w,frame,{}); QCOMPARE(tree.size(),1); QVERIFY(tree[0].maptext);
        QVERIFY(tree[0].appearanceGroup && !tree[0].appearanceGroup->transform.isIdentity());
    }
    void inheritedEffectsStayExcluded() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.appearances[2]=w.appearances[1];
        const auto frame=terrain.prepare(w,{},0,false);
        setFields(w.appearances[1], R"({"overlays":[2]})");
        QCOMPARE(scene.prepare(w,frame,{}).size(),1);
        setFields(w.appearances[1], R"({"filters":[{"type":99,"flags":1}],"overlays":[2]})");
        QVERIFY(scene.prepare(w,frame,{}).empty());
        setFields(w.appearances[1], R"({"hasColor":true,"colorR":0,"colorG":0,"colorB":255,"colorA":128,"overlays":[2]})");
        const auto tinted=scene.prepare(w,frame,{}); QCOMPARE(tinted.size(),1);
        QCOMPARE(tinted[0].tint[0],0.f); QCOMPARE(tinted[0].tint[2],1.f); QCOMPARE(tinted[0].tint[3],128.f/255);
    }
    // Stock filters an atom's maptext with its icon as one shape (tests/data/filter-reach-world maptext): the text
    // joins the icon's filter surface instead of vanishing.
    void filteredTextJoinsIconSurface() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/filter-reach-world/stock.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject stock;
        for (const auto& c:QJsonDocument::fromJson(file.readAll()).object()["sets"].toObject()["a"].toArray())
            if (c.toObject()["case"].toString()=="maptext") stock=c.toObject();
        QVERIFY(stock["redAtText"].toInt()>20); // the outline reaches the text
        auto w=world(); w.resources={{123,0}}; w.appearances[1].fields.iconResID=0;
        auto json=QJsonDocument::fromJson(QByteArray::fromStdString(fields("Ab",32,32))).object();
        json["filters"]=QJsonArray{QJsonObject{{"type",2},{"paramMask",35},{"flags",1},{"params","0000803fff0000ff0000"}}};
        setFields(w.appearances[1], QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
        auto read=[](uint32_t,QByteArray& out){QImage im(32,32,QImage::Format_RGBA8888);im.fill(Qt::red);out=iconfixture::png(im);return true;};
        shell::TurfMap terrain; shell::EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        const auto s=scene.prepare(w,frame,read); QCOMPARE(s.size(),2);
        QVERIFY(!s[0].maptext); QVERIFY(s[1].maptext);
        QVERIFY(s[0].appearanceGroup && s[0].appearanceGroup->effects);
        QCOMPARE(s[1].appearanceGroup,s[0].appearanceGroup);
    }
    void deferredSceneRetries() {
        auto w=world(); w.atoms.clear(); w.appearances.clear();
        for(uint32_t i=1;i<=20;++i) {
            auto& ap=w.appearances[i]; ap.fields.iconResID=UINT32_MAX; setFields(ap, fields(QString::number(i)));
            auto& atom=w.atoms[i]; atom.appid=i; atom.hasPos=true; atom.x=atom.y=6;
        }
        shell::TurfMap terrain; shell::EntityMap scene; shell::EntityMap::Stats stats;
        const auto frame=terrain.prepare(w,{},0,false);
        const auto first=scene.prepare(w,frame,{},0,&stats); QCOMPARE(first.size(),16); QCOMPARE(stats.maptextRasters,uint64_t(16));
        const auto complete=scene.prepare(w,frame,{},0,&stats); QCOMPARE(complete.size(),20); QCOMPARE(stats.maptextRasters,uint64_t(4));
        QVERIFY(complete==scene.prepare(w,frame,{},0,&stats)); QCOMPARE(stats.maptextRasters,uint64_t(0));
    }
    void sharedWorldAndHud() {
        auto w=world(); shell::TurfMap terrain; shell::EntityMap scene;
        w.atoms[43]=w.atoms[42]; w.atoms[43].x=7;
        w.appearances[2]=w.appearances[1];
        auto json=QJsonDocument::fromJson(QByteArray::fromStdString(fields("Hello"))).object(); json["screenLoc"]="CENTER";
        setFields(w.appearances[2], QJsonDocument(json).toJson().toStdString());
        w.atoms[44]=w.atoms[42]; w.atoms[44].appid=2; w.atoms[44].hasPos=false;
        w.hudOrder.push_back(44); w.hudAppid[44]=2;
        const auto frame=terrain.prepare(w,{},0,false);
        shell::EntityMap::Stats stats;
        const auto sprites=scene.prepare(w,frame,{},0,&stats,true);
        QCOMPARE(sprites.size(),3); QCOMPARE(stats.maptextRasters,uint64_t(1));
        for(const auto& sprite:sprites) QCOMPARE(sprite.image.cacheKey(),sprites[0].image.cacheKey());
        QCOMPARE(sprites.back().kind,shell::MapSprite::Screen);
    }
};
FUGUE_TEST_MAIN(MaptextTest)
#include "maptext_test.moc"
