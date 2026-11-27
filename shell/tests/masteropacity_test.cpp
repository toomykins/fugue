// BYOND 516.1688 master opacity: one scenario, three execution profiles.
#include <QtTest>
#include "tests/testmain.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/gpucomparison.h"
#include "shell/tests/backendcheck.h"
#include "shell/entityappearance.h"
#include "shell/mapwidget.h"
#include <algorithm>

using namespace fugue;
using namespace fugue::shell;
namespace {
struct MasterOpacityFixture : test::ReplayFixture {
    QString control;
    QJsonObject masterRefs;
    QSize view;
    static QString directory() {
        return QString(FUGUE_TEST_DATA_DIR)+"/master-opacity-world/stock/";
    }
    bool load(const QJsonObject& phase, const QJsonObject& setup) {
        control=setup["control"].toString();
        masterRefs=setup["masterRefs"].toObject();
        const auto tiles=setup["view"].toArray(), size=setup["sourceSize"].toArray();
        view={tiles[0].toInt(),tiles[1].toInt()};
        const auto checkpoint=phase["checkpoint"].toObject();
        if (!ReplayFixture::load(checkpoint["wire"].toString(),"master-opacity-world/stock",{},setup["sampleNs"].toInteger()))
            return false;
        if (frame.source.size()!=QSize(size[0].toInt(),size[1].toInt()) || frame.geometry.z!=0) {
            error="Unexpected source geometry; this retained scenario is on z=1"; return false;
        }
        int masters=0;
        for (const auto& [ref,appid]:session.world().hudAppid) {
            const auto appearance=session.world().appearances.find(appid);
            if (appearance==session.world().appearances.end()) continue;
            const auto decoded=decodeEntityAppearance(appearance->second.fields,frame.geometry.tile);
            if (!decoded.valid || !decoded.fields.master) continue;
            ++masters;
            if (QString("[0x%1]").arg(0x02000000u|ref,0,16)!=masterRefs["wire"].toString()) {
                error="Retained wire master does not match the declared client correspondence"; return false;
            }
            if (decoded.fields.mouseOpacity!=checkpoint["masterMouseOpacity"].toInt()) {
                error="Retained wire does not describe the expected master-opacity phase"; return false;
            }
        }
        if (masters!=1) { error="Expected exactly one retained HUD master"; return false; }
        return true;
    }
    MapPointer at(QPoint point, const MapSprites& scene, bool defaultMap = true) const {
        return *mapPointerAt(point,frame.source.size(),view,frame.source,frame.geometry,control,defaultMap,scene);
    }
    model::HitTarget target(const MapPointer& p) const {
        return model::targetFromHit(session.world(),p.click.over.kind,p.click.over.ref,
            p.screen ? 0 : int(p.click.loc.x)+1,p.screen ? 0 : int(p.click.loc.y)+1,p.screen);
    }
    QString targetRef(const MapPointer& p) const {
        // dmRefOf uses the player's z; load() bounds this fixture to its single z=1 map.
        const auto ref=QString("[0x%1]").arg(model::dmRefOf(session.world(),target(p).target),0,16);
        // Login creates a master per client: stock receipts and retained wire name different instances.
        return p.screen && ref==masterRefs["wire"].toString() ? masterRefs["stock"].toString() : ref;
    }
    QString checkClick(const MapPointer& p, const QJsonObject& expected) const {
        if (const auto mismatch=ReplayFixture::checkClick(p,expected); !mismatch.isEmpty()) return mismatch;
        if (targetRef(p)!=expected["targetRef"].toString())
            return "target reference: "+targetRef(p)+" expected "+expected["targetRef"].toString();
        if (!p.screen) {
            auto location=p;
            location.click.over=proto::enc::turfRef(int(p.click.loc.x)+1,int(p.click.loc.y)+1);
            if (QString::fromStdString(targetName(location))!=expected["location"].toString()) return "location name";
        }
        if (QString::fromStdString(p.click.control)!=expected["control"].toString()) return "control";
        QJsonObject params;
        for (const auto& field:QString::fromStdString(proto::enc::clickParams(p.click)).split(';')) {
            const auto equals=field.indexOf('=');
            if (equals>=0) params.insert(field.left(equals),field.mid(equals+1));
        }
        return params==expected["params"].toObject() ? QString() : "button, modifiers or encoded coordinates";
    }
    QString checkMenu(const MapPointer& p, const QJsonObject& action) const {
        const auto stack=mapPointerStack(p);
        QJsonArray names;
        for (const auto& entry:stack) {
            if (entry.masterFallback || !mapPointerCurrent(entry,session.world(),0)) return "invalid menu target";
            names.append(QString::fromStdString(targetName(entry)));
        }
        if (names!=action["menuStack"].toArray()) return "menu stack differs from retained popup transcription";
        const auto receipt=action["callbacks"].toArray().first().toObject();
        for (const auto& entry:stack) {
            if (targetRef(entry)!=receipt["targetRef"].toString()) continue;
            if (QString::fromStdString(targetName(entry))!=receipt["target"].toString()) return "selected menu target";
            const auto selected=target(entry);
            for (const auto& verb:model::atomMenu(session.world(),selected.appid,selected.target))
                if (QString::fromStdString(verb.verb.name)==action["menuVerb"].toString()) return {};
            return "recorded menu verb is not eligible for its target";
        }
        return "recorded menu target is absent from the production stack";
    }
};

// Both GPU readback and native presentation use the same exact stock pixels.
QString pixelError(const QImage& actual, const QJsonObject& checkpoint, QSize size, const QString& profile) {
    const QString phase=checkpoint["pixels"].toString();
    const QString expectedPath=MasterOpacityFixture::directory()+phase;
    const QImage expected(expectedPath);
    if (expected.size()!=size) return "Missing or invalid stock image: "+expectedPath;
    if (actual.size()!=expected.size()) return "Missing or incorrectly sized render for "+phase;
    int different=0;
    QPoint first;
    for (int y=0;y<expected.height();++y) for (int x=0;x<expected.width();++x) {
        if (actual.pixelColor(x,y)!=expected.pixelColor(x,y) && !different++) first={x,y};
    }
    if (!different) return {};
    const QString path=QDir::temp().filePath("fugue-master-opacity-"+profile+'-'+phase+"-actual.png");
    const bool saved=actual.save(path);
    return QString("%1/%2: %3 pixels differ, first at %4,%5; expected: %6; actual: %7")
        .arg(profile,phase).arg(different).arg(first.x()).arg(first.y()).arg(expectedPath,saved ? path : "could not save image");
}
}

