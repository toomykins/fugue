// animate(client) replay of retained DreamDaemon 516.1684 wire (tests/data/clientanim-world).
// Each frame arrives at its preceding "@N" tick; the named checks below own their sample times and expected offsets.
// Those numeric offsets are historical source-encoded expectations, not retained stock output. The fractional
// direct-set checks separately test recorded wire facts and Fugue's sampling/arrival-clock invariants. See the fixture README.
// stockDirectSet additionally consumes an independent DreamSeeker 516.1689 observation of the direct-set case.
#include <QtTest>
#include <QCryptographicHash>
#include "tests/testmain.h"
#include "model/world.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/animationfixture.h"

using namespace fugue;

namespace {
constexpr uint32_t kPlayer = 0xc82828, kMarker = 0x28c828, kHud = 0x2828dc;
}  // namespace

class ClientAnim : public QObject {
    Q_OBJECT
    struct Frame {
        uint16_t typ;
        Bytes body;
        int tick;
    };
    std::vector<Frame> frames_;
    std::unique_ptr<shell::test::ReplayFixture> replay_;
    size_t nextFrame_ = 0;
    double sampledTick_ = -1;
    int64_t originNs_ = 0, tickNs_ = 0;
    QSize viewport_;

    int64_t at(double tick) const { return originNs_ + int64_t(tick * double(tickNs_)); }
    void reset() {
        replay_ = std::make_unique<shell::test::ReplayFixture>();
        nextFrame_ = 0;
        sampledTick_ = -1;
    }
    QImage image(double tick) {
        if (!replay_ || tick < sampledTick_) reset();
        while (nextFrame_ < frames_.size() && frames_[nextFrame_].tick <= int(tick)) {
            const auto& frame = frames_[nextFrame_++];
            replay_->ingest(frame.typ, frame.body, at(frame.tick));
        }
        // The original fixture supplies this one static resource after wire ingest;
        // it does not record resource arrival times.
        if (!replay_->loadResources("clientanim-world")) {
            QTest::qFail(qPrintable(replay_->error), __FILE__, __LINE__);
            return {};
        }
        replay_->refresh(at(tick));
        sampledTick_ = tick;
        const QImage rendered = AnimationFixture::raster(replay_->frame, replay_->sprites);
        if (rendered.size() != viewport_) {
            const auto error = QString("tick %1: expected retained viewport %2x%3, got %4x%5")
                .arg(tick).arg(viewport_.width()).arg(viewport_.height()).arg(rendered.width()).arg(rendered.height());
            QTest::qFail(qPrintable(error), __FILE__, __LINE__);
            return {};
        }
        return rendered;
    }
    static uint32_t rgb(const QImage& img, int x, int y) { return img.pixel(x, y) & 0xffffff; }

