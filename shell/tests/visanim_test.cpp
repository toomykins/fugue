// Retained vis_contents wire with synthetic phase clocks: playback starts, state
// changes, shared holders and list ordering. Expected colours are historical
// source assertions, not retained stock output; see the fixture README.
#include <QtTest>
#include "tests/testmain.h"
#include <set>
#include "shell/tests/animationfixture.h"
#include "shell/tests/replayfixture.h"
using namespace fugue;
namespace {
enum Phase { Initial, Attached, IconStateChanged, HolderChanged, SecondHolder };
// Test arrival clocks, not measured server/client timings. PHASE markers identify the wire boundaries.
constexpr int phaseTicks[] = {0,100,160,220,280};
}
class VisAnim : public QObject {
    Q_OBJECT
    struct Frame { uint16_t type; Bytes body; int phase; };
    std::vector<Frame> frames_;
    int64_t originNs_ = 0, tickNs_ = 0;
    QSize viewport_;
    int64_t at(int phase, int ticks = 0) const { return originNs_+(phaseTicks[phase]+ticks)*tickNs_; }
    bool load(shell::test::ReplayFixture& replay, Phase checkpoint) const {
        for (const auto& frame:frames_) {
            if (frame.phase>checkpoint) break;
            replay.ingest(frame.type,frame.body,at(frame.phase));
        }
        return replay.loadResources("visanim-world");
    }
    QImage map(shell::test::ReplayFixture& replay,int64_t clock) const {
        replay.refresh(clock);
        const auto image=AnimationFixture::raster(replay.frame,replay.sprites);
        if (image.size()!=viewport_) {
            QTest::qFail("Replay did not produce the retained viewport",__FILE__,__LINE__);
            return {};
        }
        return image;
    }
    static uint32_t tile(const QImage& f,int x,int y) {
        if(f.width()!=288 || f.height()!=288) return 0;
        return f.pixelColor((x-1)*32+16,f.height()-y*32+16).rgb()&0xffffff;
    }
private slots:
    void initTestCase() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/visanim-world/manifest.json");
        QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(file.fileName()));
        QJsonParseError parseError;
        const auto manifest=QJsonDocument::fromJson(file.readAll(),&parseError).object();
        QCOMPARE(parseError.error,QJsonParseError::NoError);
        originNs_=manifest["clock_unix_ns"].toInteger(); tickNs_=manifest["tick_ns"].toInteger();
        QVERIFY(originNs_>0 && tickNs_>0);
        const auto viewport=manifest["viewport"].toObject();
        viewport_={viewport["w"].toInt(),viewport["h"].toInt()};
        QVERIFY(viewport_.width()>0 && viewport_.height()>0);
        const int expectedFrames=manifest["frames"].toInt(), expectedResources=manifest["resources"].toInt();
        QVERIFY(expectedFrames>0 && expectedResources>0);
        shell::test::ReplayFixture recording;
        int phase=Initial;
        bool ordered=true;
        QVERIFY2(recording.readFrames("frames.txt","visanim-world",[&](uint16_t type,const Bytes& body) {
            if (type==S2C::Text) {
                const auto text=QByteArray(reinterpret_cast<const char*>(body.data()),qsizetype(body.size()))
                    .split('\0').front().trimmed();
                if (text.startsWith("PHASE ")) {
                    bool valid=false; const int next=text.mid(6).toInt(&valid);
                    ordered=ordered && valid && next==phase+1 && next<=SecondHolder;
                    phase=next;
                }
            }
            frames_.push_back({type,body,phase});
        }),qPrintable(recording.error));
        QVERIFY2(ordered && phase==SecondHolder,"Expected ordered PHASE 1 through PHASE 4 markers");
        QCOMPARE(int(frames_.size()),expectedFrames);
        QVERIFY2(recording.loadResources("visanim-world"),qPrintable(recording.error));
        QCOMPARE(recording.resources.size(),size_t(expectedResources));
        const auto resourceNames=manifest["resource_names"].toObject();
        QCOMPARE(resourceNames.size(),expectedResources);
        int64_t bytes=0;
        for (auto entry=resourceNames.begin();entry!=resourceNames.end();++entry) {
            bool valid=false; const uint32_t id=entry.key().toUInt(&valid,16);
            QVERIFY(valid && !entry.value().toString().isEmpty());
            QVERIFY2(recording.resources.count(id),qPrintable("Missing resource: "+entry.key()));
            const auto& resource=recording.resources.at(id); QVERIFY(!resource.isEmpty());
            bytes+=resource.size();
        }
        QCOMPARE(bytes,manifest["resource_bytes"].toInteger());
    }
    void finiteStatePlaysFromFirstSent() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,Attached),qPrintable(replay.error));
        const uint32_t want[]={0xff0000,0x00ff00,0x0000ff,0x0000ff};
        const int ticks[]={5,15,25,55};
        for(int k=0;k<4;++k) {
            const auto f=map(replay,at(Attached,ticks[k])); QCOMPARE(f.size(),QSize(288,288));
            QCOMPARE(tile(f,4,5),want[k]); QCOMPARE(tile(f,5,6),want[k]); QCOMPARE(tile(f,6,5),0xffffffu);
        }
    }
    void iconStateChangeRestarts() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,IconStateChanged),qPrintable(replay.error));
        const uint32_t want[]={0xff0000,0x00ff00,0x0000ff};
        for(int k=0;k<3;++k) {
            const auto f=map(replay,at(IconStateChanged,5+10*k));
            QCOMPARE(tile(f,6,5),want[k]); QCOMPARE(tile(f,4,5),0x0000ffu);
        }
    }
    void holderAndSecondHolderDoNotRestart() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,HolderChanged),qPrintable(replay.error));
        auto f=map(replay,at(HolderChanged,5)); QCOMPARE(tile(f,5,6),0x0000ffu); QCOMPARE(tile(f,4,7),0x0000ffu);
        shell::test::ReplayFixture second; QVERIFY2(load(second,SecondHolder),qPrintable(second.error));
        f=map(second,at(SecondHolder,5));
        QCOMPARE(tile(f,6,7),0x0000ffu); QCOMPARE(tile(f,4,7),0x0000ffu);
    }
    void loopingStateOnTheSharedClock() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,Attached),qPrintable(replay.error));
        std::set<uint32_t> seen;
        for(int k=0;k<3;++k) {
            const auto f=map(replay,at(Attached,3+7*k));
            QCOMPARE(tile(f,7,3),tile(f,3,3)); seen.insert(tile(f,7,3));
        }
        QCOMPARE(seen.size(),size_t(3));
    }
    void listOrderStacks() {
        shell::test::ReplayFixture replay; QVERIFY2(load(replay,Attached),qPrintable(replay.error));
        const auto f=map(replay,at(Attached,5));
        QCOMPARE(tile(f,8,5),0x8000ffu); QCOMPARE(tile(f,8,6),0xff8000u);
    }
};
FUGUE_TEST_APPLESS_MAIN(VisAnim)
#include "visanim_test.moc"