class MasterOpacityTest : public QObject {
    Q_OBJECT
    QJsonArray phases_;
    QJsonObject setup_;
    QSize sourceSize_;
private slots:
    void initTestCase() {
        QFile file(MasterOpacityFixture::directory()+"result.json");
        QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(file.fileName()));
        QJsonParseError error;
        const auto record=QJsonDocument::fromJson(file.readAll(),&error).object();
        QCOMPARE(error.error,QJsonParseError::NoError);
        QVERIFY(record["complete"].toBool());
        const auto observation=record["observation"].toObject();
        QCOMPARE(observation["stockVersion"].toString(),QString("516.1688"));
        QCOMPARE(observation["platform"].toString(),QString("Windows"));
        QCOMPARE(observation["timing"].toString(),QString("not-retained"));
        QVERIFY(!observation["menuStackSource"].toString().isEmpty());
        setup_=record["replay"].toObject();
        QCOMPARE(setup_["mode"].toString(),QString("static-phase"));
        // This is our deterministic snapshot clock, not a stock capture timestamp.
        QVERIFY(setup_["sampleNs"].isDouble()); QCOMPARE(setup_["sampleNs"].toDouble(),0.0);
        QCOMPARE(setup_["control"].toString(),QString("main.map"));
        QCOMPARE(setup_["view"].toArray(),(QJsonArray{7,7}));
        QCOMPARE(setup_["sourceSize"].toArray(),(QJsonArray{224,224}));
        const auto size=setup_["sourceSize"].toArray(); sourceSize_={size[0].toInt(),size[1].toInt()};
        phases_=record["phases"].toArray();
        QStringList names;
        for (const auto& value:phases_) {
            const auto row=value.toObject(); names.push_back(row["case"].toString());
            const auto checkpoint=row["checkpoint"].toObject();
            QVERIFY(!checkpoint["wire"].toString().isEmpty());
            QCOMPARE(QImage(MasterOpacityFixture::directory()+checkpoint["pixels"].toString()).size(),sourceSize_);
            QVERIFY(checkpoint["masterMouseOpacity"].isDouble());
            QCOMPARE(checkpoint["masterMouseOpacity"].toInt(),row["case"]=="control" ? 1 : row["case"]=="two" ? 2 : 0);
            int clicks=0, menus=0;
            for (const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto point=action["point"].toArray();
                QCOMPARE(point.size(),2);
                for (int axis=0;axis<2;++axis) {
                    const auto coordinate=point[axis];
                    QVERIFY(coordinate.isDouble() && coordinate.toDouble()==coordinate.toInt());
                    QVERIFY(coordinate.toInt()>=0 && coordinate.toInt()<(axis ? sourceSize_.height() : sourceSize_.width()));
                }
                QStringList kinds;
                for (const auto& callback:action["callbacks"].toArray()) kinds.push_back(callback.toObject()["kind"].toString());
                if (action["kind"]=="click") {
                    QCOMPARE(kinds,(QStringList{"Down","Up","Click"})); ++clicks;
                } else {
                    QCOMPARE(action["kind"].toString(),QString("menu"));
                    QCOMPARE(kinds,(QStringList{"Menu"})); ++menus;
                    QVERIFY(!action["menuStack"].toArray().isEmpty());
                    for (const auto& name:action["menuStack"].toArray()) QVERIFY(!name.toString().isEmpty());
                    QCOMPARE(action["menuVerb"].toString(),QString("inspect"));
                }
            }
            QCOMPARE(clicks,5); QCOMPARE(menus,3);
        }
        QCOMPARE(names,(QStringList{"control","two","zero"}));
    }
    void portable() {
        int clicks=0, menus=0;
        for (const auto& value:phases_) {
            const auto row=value.toObject(); const auto name=row["case"].toString();
            MasterOpacityFixture f; QVERIFY2(f.load(row,setup_),qPrintable(f.error));
            for (const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto xy=action["point"].toArray();
                const QPoint point(xy[0].toInt(),xy[1].toInt());
                auto hit=f.at(point,f.sprites);
                QVERIFY(mapPointerCurrent(hit,f.session.world(),0));
                if (action["kind"]=="menu") {
                    const auto error=f.checkMenu(hit,action); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error)); ++menus;
                } else for (const auto& cv:action["callbacks"].toArray()) {
                    const auto c=cv.toObject();
                    hit.kind=c["kind"]=="Down" ? MapPointer::Down : c["kind"]=="Up" ? MapPointer::Up : MapPointer::Click;
                    const auto error=f.checkClick(hit,c); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error));
                    QVERIFY(mapPointerCurrent(hit,f.session.world(),0)); ++clicks;
                }
            }
        }
        QCOMPARE(clicks,45); QCOMPARE(menus,9);
    }
    void lifetimes() {
        MasterOpacityFixture f; QVERIFY2(f.load(phases_[1].toObject(),setup_),qPrintable(f.error));
        auto& w=f.session.worldMut(); const auto original=f.sprites;
        const auto hit=f.at({60,168},original); QVERIFY(hit.masterFallback && hit.screen);
        const auto ref=hit.click.over.ref; const auto id=w.hudAppid.at(ref);
        QVERIFY(mapPointerCurrent(hit,w,0));
        ++w.hudIdentity.at(ref).membership; QVERIFY(!mapPointerCurrent(hit,w,0)); --w.hudIdentity.at(ref).membership;
        ++w.hudIdentity.at(ref).key; QVERIFY(!mapPointerCurrent(hit,w,0)); --w.hudIdentity.at(ref).key;
        const auto hud=w.hud.at(ref); w.hud.erase(ref); QVERIFY(!mapPointerCurrent(hit,w,0)); w.hud[ref]=hud;
        QVERIFY(!mapPointerCurrent(hit,w,1));
        auto stale=hit; auto surface=std::make_shared<const uint8_t>(0); stale.surface=surface;
        QVERIFY(mapPointerCurrent(stale,w,0)); surface.reset(); QVERIFY(!mapPointerCurrent(stale,w,0));
        for (auto kind:{MapPointer::Hover,MapPointer::Drag,MapPointer::Drop,MapPointer::Wheel,MapPointer::DoubleClick}) {
            auto gesture=hit; gesture.kind=kind; QVERIFY(!mapPointerCurrent(gesture,w,0));
        }
        auto drag=f.at({40,168},original); drag.kind=MapPointer::Drag;
        drag.source=std::make_shared<MapPointer>(hit); QVERIFY(!mapPointerCurrent(drag,w,0));
        QVERIFY(!f.at({60,168},original,false).masterFallback);
        const auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(w.appearances.at(id)))).object();
        const auto saved=w.appearances.at(id);
        for (const QString key:{"filter","transform","target","source","tree","text","offset","placement","alpha","color","group","icon"}) {
            auto changed=fields;
            if(key=="filter") changed["filters"]=QJsonArray{QJsonObject{{"type",99},{"flags",1}}};
            if(key=="transform") {changed["hasTransform"]=true;changed["transform"]=QJsonArray{};}
            if(key=="target") changed["renderTarget"]="*plane";
            if(key=="source") changed["renderSource"]="*plane";
            if(key=="tree") changed["overlays"]=QJsonArray{int(id)};
            if(key=="text") changed["maptext"]="unsupported";
            if(key=="offset") changed["pixelX"]=7;
            if(key=="placement") changed["screenLoc"]="2,1";
            if(key=="alpha") {changed["hasColor"]=true;changed["colorA"]=128;}
            if(key=="color") {changed["hasColor"]=true;changed["colorR"]=128;}
            if(key=="group") changed["appearanceFlags"]=double(uint32_t(fields["appearanceFlags"].toDouble()) | 0x04000000u);
            w.appearances.at(id)=saved;
            if(key=="icon") w.appearances.at(id).fields.iconResID=1;
            setFields(w.appearances.at(id), QJsonDocument(changed).toJson(QJsonDocument::Compact).toStdString());
            f.refresh();
            QCOMPARE(f.at({60,168},f.sprites).masterFallback,key=="filter");
            QVERIFY(f.at({60,168},original).masterFallback); // published evidence stays immutable
        }
        w.appearances.at(id)=saved; f.refresh(); QVERIFY(f.at({60,168},f.sprites).masterFallback);
        // No attachment/affine/local relay member silently enables a plane fallback.
        for (int variant=0;variant<8;++variant) {
            auto scene=original;
            auto member=std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Entity && s.plane==1;});
            QVERIFY(member!=scene.end());
            if(variant==0) member->imageIdentity=1;
            if(variant==1) member->kind=MapSprite::Vis;
            if(variant==2) member->renderSource="*local";
            if(variant==3) member->maptext=true;
            if(variant==4) member->affine=StaticAffine::make({1,0,0,0,1,0},{32,32});
            if(variant==5) member->mouseOpacity=2;
            if(variant==6) member->tint[3]=.5f;
            if(variant==7) member->passMouse=true;
            QVERIFY(!f.at({60,168},scene).masterFallback);
        }
        // Last selected master wins; no borrowing from an earlier admitted master.
        auto scene=original; auto master=*std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        master.masterFallback=false; scene.push_back(master); QVERIFY(!f.at({60,168},scene).masterFallback);
        // Higher-plane ordinary ink remains above the fallback.
        scene=original; auto member=*std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Entity && s.plane==1;});
        member.plane=2; member.position+=QPoint(20,0); scene.push_back(member);
        QVERIFY(!f.at({60,168},scene).masterFallback);
        // Geometry translated with a nonzero rendered viewport still reports viewport coordinates.
        scene=original; const QPoint delta(64,96);
        for(auto& s:scene) {s.position+=delta;s.iconBottom+=delta;}
        const auto shifted=mapPointerAt(QPoint(60,168),f.frame.source.size(),f.view,f.frame.source.translated(delta),f.frame.geometry,f.control,true,scene);
        QVERIFY(shifted && shifted->masterFallback); QCOMPARE(shifted->click.iconX,61); QCOMPARE(shifted->click.iconY,56);
    }
    void gpu_data() {
        QTest::addColumn<QJsonObject>("phase");
        for (const auto& value:phases_) {
            const auto phase=value.toObject();
            QTest::newRow(qPrintable(phase["case"].toString())) << phase;
        }
    }
    void gpu() {
        QFETCH(QJsonObject,phase);
        MasterOpacityFixture f; QVERIFY2(f.load(phase,setup_),qPrintable(f.error));
        test::GpuComparison gpu;
        QVERIFY2(gpu.initialize(f.frame.source.size()),
                 "GPU profile requires a working QRhi device on the entry's backend");
        const auto error=pixelError(gpu.render(gpu.bounded,f.sprites,f.frame.source,Qt::black),
            phase["checkpoint"].toObject(),sourceSize_,"gpu");
        QVERIFY2(error.isEmpty(),qPrintable(error));
    }
    void native() {
        QVERIFY2(QGuiApplication::platformName()!="offscreen" && QGuiApplication::platformName()!="minimal",
                 "Native profile requires a window system and a working QRhi backend");
        const auto mismatch=test::backendMismatch(); QVERIFY2(mismatch.isEmpty(),qPrintable(mismatch));
        const bool openGL=test::backendIsOpenGL();
        MapWidget map;
        const auto view=setup_["view"].toArray();
        map.resize(sourceSize_*2); map.setViewSize({view[0].toInt(),view[1].toInt()});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler(setup_["control"].toString(),true,[&](const MapPointer& p){events.push_back(p);});
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        int receipts=0,menus=0;
        for(const auto& value:phases_) {
            const auto row=value.toObject(); const auto name=row["case"].toString();
            MasterOpacityFixture f; QVERIFY2(f.load(row,setup_),qPrintable(f.error));
            submitted.clear();
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            map.update();
            QTRY_VERIFY_WITH_TIMEOUT(!submitted.empty() && metrics->snapshot().pending==0,5000);
            const auto pixels=map.grabFramebuffer(); QVERIFY(!pixels.isNull());
            QImage sampled(sourceSize_,QImage::Format_RGBA8888);
            for(int y=0;y<sourceSize_.height();++y) for(int x=0;x<sourceSize_.width();++x)
                sampled.setPixelColor(x,y,pixels.pixelColor((2*x+1)*pixels.width()/map.width(),(2*y+1)*pixels.height()/map.height()));
            const auto pixelsError=pixelError(sampled,row["checkpoint"].toObject(),sourceSize_,openGL ? "native-opengl" : "native-default");
            QVERIFY2(pixelsError.isEmpty(),qPrintable(pixelsError));
            // A newer pending empty scene cannot replace the rendered master/menus.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            QCOMPARE(metrics->snapshot().pending,1u);
            for(const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto xy=action["point"].toArray();
                const QPoint point(xy[0].toInt(),xy[1].toInt());
                events.clear(); QTest::mouseClick(&map,action["kind"]=="menu" ? Qt::RightButton : Qt::LeftButton,Qt::NoModifier,point*2+QPoint(1,1));
                for (const auto& event:events) {
                    if (event.kind==MapPointer::Hover || event.kind==MapPointer::Leave) continue;
                    QCOMPARE(QString::fromStdString(event.click.control),setup_["control"].toString());
                    QCOMPARE(event.click.button,action["kind"]=="menu" ? std::string("right") : std::string("left"));
                    QVERIFY(!event.click.mods.ctrl && !event.click.mods.shift && !event.click.mods.alt);
                }
                QCOMPARE(metrics->snapshot().pending,1u);
                if(action["kind"]=="menu") {
                    const auto click=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                    QVERIFY(click!=events.end());
                    const auto error=f.checkMenu(*click,action); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error)); ++menus;
                } else {
                    int i=0;
                    for(const auto& event:events) {
                        if(event.kind==MapPointer::Hover || event.kind==MapPointer::Leave) continue;
                        const auto expected=action["callbacks"].toArray(); QVERIFY(i<expected.size());
                        const auto c=expected[i++].toObject();
                        QCOMPARE(event.kind,c["kind"]=="Down" ? MapPointer::Down : c["kind"]=="Up" ? MapPointer::Up : MapPointer::Click);
                        const auto error=f.checkClick(event,c); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error));
                        QVERIFY(mapPointerCurrent(event,f.session.world(),0)); ++receipts;
                    }
                    QCOMPARE(i,3);
                }
            }
            // Only an actually rendered replacement may retire the old target.
            QVERIFY(!map.grabFramebuffer().isNull()); events.clear();
            QCOMPARE(metrics->snapshot().pending,0u);
            QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{121,337});
            QVERIFY(!events.empty());
            QVERIFY(std::none_of(events.begin(),events.end(),[](const auto& p){return p.masterFallback;}));
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            QVERIFY(!map.grabFramebuffer().isNull());
            const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
            QVERIFY(!map.grabFramebuffer().isNull());
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        QCOMPARE(receipts,45); QCOMPARE(menus,9);
    }
};
FUGUE_TEST_MAIN(MasterOpacityTest)
#include "masteropacity_test.moc"