    // The view offset at `tick`, read off where the player's tile is drawn: it sits at (160,160) unmoved, and the map
    // moves by -x across and +y down.
    QPoint offset(double tick) {
        QImage img = image(tick);
        for (int y = 0; y < img.height(); y++)
            for (int x = 0; x < img.width(); x++)
                if (rgb(img, x, y) == kPlayer) return {160 - x, y - 160};
        return {9999, 9999};
    }

private slots:
    void initTestCase() {
        QFile file(QString(FUGUE_TEST_DATA_DIR) + "/clientanim-world/manifest.json");
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        QJsonParseError parseError;
        const auto manifest = QJsonDocument::fromJson(file.readAll(), &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        originNs_ = manifest["clock_unix_ns"].toInteger();
        tickNs_ = manifest["tick_ns"].toInteger();
        QVERIFY(originNs_ > 0 && tickNs_ > 0);
        const auto viewport = manifest["viewport"].toObject();
        viewport_ = QSize(viewport["w"].toInt(), viewport["h"].toInt());
        QVERIFY(viewport_.width() > 0 && viewport_.height() > 0);
        const int expectedFrames = manifest["frames"].toInt();
        const int expectedResources = manifest["resources"].toInt();
        const auto resourceNames = manifest["resource_names"].toObject();
        QVERIFY(expectedFrames > 0 && expectedResources > 0);
        QCOMPARE(resourceNames.size(), expectedResources);

        shell::test::ReplayFixture recording;
        int tick = 0;
        bool ordered = true;
        QVERIFY2(recording.readFrames("frames.txt", "clientanim-world", [&](uint16_t type, const Bytes& body) {
            if (type == S2C::Text && body.size() > 1 && body[0] == '@') {
                bool valid = false;
                const int next = QByteArray(reinterpret_cast<const char*>(body.data() + 1), qsizetype(body.size() - 1))
                    .split('\0').front().toInt(&valid);
                ordered = ordered && valid && next >= tick;
                tick = next;
            }
            frames_.push_back({type, body, tick});
        }), qPrintable(recording.error));
        QVERIFY2(ordered, "Recorded tick markers must be valid and ordered");
        QCOMPARE(int(frames_.size()), expectedFrames);
        QVERIFY2(recording.loadResources("clientanim-world"), qPrintable(recording.error));
        QCOMPARE(recording.resources.size(), size_t(expectedResources));
        int64_t resourceBytes = 0;
        for (auto entry = resourceNames.begin(); entry != resourceNames.end(); ++entry) {
            bool valid = false;
            const uint32_t id = entry.key().toUInt(&valid, 16);
            QVERIFY(valid && !entry.value().toString().isEmpty());
            QVERIFY2(recording.resources.count(id), qPrintable("Missing resource: " + entry.key()));
            const auto& bytes = recording.resources.at(id);
            QVERIFY(!bytes.isEmpty());
            resourceBytes += bytes.size();
        }
        QCOMPARE(resourceBytes, manifest["resource_bytes"].toInteger());
        // Known-good unanimated control validates the adapter before chains.
        QCOMPARE(offset(30), QPoint(0, 0));
    }

    void init() { reset(); }

    // animate(pixel_x = 32, time = 20, PARALLEL) at 31, then animate(pixel_y = 32, time = 20, PARALLEL) at 36: the
    // frame at 36 re-sends the first step and adds the second with flags 0x4 and StepDelay 0, so y runs 36 .. 56
    // while x runs 31 .. 51.
    void parallelOverlap() {
        QCOMPARE(offset(30), QPoint(0, 0));
        QCOMPARE(offset(36), QPoint(8, 0));
        QCOMPARE(offset(41), QPoint(16, 8));
        QCOMPARE(offset(51), QPoint(32, 24));
        QCOMPARE(offset(56), QPoint(32, 32));
        QImage img = image(56);  // the marker past the east edge fills the revealed column; the HUD stays
        QCOMPARE(rgb(img, 340, 200), kMarker);
        QCOMPARE(rgb(img, 0, 320), kHud);
        QCOMPARE(offset(65), QPoint(32, 32));  // the Count = 0 frame at 60 clears the chain; 0x0007 still says 32, 32
        QCOMPARE(offset(66), QPoint(0, 0));
    }

    // vg's directional_recoil shape: a 4-tick SINE | EASE_OUT kick to (20, 8) at 76, then at 81 the return, sent as a
    // head step (flags 0x40000080, StepDelay 0) behind the re-sent kick. The return starts at 81, not where the kick
    // ended (80): the view holds (20, 8) until then.
    void kickThenReturn() {
        QCOMPARE(offset(78), QPoint(14, 5));
        QCOMPARE(offset(80), QPoint(20, 8));
        QCOMPARE(offset(81), QPoint(20, 8));
        QCOMPARE(offset(87), QPoint(15, 6));  // half way through 12 ticks of SINE | EASE_IN
        QCOMPARE(offset(93), QPoint(0, 0));
    }

    // A 2-tick kick at 227 and the return at 232: the return is the head step of the frame at 232 (StepDelay 0), so it
    // starts at 232, three ticks after the kick ended. A historical source note reports a stock measurement of
    // 0.49 s from kick to return; the underlying stock output was not retained with this fixture.
    void headStartsAtStepDelay() {
        QCOMPARE(offset(229), QPoint(20, 0));
        QCOMPARE(offset(232), QPoint(20, 0));
        QCOMPARE(offset(238), QPoint(15, 0));
        QCOMPARE(offset(244), QPoint(0, 0));
    }

    // animate(pixel_x = 32, time = 20) at 101; pixel_x = 6 at 106 (a 0x0007, no 0x00f0): the tween keeps its timing and
    // ends on 6, shifted by the -26 the set made.
    void directSetShiftsTween() {
        QCOMPARE(offset(105), QPoint(6, 0));
        // The retained WorldState changes pixel_x from 32 to 6 at tick 106.
        // Fractional sampling must not ingest that event early or change its arrival clock.
        QVERIFY(!image(105.5).isNull());
        QCOMPARE(replay_->session.world().viewPixelX, 32);
        QCOMPARE(replay_->session.ingestClockNs(), at(105));
        QCOMPARE(offset(106), QPoint(-18, 0));
        QCOMPARE(replay_->session.world().viewPixelX, 6);
        QVERIFY(!image(106.5).isNull());
        QCOMPARE(replay_->session.world().viewPixelX, 6);
        QCOMPARE(replay_->session.ingestClockNs(), at(106));
        QCOMPARE(offset(111), QPoint(-10, 0));
        QCOMPARE(offset(121), QPoint(6, 0));
        QCOMPARE(offset(130), QPoint(6, 0));
    }

    // The same tween with pixel_y = 6 set at 146: y moves at once, x carries on.
    void directSetOtherAxis() {
        QCOMPARE(offset(146), QPoint(8, 6));
        QCOMPARE(offset(151), QPoint(16, 6));
        QCOMPARE(offset(161), QPoint(32, 6));
    }

    // directional_recoil eight times, two ticks apart, from 181: each return (a head) starts from the kick before it
    // and each new kick (a parallel) adds 16 on top, so the view stays kicked between 16 and 30 until the last return
    // ends at 200.
    void recoilBurst() {
        QCOMPARE(offset(182), QPoint(16, 0));
        QCOMPARE(offset(185), QPoint(16, 0));
        QCOMPARE(offset(185.5), QPoint(27, 0));
        QCOMPARE(offset(186), QPoint(30, 0));
        QCOMPARE(offset(192), QPoint(30, 0));
        QCOMPARE(offset(198), QPoint(14, 0));
        QCOMPARE(offset(200), QPoint(0, 0));
    }

    void stockDirectSet() {
        const QString fixture="clientanim-directset-world/stock";
        const QString directory=QString(FUGUE_TEST_DATA_DIR)+'/'+fixture+'/';
        QFile observationsFile(directory+"observations.json");
        QVERIFY2(observationsFile.open(QIODevice::ReadOnly),qPrintable(observationsFile.fileName()));
        QJsonParseError parseError;
        const auto observations=QJsonDocument::fromJson(observationsFile.readAll(),&parseError).object();
        QCOMPARE(parseError.error,QJsonParseError::NoError);
        QFile captureFile(directory+"capture.json");
        QVERIFY2(captureFile.open(QIODevice::ReadOnly),qPrintable(captureFile.fileName()));
        const auto capture=QJsonDocument::fromJson(captureFile.readAll(),&parseError).object();
        QCOMPARE(parseError.error,QJsonParseError::NoError);
        QVERIFY(capture["complete"].toBool());
        QVERIFY(!observations["stock_version"].toString().isEmpty());
        QCOMPARE(observations["stock_version"].toString(),capture["dreamdaemon_version"].toString());
        const auto setup=observations["replay"].toObject();
        const auto origin=setup["origin_ns"].toInteger(), tickNs=setup["tick_ns"].toInteger();
        const int pixelsPerTick=setup["clock_pixels_per_tick"].toInt();
        QVERIFY(origin>0 && tickNs>0 && pixelsPerTick>0);
        const auto size=setup["viewport"].toArray(), player=setup["player_origin"].toArray();
        const QSize viewport(size[0].toInt(),size[1].toInt());
        const QPoint playerOrigin(player[0].toInt(),player[1].toInt());
        QCOMPARE(viewport,QSize(352,352)); QCOMPARE(playerOrigin,QPoint(160,160));
        const auto crop=observations["measurement"].toObject()["crop"].toArray();
        const QRect source(crop[0].toInt(),crop[1].toInt(),crop[2].toInt(),crop[3].toInt());
        QCOMPARE(source.size(),viewport);

        std::vector<Frame> frames;
        QMap<QString,int> actions;
        shell::test::ReplayFixture recording;
        int tick=0;
        bool ordered=true;
        QVERIFY2(recording.readFrames("frames.txt",fixture,[&](uint16_t type,const Bytes& body) {
            if(type==S2C::Text) {
                const auto text=QByteArray(reinterpret_cast<const char*>(body.data()),qsizetype(body.size()))
                    .split('\0').front().trimmed();
                if(text.startsWith('@')) {
                    bool valid=false; const int next=text.mid(1).toInt(&valid);
                    ordered=ordered && valid && next>=tick; tick=next;
                } else if(text.startsWith("CAPTURE ")) {
                    const QString name=QString::fromUtf8(text.mid(8));
                    ordered=ordered && !actions.contains(name); actions[name]=tick;
                }
            }
            frames.push_back({type,body,tick});
        }),qPrintable(recording.error));
        QVERIFY(ordered);
        QCOMPARE(int(frames.size()),setup["frames"].toInt());
        QCOMPARE(actions.keys(),(QStringList{"assigned","end","settled","start"}));
        const int start=actions["start"];
        QCOMPARE(actions["assigned"]-start,5); QCOMPARE(actions["settled"]-start,20); QCOMPARE(actions["end"]-start,30);

        // Both the retained PNG and replay are measured by colour; only stock supplies the expectations.
        const auto bounds=[](const QImage& image,uint32_t colour) {
            QRect result;
            for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
                if((image.pixel(x,y)&0xffffff)==colour) result|=QRect(x,y,1,1);
            return result;
        };
        const QMap<QString,uint32_t> phaseColours{{"running",0xdcb414},{"assigned",0xb414dc},{"settled",0x14dc50}};
        QStringList names;
        for(const auto& value:observations["checkpoints"].toArray()) {
            const auto checkpoint=value.toObject(); const auto name=checkpoint["name"].toString(); names.push_back(name);
            const auto imageName=checkpoint["image"].toString(), phase=checkpoint["phase"].toString();
            QVERIFY(phaseColours.contains(phase));
            QFile imageFile(directory+imageName);
            QVERIFY2(imageFile.open(QIODevice::ReadOnly),qPrintable(imageFile.fileName()));
            const auto bytes=imageFile.readAll();
            QCOMPARE(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(),checkpoint["sha256"].toString().toLatin1());
            const auto raw=QImage::fromData(bytes);
            int captures=0;
            for(const auto& entry:capture["frames"].toArray()) {
                const auto record=entry.toObject();
                if(record["subject"]!="stock" || record["image"]!=imageName) continue;
                const auto physical=record["geometry"].toObject()["client_size_physical"].toArray();
                QCOMPARE(raw.size(),QSize(physical[0].toInt(),physical[1].toInt())); ++captures;
            }
            QCOMPARE(captures,1); QVERIFY(raw.rect().contains(source));
            const auto stock=raw.copy(source);
            const auto stockPlayer=bounds(stock,kPlayer), stockClock=bounds(stock,0xffffff);
            QCOMPARE(stockPlayer.size(),QSize(32,32)); QCOMPARE(stockPlayer.y(),playerOrigin.y());
            QCOMPARE(stockClock.size(),QSize(2,6)); QCOMPARE(stockClock.y(),0);
            const int stockOffset=playerOrigin.x()-stockPlayer.x();
            const int stockTime=stockClock.x()-stockPlayer.x()+playerOrigin.x();
            QCOMPARE(stockOffset,checkpoint["offset_x"].toInt()); QCOMPARE(stockTime,checkpoint["clock_pixels"].toInt());
            const QRect phaseBox(0,viewport.height()-32,32,32);
            QCOMPARE(bounds(stock,phaseColours[phase]),phaseBox);
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{checkpoint["tolerance_px"]},1,0,1),
                     qPrintable(name+": tolerance_px must be an explicit integer pixel tolerance"));
            const int tolerance=checkpoint["tolerance_px"].toInt();
            QCOMPARE(tolerance,phase=="settled" ? 0 : 1);

            // A clock pixel spans 10 ms. Sample its midpoint, keeping wire arrivals on their recorded ticks.
            const double sample=start+(stockTime+0.5)/pixelsPerTick;
            QCOMPARE(phase,sample<actions["assigned"] ? QString("running") :
                sample<actions["settled"] ? QString("assigned") : QString("settled"));
            shell::test::ReplayFixture replay;
            for(const auto& frame:frames) {
                if(frame.tick>int(sample)) break;
                replay.ingest(frame.typ,frame.body,origin+frame.tick*tickNs);
            }
            QVERIFY2(replay.loadResources(fixture),qPrintable(replay.error));
            replay.refresh(origin+int64_t(sample*double(tickNs)));
            const auto rendered=AnimationFixture::raster(replay.frame,replay.sprites);
            QCOMPARE(rendered.size(),viewport);
            const auto actualPlayer=bounds(rendered,kPlayer), actualClock=bounds(rendered,0xffffff);
            QCOMPARE(actualPlayer.size(),stockPlayer.size()); QCOMPARE(actualPlayer.y(),stockPlayer.y());
            const int actualOffset=playerOrigin.x()-actualPlayer.x();
            QVERIFY2(std::abs(actualOffset-stockOffset)<=tolerance,qPrintable(name+": player offset differs from stock"));
            QCOMPARE(actualClock.size(),stockClock.size()); QCOMPARE(actualClock.y(),stockClock.y());
            const int actualTime=actualClock.x()-actualPlayer.x()+playerOrigin.x();
            QVERIFY2(std::abs(actualTime-stockTime)<=1,qPrintable(name+": replay clock differs from stock"));
            QCOMPARE(bounds(rendered,phaseColours[phase]),phaseBox);
        }
        QCOMPARE(names,(QStringList{"before-assignment","after-assignment","settled"}));
    }
};

FUGUE_TEST_MAIN(ClientAnim)
#include "clientanim_test.moc"
