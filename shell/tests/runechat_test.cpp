// Wire-backed runechat placement, fade, deletion and MeasureText regressions.
// Checkpoints use retained wire markers; clocks and expectations are historical
// test inputs/assertions, not new stock observations. See the fixture README.
// Qt glyph rasterization does not establish stock GDI glyph/metric parity.
#include <QtTest>
#include "tests/testmain.h"
#include <QFontDatabase>
#include <array>
#include "shell/tests/replayfixture.h"
#include "proto/messages.h"
#include "net/measuretext.h"
#include "net/textmacro.h"
using namespace fugue;
namespace {
// Synthetic replay clock: the recording does not retain frame arrival times.
constexpr int64_t t0=1'000'000'000'000'000'000, tick=100'000'000;
const QSize textSize{98,18}; // the runechat's 96x16 box and its 1px outline, which reaches a pixel past it on every side
enum Checkpoint { FadingIn, Visible, BubbleDeleted };
using Frames=std::vector<testinput::RecordedFrame>;
}
class Runechat : public QObject {
    Q_OBJECT
    Frames frames;
    std::array<size_t,3> checkpoints{};
    QString sheet;
    bool load(shell::test::ReplayFixture& replay,const Frames& input,Checkpoint checkpoint) const {
        for(size_t i=0;i<checkpoints[checkpoint];++i) replay.ingest(input[i].type,input[i].body,t0);
        replay.entities.setMapTextStyle({sheet,{},0});
        return replay.loadResources("runechat-world");
    }
    static shell::MapSprites scene(shell::test::ReplayFixture& replay,int64_t now) {
        replay.refresh(now);
        auto sprites=replay.sprites;
        const auto& region=replay.frame.geometry.region;
        // Translate to world coordinates so camera recentering is not the oracle.
        for(auto& sprite:sprites) sprite.position-=QPoint(-region.x()*32,(region.y()+region.height())*32);
        return sprites;
    }
    static const shell::MapSprite* plane(const shell::MapSprites& sprites,int p,QSize size) {
        for(const auto& sprite:sprites) if(sprite.plane==p && sprite.image.size()==size) return &sprite;
        return nullptr;
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()=="offscreen") {
            const auto fonts=qEnvironmentVariable("WINDIR")+"/Fonts"; qputenv("QT_QPA_FONTDIR",fonts.toUtf8());
            QVERIFY(QFontDatabase::addApplicationFont(fonts+"/arial.ttf")>=0);
        }
#endif
        shell::test::ReplayFixture recording;
        QVERIFY2(recording.loadResources("runechat-world"),qPrintable(recording.error));
        QCOMPARE(recording.resources.size(),size_t(3));
        for(uint32_t id:{0x4c895ab0u,0x5c894aeau,0x6075a274u}) {
            QVERIFY2(recording.resources.count(id),qPrintable(QString("Missing resource: %1").arg(id,8,16,QChar('0'))));
            QVERIFY(!recording.resources.at(id).isEmpty());
        }
        const auto& skin=recording.resources.at(0x6075a274); const auto at=skin.indexOf("style = \".center"); QVERIFY(at>=0);
        const auto first=skin.indexOf('"',at), last=skin.indexOf('"',first+1); QVERIFY(last>first+1);
        sheet=QString::fromUtf8(skin.mid(first+1,last-first-1));
        QVERIFY(sheet.contains(".maptext") && sheet.contains(".black_outline"));
        QVERIFY2(recording.readFrames("frames.txt","runechat-world",[&](uint16_t type,const Bytes& body) {
            frames.push_back({type,body});
        }),qPrintable(recording.error));
        QCOMPARE(frames.size(),size_t(89));
        int speech=0, animations=0, measureRequests=0;
        for(size_t i=0;i<frames.size();++i) {
            const auto& [type,body]=frames[i];
            if(type==S2C::Text) {
                const auto text=QString::fromStdString(proto::decodeText(body).text).trimmed();
                if(text.startsWith("SAY ")) {
                    bool valid=false; const int next=text.mid(4).toInt(&valid);
                    QVERIFY(valid); QCOMPARE(next,speech+1); speech=next;
                }
            }
            if(speech==1 && type==S2C::MeasureText) ++measureRequests;
            if(speech==1 && type==S2C::Animate) {
                const auto animation=proto::decodeAnimate(body);
                QCOMPARE(animation.kind,uint8_t(13)); QCOMPARE(animation.ref,uint32_t(1));
                if(animations==0) QVERIFY(!animation.steps.empty());
                else {
                    QCOMPARE(animations,1); QVERIFY(animation.steps.empty());
                    // Preserve all intervening records, including camera updates.
                    checkpoints[FadingIn]=i;
                    checkpoints[Visible]=i+1;
                }
                ++animations;
            }
            if(speech==2 && type==S2C::ImageDelete && !checkpoints[BubbleDeleted]) {
                QCOMPARE(proto::decodeImageDelete(body).imageID,uint32_t(0));
                checkpoints[BubbleDeleted]=i+1;
            }
        }
        QCOMPARE(speech,3); QCOMPARE(animations,2); QCOMPARE(measureRequests,1);
        QVERIFY(checkpoints[FadingIn]>0 && checkpoints[FadingIn]<checkpoints[Visible]);
        QVERIFY(checkpoints[Visible]<checkpoints[BubbleDeleted]);
    }
    void bubbleAndRunechatAreDrawn() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,frames,Visible),qPrintable(replay.error));
        QCOMPARE(replay.session.world().images.size(),size_t(2));
        const auto sprites=scene(replay,t0+10'000'000'000LL);
        const auto* bubble=plane(sprites,16,{32,32}); QVERIFY(bubble);
        QCOMPARE(bubble->position,QPoint(12*32,-10*32)); // known-good icon control
        const auto* text=plane(sprites,25,textSize); QVERIFY(text); QVERIFY(text->maptext); QVERIFY(text->mouseTransparent);
        QCOMPARE(text->position,bubble->position+QPoint(-32-1,32-30-16-1)); QCOMPARE(text->tint[3],1.f);
        int gold=0,black=0;
        for(int y=0;y<text->image.height();++y) for(int x=0;x<text->image.width();++x) { const auto c=text->image.pixelColor(x,y); if(!c.alpha()) continue;
            if(c.red()>200 && c.green()>150 && c.blue()<50) ++gold;
            if(c.red()==0 && c.green()==0 && c.blue()==0) ++black;
        }
        QVERIFY(gold>0); QVERIFY(black>0);
    }
    void runechatFadesInWithoutNewRasters() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,frames,FadingIn),qPrintable(replay.error));
        const auto before=scene(replay,t0+tick/2); const auto* hidden=plane(before,25,textSize); QVERIFY(!hidden || hidden->tint[3]==0);
        const auto mid=scene(replay,t0+2*tick); const auto* text=plane(mid,25,textSize); QVERIFY(text);
        QVERIFY(text->tint[3]>.39f && text->tint[3]<.63f);
        const auto later=scene(replay,t0+tick*5/2); const auto* next=plane(later,25,textSize); QVERIFY(next);
        QCOMPARE(text->image.cacheKey(),next->image.cacheKey()); QVERIFY(next->tint[3]>text->tint[3]);
    }
    void bubbleLeavesWithItsDelete() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,frames,BubbleDeleted),qPrintable(replay.error));
        QCOMPARE(replay.session.world().images.size(),size_t(1));
        const auto sprites=scene(replay,t0+10'000'000'000LL); QVERIFY(!plane(sprites,16,{32,32})); QVERIFY(plane(sprites,25,textSize));
    }
    void markerIsNotDrawn() {
        shell::test::ReplayFixture plain; QVERIFY2(load(plain,frames,Visible),qPrintable(plain.error));
        const auto a=scene(plain,t0+10'000'000'000LL);
        auto markedFrames=frames;
        const std::string hello="Hello there 1";
        int markers=0;
        for(auto& [type,body]:markedFrames) if(type==S2C::Appearance) {
            const auto at=std::search(body.begin(),body.end(),hello.begin(),hello.end());
            if(at!=body.end()) { body.insert(at,{0xff,0x16}); ++markers; }
        }
        QCOMPARE(markers,2); // the first message's alpha-zero and alpha-full appearances
        shell::test::ReplayFixture marked; QVERIFY2(load(marked,markedFrames,Visible),qPrintable(marked.error));
        const auto b=scene(marked,t0+10'000'000'000LL);
        const auto* x=plane(a,25,textSize); const auto* y=plane(b,25,textSize); QVERIFY(x && y); QCOMPARE(x->image,y->image);
    }
    void recordedMeasureRequestUsesDrawingLayout() {
        shell::MapTextCache cache; cache.setStyle({sheet,{},0});
        for(const auto& [type,body]:frames) if(type==S2C::MeasureText) {
            const auto reply=net::measureTextReply(body,[&](const std::string& text,const std::string& style,uint16_t width) {
                const auto size=cache.measure(QString::fromStdString(net::plainText(text)),QString::fromStdString(style),width);
                return std::pair<uint16_t,uint16_t>{uint16_t(size.width()),uint16_t(size.height())};
            });
            QVERIFY(reply); QCOMPARE(reply->size(),size_t(8)); QCOMPARE((*reply)[0],uint8_t(96)); QVERIFY((*reply)[2]>0 && (*reply)[2]<32);
            return;
        }
        QFAIL("missing historical MeasureText request");
    }
};
FUGUE_TEST_MAIN(Runechat)
#include "runechat_test.moc"
