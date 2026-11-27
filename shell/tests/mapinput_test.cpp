#include "tests/appearancejson.h"
#include <QtTest>
#include <QSet>
#include "tests/testmain.h"
#include "tests/recordedinput.h"
#include "shell/mapinput.h"
#include "shell/mapvisibility.h"
#include "shell/mappresentation.h"
#include "model/world.h"
#include "net/input.h"
#include "shell/entitymap.h"
#include "shell/namedmap.h"
#include "shell/tests/roottreefixture.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/hudrelayfixture.h"
#include "shell/tests/replayfixture.h"
#include "shell/compositionplan.h"
using namespace fugue;
using namespace fugue::shell;
class MapInputTest : public QObject {
    Q_OBJECT
    // One pick a second, and what it reports by the end of its tick.
    static Bytes hoverStep(MapHover& hover, const MapPointer& p, bool current, MapHover::Gate gate) {
        static int64_t now = 0;
        now += 1'000'000'000;
        hover.update(p, current, gate, now);
        return hover.due(now + MapHover::kPeriodNs);
    }
private slots:
    void popupReleaseRouting() {
        MapPointer p;
        for(const auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click,MapPointer::DoubleClick,
                            MapPointer::Drag,MapPointer::Drop,MapPointer::Wheel,MapPointer::Hover,MapPointer::Leave}) {
            p.kind=kind;
            for(const auto* button:{"left","middle","right"}) {
                p.click.button=button;
                QCOMPARE(mapPopupAction(p,false),MapPopupAction::None);
                const bool ordinary=p.click.button!="right" || kind==MapPointer::Wheel ||
                    kind==MapPointer::Hover || kind==MapPointer::Leave;
                QCOMPARE(mapPopupAction(p,true),ordinary ? MapPopupAction::None :
                    kind==MapPointer::Click ? MapPopupAction::Open : MapPopupAction::Consume);
            }
        }
        // Stock 516.1684, tests/data/popupmenu-world part A: the flag at the press and at the release.
        p=MapPointer{}; p.click.button="right";
        auto sequence=[&](bool atPress,bool atRelease) {
            QString out; bool sent=false;
            for(const auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click}) {
                p.kind=kind;
                const auto action=mapPopupAction(p,kind==MapPointer::Down ? atPress : atRelease,sent);
                if(kind==MapPointer::Down) sent=action==MapPopupAction::None;
                out+=action==MapPopupAction::None ? 'S' : action==MapPopupAction::Open ? 'M' : '-';
            }
            return out;
        };
        QCOMPARE(sequence(true,true),QString("--M"));   // menu, nothing sent
        QCOMPARE(sequence(false,false),QString("SSS")); // DOWN UP CLICK
        QCOMPARE(sequence(false,true),QString("SSS"));  // DOWN UP CLICK, no menu
        QCOMPARE(sequence(true,false),QString("-SS"));  // UP CLICK
        // Part C: the press that closes the verb menu is consumed whatever the flag; its release is a fresh one.
        p.closedPopup=true;
        QCOMPARE(sequence(false,false),QString("-SS"));
        QCOMPARE(sequence(false,true),QString("--M"));
        p=MapPointer{};
        // Menu-only rectangles have no alpha target: opening is only a routing
        // decision, not a replacement for worker candidate/lifetime validation.
        p.kind=MapPointer::Click; p.click.button="right"; p.backgroundHidden=true;
        QCOMPARE(mapPopupAction(p,true),MapPopupAction::Open);
    }
    void opacityAndRenderedStacks() {
        MapSprite bottom; bottom.image=QImage(32,32,QImage::Format_RGBA8888); bottom.image.fill(Qt::red);
        bottom.cell={1,1}; bottom.iconBottom={0,32}; bottom.atom=1; bottom.haveKey=true; bottom.key=9; bottom.targetIdentity=10;
        auto top=bottom; top.atom=2; top.targetIdentity=20; top.plane=1;
        top.image.fill(Qt::transparent);
        MapSprites sprites{bottom,top};
        auto pick=[&] { return *mapPointerAt({8,8},{32,32},{1,1},{0,0,32,32},
            {{0,0,1,1},{1,1},32,0,7},"main.map",true,sprites); };
        QCOMPARE(pick().click.over.ref,1u);
        QCOMPARE(mapPointerStack(pick()).size(),2); // rectangles, not source alpha
        sprites.back().color[19]=1; // compositing cannot turn a source-alpha hole into a hit
        QCOMPARE(pick().click.over.ref,1u);
        sprites.back().mouseOpacity=2;
        QCOMPARE(pick().click.over.ref,2u);
        sprites.back().mouseOpacity=1; sprites.back().image.fill(Qt::green);
        sprites.back().tint[3]=0; sprites.back().color={};
        QCOMPARE(pick().click.over.ref,2u); // alpha/tint/matrix ignored
        const auto retained=pick(); const auto stack=mapPointerStack(retained);
        sprites.back().mouseOpacity=0;
        QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),1);
        QCOMPARE(mapPointerStack(retained).size(),2); // immutable last-rendered values
        model::World w; w.bounds.maxX=w.bounds.maxY=1;
        for(uint32_t id:{1u,2u}) {auto& a=w.atoms[id];a.hasPos=true;a.haveKey=true;a.key=9;a.renderIdentity=id*10;}
        QVERIFY(mapPointerCurrent(stack[0],w,7));
        ++w.atoms[2].renderIdentity; QVERIFY(!mapPointerCurrent(stack[0],w,7));
        QVERIFY(mapPointerCurrent(stack[1],w,7)); QVERIFY(!mapPointerCurrent(stack[1],w,8));
        sprites={bottom,top,top}; QCOMPARE(mapPointerStack(pick()).size(),2); // overlays deduplicate
        sprites.back().position={64,0}; QCOMPARE(mapPointerStack(pick()).size(),2); // no union-box gap
        sprites={bottom,top}; sprites.back().footprint=true; sprites.back().image={}; sprites.back().size={32,32};
        QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),2);
        sprites.back()=top; sprites.back().kind=MapSprite::Image; sprites.back().screenInteractive=true;
        sprites.back().mouseOpacity=2; sprites.back().image.fill(Qt::red);
        QCOMPARE(pick().click.over.ref,1u); // unverified image-owned opacity-2 footprint refused
        QCOMPARE(mapPointerStack(pick()).size(),2); // menu rectangles are independently established
        sprites.back()=top; sprites.back().mouseOpacity=3;
        QCOMPARE(pick().click.over.ref,1u); // unknown packed mode is not guessed
        sprites.back()=top; sprites.back().maptext=true; sprites.back().mouseTransparent=true;
        QCOMPARE(pick().click.over.ref,1u);
        sprites.back().mouseOpacity=2; QCOMPARE(pick().click.over.ref,2u);
        sprites.back()=top; sprites.back().mouseOpacity=2;
        sprites.back().affine=StaticAffine::make({.8,-.6,0,.6,.8,0},{32,32});
        // Opacity 2 is the turned icon rectangle: a corner of its bounds falls through (stock 516.1689, click-world
        // set b flat-turn-opaque2).
        const auto corner=sprites.back().rect().topLeft()+QPoint(1,1);
        const auto box=mapPointerAt(corner+QPoint(16,16),{64,64},{2,2},{-16,-16,64,64},
            {{0,0,2,2},{2,2},32,0,7},"main.map",true,sprites);
        QVERIFY(box); QVERIFY(box->click.over.ref!=2u);
        sprites={bottom,top}; MapSprite master; master.kind=MapSprite::Master;master.plane=1;master.mouseOpacity=0;
        sprites.push_back(master); QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),1);
        master.mouseOpacity=1;master.tint[3]=0;sprites.push_back(master);
        QCOMPARE(mapPointerStack(pick()).size(),2); // last master wins; alpha is not opacity
        sprites.clear();
        for(uint32_t id=1;id<=200;++id) { auto s=bottom;s.atom=id;sprites.push_back(s); }
        QCOMPARE(mapPointerStack(pick()).size(),128);
        auto outside=pick();outside.insideScene=false;QVERIFY(mapPointerStack(outside).empty());
    }
    void affineCoverageAndStockCoordinates_data() {
        QTest::addColumn<bool>("mob");
        QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void affineCoverageAndStockCoordinates() {
        QFETCH(bool,mob);
        MapSprite sprite; sprite.image=QImage(32,32,QImage::Format_RGBA8888); sprite.image.fill(Qt::red);
        sprite.position={32,32}; sprite.iconBottom={32,64}; sprite.cell={2,2}; sprite.atom=42 | (mob ? model::kRegistryA : 0);
        sprite.targetIdentity=123; sprite.haveKey=true; sprite.key=9;
        auto hit=[&](QPoint p) { return mapPointerAt(QPointF(p),{96,96},{3,3},{0,0,96,96},
            {QRect(0,0,3,3),QSize(3,3),32,0,1},"main.map",true,{sprite}); };
        sprite.affine=StaticAffine::make({.8,-.6,0,.6,.8,0},{32,32}); QVERIFY(sprite.affine);
        const QRect bounds=sprite.rect();
        for(QPoint corner:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()}) {
            const auto p=hit(corner); QVERIFY(p); QCOMPARE(p->click.over.kind,uint8_t(proto::enc::RefTurf));
        }
        QCOMPARE(hit({48,48})->click.over.ref,uint32_t(42));
        sprite.image.setPixelColor(16,16,Qt::transparent);
        QCOMPARE(hit({48,48})->click.over.kind,uint8_t(proto::enc::RefTurf));
        sprite.image.fill(Qt::red);
        struct Case { std::array<double,6> m; QPoint pixel, icon, vis; bool hasVis; };
        // Integer pixel corners reconstructed from the stock fixture screen-loc.
        const Case cases[] = {
            {{0,-1,0,1,0,0},{9,24},{9,23},{10,8},true},
            {{0,1,0,-1,0,0},{11,7},{8,11},{12,25},true}, // paired stock mob/object lying rotation
            {{1,0,9,0,1,5},{16,6},{8,21},{8,21},false},
            {{-1,0,0,0,1,0},{25,9},{8,23},{26,23},true},
            {{1,.5,0,0,1,0},{7,16},{8,16},{16,16},true},
            {{1,.5,0,0,1,0},{7,15},{7,17},{16,17},true}
        };
        for(const auto& item:cases) {
            sprite.affine=StaticAffine::make(item.m,{32,32}); const auto p=hit(sprite.position+item.pixel); QVERIFY(p);
            QCOMPARE(p->click.over.kind,uint8_t(mob ? proto::enc::RefMob : proto::enc::RefObj));
            QCOMPARE(p->click.over.ref,uint32_t(42)); QCOMPARE(p->click.iconX,item.icon.x()); QCOMPARE(p->click.iconY,item.icon.y());
            QCOMPARE(p->click.hasVis,item.hasVis);
            if(item.hasVis) { QCOMPARE(p->click.visX,item.vis.x()); QCOMPARE(p->click.visY,item.vis.y()); }
            QCOMPARE(p->targetIdentity,uint64_t(123)); QCOMPARE(p->atomKey,uint16_t(9));
            model::World w; auto& atom=w.atoms[sprite.atom]; atom.hasPos=true; atom.z=0;
            atom.renderIdentity=123; atom.haveKey=true; atom.key=9;
            QVERIFY(mapPointerCurrent(*p,w,1));
            ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(*p,w,1)); // recycled registry slot
            atom.renderIdentity=123; w.resync.freed.insert(sprite.atom); QVERIFY(!mapPointerCurrent(*p,w,1));
        }
    }
    void imageAffineStockCoordinates_data() {
        QTest::addColumn<bool>("mob"); QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void imageAffineStockCoordinates() {
        QFETCH(bool,mob); const uint32_t target=42u | (mob ? model::kRegistryA : 0u);
        MapSprite image; image.kind=MapSprite::Image; image.screenInteractive=true;
        image.image=QImage(32,32,QImage::Format_RGBA8888); image.image.fill(Qt::transparent);
        for(int y=3;y<9;++y) for(int x=23;x<29;++x) image.image.setPixelColor(x,y,Qt::yellow);
        image.position={32+7+25,32-5-9}; image.iconBottom=image.position+QPoint(0,32);
        image.cell={2,2}; image.atom=target; image.haveKey=true; image.key=9;
        image.imageId=7; image.imageIdentity=20; image.targetIdentity=10;
        MapGeometry geometry{{0,0,4,4},{4,4},32,0,1};
        geometry.visibility=std::make_shared<const VisibilityFrame>();
        auto hit=[&](QPoint p){return *mapPointerAt(image.position+p,{128,128},{4,4},{0,0,128,128},
            geometry,"main.map",true,{image});};
        struct Case {std::array<double,6> m; QPoint pixel,icon,vis; bool hasVis;};
        const Case cases[]={
            {{1,0,0,0,1,0},{25,5},{26,27},{26,27},false},
            {{0,-1,0,1,0,0},{5,5},{28,27},{6,27},true},
            {{.5,0,0,0,.5,0},{20,10},{25,28},{13,14},true},
            {{1,0,9,0,1,5},{34,0},{26,27},{26,27},false},
            {{0,-1,9,1,0,5},{14,0},{28,27},{6,27},true},
            {{.5,0,9,0,.5,5},{29,5},{25,28},{13,14},true}
        };
        model::World w; auto& atom=w.atoms[target]; atom.hasPos=true; atom.haveKey=true; atom.key=9; atom.renderIdentity=10;
        w.atoms[target^model::kRegistryA]=atom; // even identical wire keys/tokens cannot alias registries
        w.imageInstances[7]={2,target,uint8_t(mob?2:1),false,20,10};
        for(const auto& c:cases) {
            image.affine=StaticAffine::make(c.m,{32,32}); image.smooth=c.m[0]==.5;
            const auto p=hit(c.pixel);
            QCOMPARE(p.click.over.kind,uint8_t(mob?proto::enc::RefMob:proto::enc::RefObj)); QCOMPARE(p.click.over.ref,42u);
            QCOMPARE(p.click.loc.kind,uint8_t(proto::enc::RefTurf));
            QCOMPARE(p.click.iconX,c.icon.x()); QCOMPARE(p.click.iconY,c.icon.y());
            QCOMPARE(p.click.hasVis,c.hasVis);
            if(c.hasVis){QCOMPARE(p.click.visX,c.vis.x());QCOMPARE(p.click.visY,c.vis.y());}
            QVERIFY(mapPointerCurrent(p,w,1));
            auto forged=p; forged.click.over.kind=mob?proto::enc::RefObj:proto::enc::RefMob;
            QVERIFY(!mapPointerCurrent(forged,w,1));
            forged.click.over.kind=proto::enc::RefTurf; QVERIFY(!mapPointerCurrent(forged,w,1));
            auto drag=p; drag.kind=MapPointer::Drag; drag.source=std::make_shared<const MapPointer>(p);
            // Stock 516.1689 drags and drops a client image's atom like any other (plane-click-world gestures.json).
            QVERIFY(mapPointerCurrent(drag,w,1)); drag.kind=MapPointer::Drop; QVERIFY(mapPointerCurrent(drag,w,1));
            ++w.imageInstances[7].identity; QVERIFY(!mapPointerCurrent(p,w,1)); --w.imageInstances[7].identity;
            ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(p,w,1)); --atom.renderIdentity;
            const auto transparent=image.affine->forward.map(QPointF(16,16)).toPoint();
            QVERIFY(hit(transparent).backgroundHidden); // no phantom-turf fallback
            image.renderTarget="*captured"; QVERIFY(hit(c.pixel).backgroundHidden); image.renderTarget.clear();
        }
        image.affine=StaticAffine::make({.5,0,0,0,.5,0},{32,32}); image.smooth=true;
        QVERIFY(hit({19,9}).backgroundHidden); QVERIFY(hit({19,10}).backgroundHidden);
        QCOMPARE(hit({22,12}).click.iconX,29); QCOMPARE(hit({22,12}).click.iconY,24);
        QCOMPARE(hit({22,10}).click.iconY,28);
    }
    void ordinaryRootTreePicking() {
        auto w=roottreefixture::world(true,true); roottreefixture::pose(w,{0,-1,9,1,0,5});
        TurfMap terrain; EntityMap scene; auto frame=terrain.prepare(w,{},0,false);
        auto sprites=scene.prepare(w,frame,roottreefixture::read,1,nullptr,true); QCOMPARE(sprites.size(),4);
        frame.geometry.session=7; frame.geometry.visibility=std::make_shared<const VisibilityFrame>();
        const QPoint origin=sprites[1].position-frame.source.topLeft();
        auto hit=[&](QPoint p){return *mapPointerAt(origin+p,{160,160},{5,5},frame.source,frame.geometry,"main.map",true,sprites);};
        // Child-only and nested pixels report ROOT coordinates, including negatives.
        const auto child=hit({60,4}); QCOMPARE(child.click.over.kind,uint8_t(proto::enc::RefMob));
        QCOMPARE(child.click.iconX,24); QCOMPARE(child.click.iconY,-19);
        QCOMPARE(child.click.visX,52); QCOMPARE(child.click.visY,23); QVERIFY(mapPointerCurrent(child,w,7));
        const auto nested=hit({58,15}); QCOMPARE(nested.click.iconX,13); QCOMPARE(nested.click.iconY,-17);
        // Captured stock corner-alpha boundary: adjacent visible pixels need not hit.
        QVERIFY(hit({21,12}).backgroundHidden); QVERIFY(hit({0,0}).backgroundHidden);
        const auto underlay=hit({21,13}); QCOMPARE(underlay.click.over.kind,uint8_t(proto::enc::RefMob));
        for(auto& s:sprites) s.renderTarget="*capture";
        QVERIFY(hit({60,4}).backgroundHidden); // no phantom turf behind captured tree
        for(auto& s:sprites) {s.renderTarget.clear();s.mouseTransparent=true;}
        QVERIFY(hit({60,4}).backgroundHidden);
        ++w.atoms.begin()->second.renderIdentity; QVERIFY(!mapPointerCurrent(child,w,7));
        --w.atoms.begin()->second.renderIdentity; QVERIFY(!mapPointerCurrent(child,w,8));
        w.resync.freed.insert(w.atoms.begin()->first); QVERIFY(!mapPointerCurrent(child,w,7));
    }
    void stockHudAnchorsAndPassMouse() {
        QJsonArray rows; QString inventoryError;
        QVERIFY2(test::HudRelayFixture::rows(rows,inventoryError),qPrintable(inventoryError));QCOMPARE(rows.size(),12);
        for(const auto& value:rows) {
            const auto row=value.toObject();test::HudRelayFixture f(row["phase"].toString());
            for(const auto& v:row["clicks"].toArray()) {
                const auto expected=v.toObject();const auto xy=expected["point"].toArray();
                const auto hit=f.at({xy[0].toInt(),xy[1].toInt()});
                const auto error=test::HudRelayFixture::check(hit,expected);
                QVERIFY2(error.isEmpty(),qPrintable(row["phase"].toString()+": "+error));
                QVERIFY(mapPointerCurrent(hit,f.world,17));
            }
            EntityMap::Stats stats;const auto old=f.sprites;f.refresh(&stats);
            QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(old.constData(),f.sprites.constData());
        }
    }
    void stockWideMousePackets() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/hud-relay-world/wide-mouse.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto rows=document.object()["bodies"].toObject();
        QCOMPARE(rows.keys(),(QStringList{"03","04","05","06","08","drag","drop","wheel"}));
        std::map<QString,Bytes> expected;
        for (auto it=rows.begin();it!=rows.end();++it) {
            auto body=hexDecode(it.value().toString().toStdString());
            QVERIFY2(it.value().isString() && body && !body->empty(),qPrintable(path+": invalid hexadecimal body for "+it.key()));
            expected[it.key()]=std::move(*body);
        }
        auto check=[&](const Bytes& narrow,const QString& name) {
            QCOMPARE(proto::enc::mouseWireBody(narrow,true),expected.at(name));
            QCOMPARE(proto::enc::mouseWireBody(narrow,false),narrow); // negotiated mode, not ID magnitude
        };
        MapPointer p;p.click.over=proto::enc::objRef(0);p.click.control="main.map";
        p.click.iconX=4;p.click.iconY=18;p.click.screenX=3;p.click.screenY=5;p.click.screenPX=p.click.screenPY=16;
        for(auto [kind,name]:std::initializer_list<std::pair<MapPointer::Kind,const char*>>{
            {MapPointer::Click,"03"},{MapPointer::Down,"06"},{MapPointer::Up,"05"},{MapPointer::Hover,"08"}}) {
            p.kind=kind;check(mapPointerBody(p),name);
        }
        p.kind=MapPointer::DoubleClick;p.click.over=p.click.loc=proto::enc::turfRef(7,2);
        p.click.iconX=p.click.screenPX=9;p.click.iconY=p.click.screenPY=28;p.click.screenX=7;p.click.screenY=2;
        check(mapPointerBody(p),"04");
        proto::enc::WheelCmd wheel;wheel.over=p.click.over;wheel.dy=120;wheel.control="main.map";
        wheel.params="icon-x=9;icon-y=28;screen-loc=map:7:9,2:28";check(proto::enc::encodeWheelCmd(wheel),"wheel");
        proto::enc::DragCmd drag;drag.src=drag.srcLoc=p.click.over;drag.over=drag.overLoc=proto::enc::turfRef(6,2);
        drag.srcControl=drag.overControl="main.map";
        drag.params="icon-x=21;icon-y=18;left=1;button=left;screen-loc=6:21,2:18";
        check(proto::enc::encodeDragCmd(drag),"drag");
        drag.drop=true;drag.over=drag.overLoc=proto::enc::nullRef();drag.overControlNull=true;
        drag.params="icon-x=9;icon-y=28;left=1;button=left;screen-loc=7:9,2:28";
        check(proto::enc::encodeDragCmd(drag),"drop");
        QVERIFY(proto::enc::mouseWireBody({},true).empty());
        QVERIFY(proto::enc::mouseWireBody(proto::enc::encodeVerb(3,0,0,{},true),true).empty());
    }
    void hudPassMouseLifetimeAndBounds() {
        test::HudRelayFixture f("chain");const auto hit=f.at({79,16});
        QVERIFY(hit.planeMousePath);QCOMPARE(hit.planeMousePath->hops.size(),size_t(2));
        QCOMPARE(hit.click.over.ref,1u);QVERIFY(!hit.click.hasVis);
        for(const auto& hop:hit.planeMousePath->hops) {
            QVERIFY(hop.master.renderedScene.empty());QVERIFY(!hop.master.composition);
            QVERIFY(hop.relay.renderedScene.empty());QVERIFY(!hop.relay.composition);
            QVERIFY(!hop.master.planeMousePath);QVERIFY(!hop.relay.planeMousePath);
        }
        QVERIFY(mapPointerCurrent(hit,f.world,17));QVERIFY(!mapPointerCurrent(hit,f.world,18));
        for(uint32_t id:{1,2,3,4,5}) {
            ++f.world.hudIdentity[id].membership;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].membership;
            ++f.world.hudIdentity[id].key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].key;
            const auto appid=f.world.hudAppid[id];f.world.hudAppid.erase(id);
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
            f.world.appearances[99]=f.world.appearances[appid];f.world.hudAppid[id]=99;
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
        }
        auto hover=hit;hover.kind=MapPointer::Hover;
        QVERIFY(mapPointerCurrent(hover,f.world,17));MapHover state;
        QVERIFY(!hoverStep(state,hover,true,{true}).empty());QVERIFY(hoverStep(state,hover,true,{true}).empty());
        for(auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click}) {
            auto p=hit;p.kind=kind;QVERIFY(mapPointerCurrent(p,f.world,17));
        }
        // Stock 516.1689 drags and drops what a PASS_MOUSE relay shows (plane-click-world gestures.json, relay).
        for(auto kind:{MapPointer::DoubleClick,MapPointer::Wheel,MapPointer::Drag,MapPointer::Drop}) {
            auto p=hit;p.kind=kind;p.source=std::make_shared<const MapPointer>(hit);
            QCOMPARE(mapPointerCurrent(p,f.world,17),kind==MapPointer::Drag || kind==MapPointer::Drop);
        }
        for(uint32_t id:{3,5}) {
            const auto fields=fieldsJson(f.world.appearances[id]);
            f.change(id,{{"renderSource","*rebound"}});QVERIFY(!mapPointerCurrent(hit,f.world,17));
            setFields(f.world.appearances[id], fields);
        }
        const auto old=f.sprites;f.change(1,{{"screenLoc","TOP:-50,CENTER:-20"}});f.refresh();
        QCOMPARE(f.at({79,16}).click.over.kind,uint8_t(proto::enc::RefTurf));
        const auto retained=mapPointerAt({79,16},{224,160},{7,5},f.frame.source,f.frame.geometry,"main.map",true,old);
        QVERIFY(retained && retained->planeMousePath);QCOMPARE(retained->click.over.ref,1u);
        {   // A relay standing one pixel further right moves what it draws and what it passes clicks to with it (stock
            // 516.1689, tests/data/plane-relay-world loc23, locpx).
            test::HudRelayFixture moved("chain");
            for(auto& s:moved.sprites) if(s.atom==3) s.iconBottom.rx()+=1;
            const auto through=moved.at({80,16});
            QVERIFY(through.planeMousePath); QCOMPARE(through.click.over.ref,1u); QCOMPARE(through.click.iconX,4);
        }
        for(int variant=1;variant<6;++variant) {
            test::HudRelayFixture refused("chain");
            for(auto& s:refused.sprites) if(s.atom==3) {
                // A filter a relay is not drawn with (a blur, outline, shadow or colour matrix passes: plane-relay-world
                // fblur-loc23, foutline-loc23, fcolor-loc23).
                if(variant==1) {
                    auto fx=std::make_shared<PlaneEffects>();PlaneFilter wave;wave.kind=PlaneFilter::Wave;
                    fx->filters.push_back(wave);s.planeEffects=fx;
                }
                if(variant==2) s.tint[3]=.5f;
                if(variant==3) s.passMouse=false; // mixed ownership chain is not admitted
                if(variant==4) s.renderSource="*middle"; // capture cycle
                if(variant==5) s.imageIdentity=1;
            }
            QVERIFY(!refused.at({79,16}).planeMousePath);
        }
        test::HudRelayFixture duplicate("chain");
        auto master=*std::find_if(duplicate.sprites.begin(),duplicate.sprites.end(),[](const auto& s){return s.atom==2;});
        master.plane=11;duplicate.sprites.push_back(master);
        QVERIFY(!duplicate.at({79,16}).planeMousePath);
    }
    void stockWorldPassMouse() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/plane-pass-world/observations.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        const auto rows = document.array();
        QVERIFY2(rows.size() == 13, qPrintable(path + ": expected 13 retained plane-pass cases"));
        QSet<QString> names, recordings;
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const QString name = row["case"].toString(), frames = row["frames"].toString();
            const QString context = path + ": case " + name + ": ";
            QVERIFY2(!name.isEmpty() && !names.contains(name), qPrintable(context + "missing or duplicate case name"));
            QVERIFY2(!frames.isEmpty() && !recordings.contains(frames), qPrintable(context + "missing or duplicate frames file"));
            names.insert(name); recordings.insert(frames);
            const auto points = row["points"].toArray();
            QVERIFY2(points.size() == 3, qPrintable(context + "expected three retained input points"));
            QSet<QPoint> positions;
            for (const auto& pointValue : points) {
                const auto point = pointValue.toObject();
                QVERIFY2(testinput::isIntegerTuple(point["point"], 2, 0, 223),
                         qPrintable(context + "point must contain two integer coordinates in the 224-pixel map"));
                const auto xy = point["point"].toArray();
                const QPoint position(xy[0].toInt(), xy[1].toInt());
                QVERIFY2(!positions.contains(position), qPrintable(context + "duplicate input point"));
                positions.insert(position);
                QVERIFY2(point["events"].isArray(), qPrintable(context + "events must be an explicit array, including no callbacks"));
                QStringList kinds;
                for (const auto& event : point["events"].toArray()) {
                    const auto expected = event.toObject();
                    const auto observationError = test::ReplayFixture::clickObservationError(expected);
                    QVERIFY2(observationError.isEmpty(), qPrintable(context + observationError));
                    kinds.append(expected["kind"].toString());
                }
                QVERIFY2(kinds.isEmpty() || kinds == QStringList({"Down", "Up", "Click"}) ||
                         kinds == QStringList({"Enter", "Down", "Up", "Click"}),
                         qPrintable(context + "expected the retained Enter/Down/Up/Click callback order, with optional Enter"));
            }
        }
        int callbacks=0,empty=0;
        for(const auto& value:rows) {
            const auto row=value.toObject();const auto name=row["case"].toString();
            test::ReplayFixture f;
            QVERIFY2(f.load(row["frames"].toString(),"plane-pass-world","stock-shadow-world"),qPrintable(name));
            // Live MAP publishes unified-scene visibility: never synthesize a
            // flattened terrain hit through a captured turf's alpha hole.
            f.frame.geometry.visibility=std::make_shared<const VisibilityFrame>();
            for(const auto& pointValue:row["points"].toArray()) {
                const auto point=pointValue.toObject();const auto xy=point["point"].toArray();
                const auto picked=mapPointerAt({xy[0].toDouble(),xy[1].toDouble()},{224,224},{7,7},
                    f.frame.source,f.frame.geometry,"main.map",true,f.sprites);
                QVERIFY(picked);
                const auto events=point["events"].toArray();
                if(events.empty()) {
                    ++empty;QVERIFY(picked->backgroundHidden);
                    QCOMPARE(picked->click.over.kind,uint8_t(proto::enc::RefNull));
                }
                for(const auto& eventValue:events) {
                    const auto expected=eventValue.toObject();auto hit=*picked;
                    const auto kind=expected["kind"].toString();
                    hit.kind=kind=="Enter" ? MapPointer::Hover : kind=="Down" ? MapPointer::Down :
                        kind=="Up" ? MapPointer::Up : MapPointer::Click;
                    const auto mismatch=f.checkClick(hit,expected);
                    QVERIFY2(mismatch.isEmpty(),qPrintable(name+" "+kind+": "+mismatch));
                    QVERIFY2(mapPointerCurrent(hit,f.session.world(),0),qPrintable(name+" lifetime"));
                    if(expected["target"].toString()!="floor") {
                        QVERIFY2(hit.planeMousePath,qPrintable(name));
                        QCOMPARE(hit.planeMousePath->hops.size(),name.endsWith("chain") ? size_t(2) : size_t(1));
                    }
                    ++callbacks;
                }
            }
        }
        QCOMPARE(callbacks,150);QCOMPARE(empty,1);
    }
    // tests/data/plane-click-world gestures.json (stock 516.1689): a press, moves with the button held and a release,
    // with the mouse procs the world logged. A drag and a drop name the pressed atom and the atom under the pointer,
    // each the one a click there names: a client image's atom, a vis_contents member and a plane's own atom alike.
    void planeDragsLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/plane-click-world/gestures.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        int drags = 0, drops = 0; QSet<QString> dragged;
        for (const auto& value : document.object()["cases"].toArray()) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            const QString replay = QString(FUGUE_TEST_DATA_DIR) + "/plane-click-world/replay/" + name + ".txt";
            if (!QFile::exists(replay)) continue;   // a case kept for its log alone
            const int64_t login = 1'000'000'000'000'000'000;
            test::ReplayFixture f; f.session.setIngestClockNs(login);
            QVERIFY2(f.load(name + ".txt", "plane-click-world/replay", {}, login + 6'000'000'000), qPrintable(name + ": " + f.error));
            auto pick = [&](const QString& at) {
                const auto xy = at.split(',');
                return mapPointerAt(QPointF(xy.value(0).toInt(), xy.value(1).toInt()), {224, 224}, {7, 7}, f.frame.source,
                                    f.frame.geometry, "map", true, f.sprites);
            };
            std::shared_ptr<const MapPointer> pressed;
            for (const auto& entry : row["actions"].toArray()) {
                const auto action = entry.toObject()["action"].toString();
                QStringList logged; for (const auto& line : entry.toObject()["logged"].toArray()) logged.push_back(line.toString());
                const auto colon = action.indexOf(':'); if (colon < 0) continue;   // a plain click
                const auto verb = action.left(colon); const auto hit = pick(action.mid(colon + 1));
                QVERIFY2(hit, qPrintable(name + " " + action));
                if (verb == "down") { auto down = *hit; down.kind = MapPointer::Down; pressed = std::make_shared<const MapPointer>(down); continue; }
                if (!pressed) { QVERIFY2(logged.isEmpty(), qPrintable(name + " " + action)); continue; }   // a move, nothing held
                const bool drop = verb == "up";
                const auto line = logged.filter(drop ? "DROP " : "DRAG ").value(0).split(' ');   // DRAG <source> over=<atom> ...
                QVERIFY2(line.size() >= 3 && line[2].startsWith("over="), qPrintable(name + " " + action + ": " + logged.join("; ")));
                auto gesture = *hit; gesture.kind = drop ? MapPointer::Drop : MapPointer::Drag; gesture.source = pressed;
                const auto from = QString::fromStdString(f.targetName(*pressed)), over = QString::fromStdString(f.targetName(gesture));
                QVERIFY2(from == line[1] && over == line[2].mid(5),
                         qPrintable(QString("%1 %2: stock %3 over %4, got %5 over %6").arg(name, action, line[1], line[2].mid(5), from, over)));
                QVERIFY2(mapPointerCurrent(gesture, f.session.world(), 0), qPrintable(name + " " + action + ": not sent"));
                dragged.insert(from); ++(drop ? drops : drags);
                if (drop) pressed.reset();
            }
        }
        QVERIFY2(drags >= 6 && drops >= 4, qPrintable(QString("%1 drags, %2 drops").arg(drags).arg(drops)));
        QVERIFY2(dragged.contains("holder") && dragged.contains("part"), qPrintable(QStringList(dragged.values()).join(", ")));
    }
    // tests/data/plane-click-world (stock 516.1689): a HUD plane master with a matrix, filters or both over a world
    // object, a client image and a vis_contents member on its plane (turned on their own too), held at the end of an
    // animation, or masked through a render target. replay/<case>.txt is what DreamDaemon sent for the
    // case; clicks.json what stock named for left clicks there (with icon-x/y) and the entries of its right-click menus.
    void planeClicksLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/plane-click-world/clicks.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        QSet<QString> remaining{"plain", "outline1", "turn90", "scalex2", "turn90-outline1", "tree45", "tree45-scalex2",
                                "iconalpha", "rtalpha", "ptalpha", "rtalpha-inverse", "ptalpha-inverse", "turn90-rtalpha",
                                "scalex2-ptalpha",
                                // 2026-11-26: an image and a member turned on their own, on a plain, turned or scaled
                                // plane; a master held at the end of an animated matrix or mask offset; a mask whose
                                // source is turned or outlined; an atom's own mask from a render target.
                                "selfturn", "turn90-selfturn", "scalex2-selfturn", "spin90", "grow2x", "iconalpha-slide",
                                "rtalpha-rtturn", "rtalpha-rtoutline", "ptalpha-ptturn", "ptalpha-ptoutline", "selfmask",
                                "selfmask-inverse"};
        QSet<QString> over;
        int clicks = 0, menus = 0;
        for (const auto& value : document.object()["cases"].toArray()) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            QVERIFY2(remaining.remove(name), qPrintable(path + ": unknown or duplicate case " + name));
            // Taken six seconds after login, as the animated cases' clicks were: the end of their first step, held.
            const int64_t login = 1'000'000'000'000'000'000, clicked = login + 6'000'000'000;
            test::ReplayFixture f;
            f.session.setIngestClockNs(login);
            QVERIFY2(f.load(name + ".txt", "plane-click-world/replay", {}, clicked), qPrintable(name + ": " + f.error));
            QCOMPARE(f.frame.source.size(), QSize(224, 224));
            auto pick = [&](const QJsonValue& at, QString& context) -> std::optional<MapPointer> {
                context = QString("%1 at %2,%3").arg(name).arg(at[0].toInt()).arg(at[1].toInt());
                if (!testinput::isIntegerTuple(at, 2, 0, 223)) return {};
                return mapPointerAt(QPointF(at[0].toInt(), at[1].toInt()), {224, 224}, {7, 7}, f.frame.source,
                                    f.frame.geometry, "map", true, f.sprites);
            };
            for (const auto& entry : row["clicks"].toArray()) {
                const auto click = entry.toObject(); QString context;
                const auto hit = pick(click["at"], context);
                QVERIFY2(hit, qPrintable(context));
                const auto named = QString::fromStdString(f.targetName(*hit));
                QVERIFY2(named == click["over"].toString() && hit->click.iconX == click["iconX"].toInt() &&
                         hit->click.iconY == click["iconY"].toInt(),
                         qPrintable(QString("%1: stock %2 x=%3 y=%4, got %5 x=%6 y=%7").arg(context, click["over"].toString())
                                    .arg(click["iconX"].toInt()).arg(click["iconY"].toInt()).arg(named)
                                    .arg(hit->click.iconX).arg(hit->click.iconY)));
                over.insert(named); ++clicks;
            }
            for (const auto& entry : row["menus"].toArray()) {
                const auto menu = entry.toObject(); QString context;
                const auto hit = pick(menu["at"], context);
                QVERIFY2(hit, qPrintable(context));
                QStringList stock, listed;
                for (const auto& atom : menu["entries"].toArray()) stock.push_back(atom.toString());
                for (const auto& candidate : mapPointerStack(*hit)) listed.push_back(QString::fromStdString(f.targetName(candidate)));
                QVERIFY2(!stock.empty() && listed == stock,
                         qPrintable(QString("%1: stock menu %2, got %3").arg(context, stock.join(", "), listed.join(", "))));
                ++menus;
            }
        }
        QVERIFY2(remaining.isEmpty(), qPrintable(path + ": missing case " + QStringList(remaining.values()).join(", ")));
        QVERIFY2(over == (QSet<QString>{"floor", "part", "holder", "child", "master", "leaner"}), qPrintable(QStringList(over.values()).join(", ")));
        QVERIFY2(clicks >= 130 && menus >= 67,qPrintable(QString("%1 clicks, %2 menus").arg(clicks).arg(menus)));
    }
    // tests/data/master-screen-loc-world (stock 516.1689, README "Clicks in the larger map"): a screen object outside the
    // 7x7 view enlarges the map, in controls of 192, 224 and 288 pixels. Stock takes the pointer to the world only in
    // the view: in the black strip at the control's left or top the world hidden there is clicked and listed as if it
    // were shown, and in the added tiles nothing of the world is, drawn or not; screen objects are clicked anywhere. A
    // colour-matrix atom is clicked on its own tile, not where stock draws it stretched. replay/<case>.txt is what
    // DreamDaemon sent; clicks.json what stock named for left clicks and listed in right-click menus.
    // tests/data/hover-world (stock 516.1689): the real pointer moved over stock's map with presses and releases, and
    // the hover procs the world logged per action (hover.json), for each compile of the world with what DreamDaemon sent
    // it (replay/<compile>.txt). Each run is replayed in order: the real hit test picks, MapHover reports under the
    // MouseSubs and appearance flags DreamDaemon sent, and the server's rule turns the reports into procs: MouseExited
    // (with the params it entered with) and MouseEntered when a report names another atom than the last one, MouseMove
    // when it names the same one; a drop enters the atom it lands on, and a drop over a map where nothing is (empty
    // space, the letterbox, another map's empty tile) exits the last one. Those must be the procs stock's world logged,
    // action by action. Cases that need moves posted to the window, fast sweeps or a scene that moves are left out, and
    // so are those over the EXTRA compile's stat panel and grid, which Fugue does not report hover on.
    // The EXTRA compile adds a second map (window pixels 300..395 x 0..31), modifiers, and right-click menus: a menu
    // opened at the release of a right press on an obj (every obj has a verb, a turf none) takes the pointer until
    // Escape, a press off it, or a pick from it (a press within its two levels, 258 x 40 pixels right of and below where
    // it opened); the menu ignores the middle button, whose release after Escape closed the menu is a click where it is
    // released. A right press while the middle button is held does nothing. Straight from one map onto another, the
    // second map's pick comes before the first map's leave.
    void hoverLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/hover-world/hover.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        const QSet<QString> entered{"enter", "enter2", "both", "big", "slider", "glider", "hopper", "enteronly", "island", "hud2"},
            exited{"enter", "enter2", "both", "big", "slider", "glider", "hopper", "exitonly", "moveexit", "island", "hud2"},
            moving{"move", "both", "big", "slider", "glider", "hopper", "moveexit", "island", "hud2"};
        const QSet<QString> hoverProcs{"ENTER", "EXIT", "MOVE", "C-ENTER", "C-EXIT", "C-MOVE"};
        QSet<QString> runs; int actions = 0, reports = 0, lines = 0;
        for (const auto& runValue : document.object()["runs"].toArray()) {
            const auto run = runValue.toObject(); const auto name = run["run"].toString();
            if (name.startsWith("pace")) continue;
            runs.insert(name);
            const auto replay = run["replay"].toString();
            test::ReplayFixture f; net::InputModel im;
            QVERIFY2(f.load(replay, "hover-world/replay"), qPrintable(name + ": " + f.error));
            // The live map publishes unified-scene visibility: a turf without an icon is empty space, named by nothing.
            f.frame.geometry.visibility = std::make_shared<const VisibilityFrame>();
            QVERIFY(f.readFrames(replay, "hover-world/replay", [&](uint16_t type, const Bytes& body) { im.onFrame(type, body); }));
            QStringList switches; for (const auto& v : run["switches"].toArray()) switches.push_back(v.toString());
            const bool clientEnter = switches.contains("CLIENT_ENTER"), clientMove = switches.contains("CLIENT_MOVE");
            // Any client hover proc sets the hover subscription; client/MouseMove also the move bit.
            QCOMPARE(im.clientHover(), clientEnter || clientMove); QCOMPARE(im.clientMove(), clientMove);
            QCOMPARE(im.showPopupMenus(), switches.contains("EXTRA"));
            const auto& world = f.session.world();
            NamedMap second("map2");
            const auto map2 = second.prepare(world, [&](uint32_t id, QByteArray& out) { return f.read(id, out); }, 0, 0);
            auto flagsOf = [&](const MapPointer* p) -> uint32_t {
                const auto target = p ? mapPointerTarget(*p, world) : std::nullopt;
                if (!target || !target->appid) return 0;
                const auto a = world.appearances.find(*target->appid);
                return a == world.appearances.end() ? 0 : a->second.fields.appearanceFlags;
            };
            // 1: the map, 2: map2, 0: anywhere else.
            auto region = [](QPoint q) {
                return q.x() >= 0 && q.y() >= 0 && q.x() < 288 && q.y() < 224 ? 1 :
                       q.x() >= 300 && q.y() >= 0 && q.x() < 396 && q.y() < 32 ? 2 : 0;
            };
            proto::enc::Mods mods;
            auto pick = [&](QPoint q) {
                std::optional<MapPointer> p;
                if (region(q) == 1)
                    p = mapPointerAt(QPointF(q), {288, 224}, {7, 7}, f.frame.source, f.frame.geometry, "map", true, f.sprites);
                else if (region(q) == 2)
                    p = mapPointerAt(QPointF(q - QPoint(300, 0)), {96, 32}, map2.terrain.geometry.region.size(),
                                     map2.terrain.source, map2.terrain.geometry, "main.map2", false, map2.sprites);
                auto picked = p ? *p : MapPointer{};
                picked.click.mods = mods;
                picked.click.button.clear();
                return picked;
            };
            // The server: what it was last told the pointer is over, and the params it entered with.
            struct Stored { QString name, params; proto::enc::AtomRef over; };
            std::optional<Stored> stored; QStringList logged;
            auto exit = [&] {
                if (!stored) return;
                if (clientEnter) logged << "C-EXIT " + stored->name + " " + stored->params;
                if (exited.contains(stored->name)) logged << "EXIT " + stored->name + " " + stored->params;
                stored.reset();
            };
            auto enter = [&](const MapPointer& p, const QString& params) {
                const auto atom = QString::fromStdString(f.targetName(p));
                if (clientEnter) logged << "C-ENTER " + atom + " " + params;
                if (entered.contains(atom)) logged << "ENTER " + atom + " " + params;
                stored = Stored{atom, params, p.click.over};
            };
            auto same = [&](const MapPointer& p) {
                return stored && stored->over.kind == p.click.over.kind && stored->over.ref == p.click.over.ref &&
                       stored->over.x == p.click.over.x && stored->over.y == p.click.over.y;
            };
            MapHover hover; int64_t now = 0;
            auto flush = [&](int64_t ns) {
                now += ns;
                for (auto body = hover.due(now); !body.empty(); body = hover.due(now)) {
                    ++reports;
                    const auto* sent = hover.sent();
                    if (!sent) { exit(); continue; }
                    const auto params = QString::fromStdString(proto::enc::hoverParams(sent->click));
                    if (same(*sent)) {
                        const auto atom = stored->name;
                        if (clientMove) logged << "C-MOVE " + atom + " " + params;
                        if (moving.contains(atom)) logged << "MOVE " + atom + " " + params;
                    } else {
                        exit();
                        enter(*sent, params);
                    }
                }
            };
            auto report = [&](MapPointer p) {
                p.kind = MapPointer::Hover;
                const MapHover::Gate gate{im.clientHover(), im.clientMove(), flagsOf(&p), flagsOf(hover.last())};
                hover.update(p, mapPointerCurrent(p, world, 0), gate, now);
            };
            auto leave = [&] {
                MapPointer p; p.kind = MapPointer::Leave;
                hover.update(p, true, {im.clientHover(), im.clientMove(), 0, flagsOf(hover.last())}, now);
            };
            QPoint at(-80, 112); bool held = false, dragged = false; MapPointer pressed;
            std::optional<QPoint> menu; bool menuPick = false;
            bool middleHeld = false, middleFromMenu = false;   // the middle button down; its press taken by the menu
            auto onMenu = [&](QPoint q) {
                return menu && q.x() >= menu->x() && q.x() <= menu->x() + 258 && q.y() > menu->y() && q.y() <= menu->y() + 40;
            };
            auto menuClosed = [&](bool leftHeld, bool rightHeld) {
                menu.reset();
                const auto closed = mapMenuClosed(leftHeld, rightHeld, middleHeld, region(at) != 0);
                if (closed == MapMenuClosed::Nothing) return;
                auto p = pick(at);
                if (closed == MapMenuClosed::PickRight) p.click.button = "right";
                if (closed == MapMenuClosed::PickMiddle) p.click.button = "middle";
                report(p);
            };
            auto moveTo = [&](QPoint q) {
                if (q == at) return;   // no real move: Windows sends none
                const int from = region(at), to = region(q);
                at = q;
                if (menu) return;      // the menu has the pointer
                if (held) {
                    if (to && !sameMapTarget(pick(q), pressed)) dragged = true;
                    return;   // a held button drags: no hover
                }
                if (to) report(pick(q));
                if (from && from != to) leave();   // after the map reached has reported
            };
            auto press = [&](const char* button) {
                held = true; dragged = false; pressed = pick(at);
                pressed.click.button = button;
            };
            auto release = [&](const char* button) {
                held = false;
                auto up = pick(at); up.kind = MapPointer::Up; up.click.button = button;
                if (!mapReleaseForgetsHover(mapPopupAction(up, im.showPopupMenus()))) {
                    if (pressed.click.over.kind == proto::enc::RefObj) menu = at;
                    return;
                }
                hover.released();
                auto drop = region(at) ? pick(at) : MapPointer{};
                if (region(at) && (dragged || !sameMapTarget(drop, pressed))) {
                    if (drop.click.over.kind == proto::enc::RefNull) {
                        exit();
                    } else if (!same(drop)) {
                        drop.click.button = pressed.click.button;
                        enter(drop, QString::fromStdString(proto::enc::clickParams(drop.click)));
                    }
                }
            };
            // The stat panel and the grid of the EXTRA compile.
            auto unreported = [](QPoint q) { return (q.x() >= 300 && q.y() >= 40 && q.y() < 224) || (q.y() >= 232 && q.x() < 288); };
            auto point = [](const QString& action) -> std::optional<QPoint> {
                const auto xy = action.split(',');
                if (xy.size() != 2 || action.contains(':')) return {};
                return QPoint(xy[0].toInt(), xy[1].toInt());
            };
            for (const auto& caseValue : run["cases"].toArray()) {
                const auto row = caseValue.toObject(); const auto label = name + "/" + row["case"].toString();
                bool skip = row["case"].toString().contains("still");
                for (const auto& entry : row["actions"].toArray()) {
                    const auto action = entry.toObject()["action"].toString();
                    skip |= action.startsWith("post:") || action.startsWith("sweep:") || action.startsWith("tap");
                    if (const auto q = point(action)) skip |= unreported(*q);
                }
                if (skip) {   // what it leaves behind is not modelled: start the next case afresh, the pointer outside
                    hover = MapHover{}; stored.reset(); at = QPoint(-80, 112); held = false; menu.reset();
                    continue;
                }
                moveTo(QPoint(-80, 112)); flush(1'000'000'000); logged.clear();   // not kept by the probe
                for (const auto& entry : row["actions"].toArray()) {
                    const auto action = entry.toObject()["action"].toString();
                    if (const auto q = point(action)) {
                        moveTo(*q);
                    } else if (action == "down" || action == "rdown") {
                        if (menu && action == "down" && onMenu(at)) {
                            menuPick = true;   // an entry of the menu
                        } else if (action == "rdown" && middleHeld && !middleFromMenu) {
                            // ignored, and so is its release
                        } else {
                            if (menu) menuClosed(action == "down", action == "rdown");
                            press(action == "down" ? "left" : "right");
                        }
                    } else if (action == "mdown") {
                        middleHeld = true;
                        middleFromMenu = bool(menu);   // the menu takes it and stays open
                        if (!menu) press("middle");
                    } else if (action == "mup") {
                        middleHeld = false;
                        if (!std::exchange(middleFromMenu, false)) {
                            release("middle");
                        } else if (!menu) {   // pressed where it is released
                            press("middle");
                            release("middle");
                        }
                    } else if (action == "rup" && middleHeld && !middleFromMenu) {
                        // the ignored right press's release
                    } else if (action == "up" || action == "rup") {
                        if (menuPick) {
                            menuPick = false;
                            menuClosed(false, false);
                        } else {
                            release(action == "up" ? "left" : "right");
                        }
                    } else if (action == "key:esc") {
                        if (menu) menuClosed(false, false);
                    } else if (action.startsWith("kdown:") || action.startsWith("kup:")) {
                        const auto key = action.section(':', 1);
                        (key == "ctrl" ? mods.ctrl : key == "shift" ? mods.shift : mods.alt) = action.startsWith("kdown:");
                    } else if (action.startsWith("wait:")) {
                        now += int64_t(action.mid(5).toDouble() * 1e9);
                    } else {
                        QVERIFY2(action.startsWith("shot:"), qPrintable(label + ": not modelled: " + action));
                    }
                    flush(action.startsWith("shot:") ? 50'000'000 : 350'000'000);
                    QStringList stock;
                    for (const auto& line : entry.toObject()["logged"].toArray()) {
                        const auto words = line.toString().split(' ');
                        if (words.size() >= 4 && hoverProcs.contains(words[1])) stock << words[1] + " " + words[2] + " " + words.last();
                    }
                    QVERIFY2(logged == stock, qPrintable(QString("%1 %2: stock [%3], got [%4]")
                                                          .arg(label, action, stock.join("; "), logged.join("; "))));
                    lines += int(stock.size()); logged.clear(); ++actions;
                }
            }
        }
        QCOMPARE(runs, (QSet<QString>{"base", "base2", "lag10", "center", "cmove", "cboth", "extra", "extramenu", "xboth",
                                         "round3", "round3mid", "xround3"}));
        QVERIFY2(actions == 784 && reports == 387 && lines == 467,
                 qPrintable(QString("%1 actions, %2 reports, %3 procs").arg(actions).arg(reports).arg(lines)));
    }
    // tests/data/hover-world pace runs (stock 516.1689, tick_lag 0.25, 0.5 and 10 ds): 20 one-pixel steps over one atom
    // with MouseMove, 1 to 50 ms apart; how many MouseMove calls stock's world got. MapHover keeps the last pick of each
    // tick of 1/64 s, so the count depends on where the steps fall in the ticks. Stock's count must be one that some
    // phase gives, or differ from those by one at most: 7 of the 35 sweeps do (stock's ticks are not exactly even).
    void hoverPaceLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/hover-world/hover.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        test::ReplayFixture f;
        QVERIFY2(f.load("base.txt", "hover-world/replay"), qPrintable(f.error));
        int cases = 0;
        for (const auto& runValue : document.object()["runs"].toArray()) {
            const auto run = runValue.toObject();
            if (!run["run"].toString().startsWith("pace")) continue;
            for (const auto& caseValue : run["cases"].toArray()) {
                const auto row = caseValue.toObject(); const auto label = run["run"].toString() + "/" + row["case"].toString();
                QStringList sweep; int measured = 0; bool counting = false;
                for (const auto& entry : row["actions"].toArray()) {
                    const auto action = entry.toObject()["action"].toString();
                    if (action.startsWith("sweep:")) { sweep = action.mid(6).split(','); counting = true; }
                    else if (!action.startsWith("wait:")) counting = false;
                    if (counting)
                        for (const auto& line : entry.toObject()["logged"].toArray())
                            measured += line.toString().split(' ').value(1) == "MOVE";
                }
                if (sweep.size() != 6) continue;
                const int x0 = sweep[0].toInt(), x1 = sweep[2].toInt(), y = sweep[1].toInt(), n = sweep[4].toInt();
                const int64_t step = sweep[5].toLongLong() * 1'000'000;
                auto pickAt = [&](int x) {
                    auto p = *mapPointerAt(QPointF(x, y), {288, 224}, {7, 7}, f.frame.source, f.frame.geometry, "map", true, f.sprites);
                    p.kind = MapPointer::Hover;
                    return p;
                };
                const auto start = pickAt(112);
                QCOMPARE(f.targetName(start), std::string("both"));
                const MapHover::Gate gate{false, false, 0x00180804u, 0x00180804u};
                int least = n + 1, most = -1;
                for (int phase = 0; phase < 64; ++phase) {
                    MapHover hover; int64_t now = 0; int sent = 0;
                    hover.update(start, true, gate, now); now += 500'000'000; hover.due(now);
                    now += phase * MapHover::kPeriodNs / 64;
                    const int64_t begin = now;
                    for (int64_t t = begin; t < begin + n * step + 100'000'000; t += 250'000) {
                        const int64_t i = (t - begin) / step;
                        if (i < n && (t - begin) % step < 250'000)
                            hover.update(pickAt(x0 + int((x1 - x0) * (i + 1) / n)), true, gate, t);
                        sent += !hover.due(t).empty();
                    }
                    least = std::min(least, sent); most = std::max(most, sent);
                }
                QVERIFY2(measured >= least - 1 && measured <= most + 1,
                         qPrintable(QString("%1: stock %2 MouseMove, MapHover %3..%4").arg(label).arg(measured).arg(least).arg(most)));
                ++cases;
            }
        }
        QVERIFY2(cases >= 33, qPrintable(QString("%1 cases").arg(cases)));
    }
    void largerMapClicksLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/master-screen-loc-world/clicks.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        QSet<QString> remaining{"8,1_obj_wide", "1,9_obj_wide", "0,4_obj_wide", "8,1_obj_wide_288x288", "8,1_obj_wide_192x192"};
        int clicks = 0, none = 0, menus = 0;
        for (const auto& value : document.object()["cases"].toArray()) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            QVERIFY2(remaining.remove(name), qPrintable(path + ": unknown or duplicate case " + name));
            test::ReplayFixture f;
            QVERIFY2(f.load(name + ".txt", "master-screen-loc-world/replay"), qPrintable(name + ": " + f.error));
            // The frame after the one that learns of the screen object holds the larger map.
            const auto reader = [&](uint32_t id, QByteArray& out) { return f.read(id, out); };
            const QMargins hud = f.entities.hudMargins();
            QVERIFY2(!hud.isNull(), qPrintable(name));
            f.frame = f.terrain.prepare(f.session.world(), reader, 0, false, hud);
            f.sprites = f.entities.prepare(f.session.world(), f.frame, reader, 0, nullptr, true);
            const int tile = f.frame.geometry.tile;
            const auto size = name.section('_', 3, 3);
            const int side = size.isEmpty() ? 224 : size.section('x', 0, 0).toInt();
            MapPresentationSettings normal; normal.iconSize = tile;   // the skin's icon-size = 32: the map is not scaled
            const auto placement = mapPresentation(QSizeF(side, side), f.frame.source.size(), tile, normal);
            QCOMPARE(placement.scale, 1.0);
            auto pick = [&](const QJsonValue& at, QString& context) -> std::optional<MapPointer> {
                context = QString("%1 at %2,%3").arg(name).arg(at[0].toInt()).arg(at[1].toInt());
                if (!testinput::isIntegerTuple(at, 2, 0, side - 1)) return {};
                return mapPointerAtPresentation(QPointF(at[0].toInt(), at[1].toInt()), placement.destination,
                                                f.frame.source.size() / tile, f.frame.source, f.frame.geometry, "map", true,
                                                f.sprites);
            };
            // A menu entry is known by its verb: poke_<kind>, the icon state of a plain object, else the atom's name.
            auto kind = [&](const MapPointer& p) {
                const auto& ref = p.click.over; const auto& loc = p.click.loc;
                const auto target = model::targetFromHit(f.session.world(), ref.kind, ref.ref, int(loc.x) + 1, int(loc.y) + 1, p.screen);
                const auto ap = f.session.world().appearances.find(target.appid);
                const auto named = QString::fromStdString(f.targetName(p));
                return named == "obj" && ap != f.session.world().appearances.end() ?
                    QString::fromStdString(ap->second.fields.iconState) : named;
            };
            for (const auto& entry : row["clicks"].toArray()) {
                const auto click = entry.toObject(); QString context;
                const auto hit = pick(click["at"], context);
                QVERIFY2(hit, qPrintable(context));
                if (click["over"].isNull()) {
                    QVERIFY2(hit->click.over.kind == proto::enc::RefNull,
                             qPrintable(context + ": stock clicked nothing, got " + QString::fromStdString(f.targetName(*hit))));
                    ++none; continue;
                }
                const auto named = QString::fromStdString(f.targetName(*hit));
                const QPoint place = hit->screen ? QPoint() : QPoint(int(hit->click.loc.x) + 1, int(hit->click.loc.y) + 1);
                const auto expected = click["place"].toArray();
                QVERIFY2(named == click["over"].toString() && hit->click.iconX == click["iconX"].toInt() &&
                         hit->click.iconY == click["iconY"].toInt() && place == QPoint(expected[0].toInt(), expected[1].toInt()),
                         qPrintable(QString("%1: stock %2 x=%3 y=%4 at %5,%6, got %7 x=%8 y=%9 at %10,%11").arg(context,
                                    click["over"].toString()).arg(click["iconX"].toInt()).arg(click["iconY"].toInt())
                                    .arg(expected[0].toInt()).arg(expected[1].toInt()).arg(named).arg(hit->click.iconX)
                                    .arg(hit->click.iconY).arg(place.x()).arg(place.y())));
                ++clicks;
            }
            for (const auto& entry : row["menus"].toArray()) {
                const auto menu = entry.toObject(); QString context;
                const auto hit = pick(menu["at"], context);
                QVERIFY2(hit, qPrintable(context));
                QStringList stock, listed;
                for (const auto& atom : menu["entries"].toArray()) stock.push_back(atom.toString());
                for (const auto& candidate : mapPointerStack(*hit)) listed.push_back(kind(candidate));
                QVERIFY2(listed == stock, qPrintable(QString("%1: stock menu [%2], got [%3]").arg(context, stock.join(", "),
                                                                                              listed.join(", "))));
                ++menus;
            }
        }
        QVERIFY2(remaining.isEmpty(), qPrintable(path + ": missing case " + QStringList(remaining.values()).join(", ")));
        QVERIFY2(clicks >= 15 && none >= 5 && menus >= 10, qPrintable(QString("%1 clicks, %2 on nothing, %3 menus")
                                                                       .arg(clicks).arg(none).arg(menus)));
    }
    // tests/data/plane-relay-world (stock 516.1689): plane 1's master has the render target "*p1" and a screen object
    // draws it through render_source, standing elsewhere (numbers, edges, percents, a range), with pixel offsets, a
    // matrix or filters of its own, without PASS_MOUSE, over a master with a matrix, with an overlay of its own, as an
    // overlay of a screen object, relayed again by a second relay of its own plane, or over a client image and a
    // vis_contents member (see its README). replay/<case>.txt is what DreamDaemon sent for the case; clicks.json what
    // stock named for left clicks (with icon-x/y) and listed in right-click menus where the relay draws each atom, where
    // the atom stands and on empty floor.
    void planeRelaysLikeStock() {
        const QString path = QString(FUGUE_TEST_DATA_DIR) + "/plane-relay-world/clicks.json";
        QJsonDocument document; QString error;
        QVERIFY2(testinput::readRecordedJson(path, document, error), qPrintable(error));
        QSet<QString> remaining{"plain", "nopass", "visible", "visible-loc23", "loc23", "nopass-loc23", "locpx", "loccenter",
                                "rturn90", "nopass-rturn90", "rt_45", "rhalf", "rscalex2", "rflip", "rmove", "rgrowmove",
                                "rturnmove", "malpha-rhalf", "malpha-rt_45", "mturn90", "nopass-mturn90", "mturn90-loc23",
                                "norelay", "locleft", "locright", "locroff", "rpx", "rpw-loc23", "locpd-mark", "locpe",
                                "locpf-mark", "locrange", "locrow", "chain", "chain-loc23-bneg", "chain-rhalf",
                                "chain-bhalf", "chain-loc23-bturn90", "chain-bopaque-loc23", "chain-m5turn90",
                                "mcenter", "fblur-loc23", "foutline-loc23", "foutline-rturn90", "fcolor-loc23",
                                "nokt-rov", "nokt-rov-mturn90", "ovrelay-loc23", "ovrelay-mturn90", "nopass-rhalf",
                                "nopass-rt_45", "locpbare-mark", "iv-loc23"};
        QSet<QString> over;
        int clicks = 0, menus = 0;
        for (const auto& value : document.object()["cases"].toArray()) {
            const auto row = value.toObject(); const auto name = row["case"].toString();
            QVERIFY2(remaining.remove(name), qPrintable(path + ": unknown or duplicate case " + name));
            test::ReplayFixture f;
            QVERIFY2(f.load(name + ".txt", "plane-relay-world/replay"), qPrintable(name + ": " + f.error));
            QCOMPARE(f.frame.source.size(), QSize(224, 224));            auto pick = [&](const QJsonValue& at, QString& context) -> std::optional<MapPointer> {
                context = QString("%1 at %2,%3").arg(name).arg(at[0].toInt()).arg(at[1].toInt());
                if (!testinput::isIntegerTuple(at, 2, 0, 223)) return {};
                return mapPointerAt(QPointF(at[0].toInt(), at[1].toInt()), {224, 224}, {7, 7}, f.frame.source,
                                    f.frame.geometry, "map", true, f.sprites);
            };
            for (const auto& entry : row["clicks"].toArray()) {
                const auto click = entry.toObject(); QString context;
                const auto hit = pick(click["at"], context);
                QVERIFY2(hit, qPrintable(context));
                const auto named = QString::fromStdString(f.targetName(*hit));
                QVERIFY2(named == click["over"].toString() && hit->click.iconX == click["iconX"].toInt() &&
                         hit->click.iconY == click["iconY"].toInt(),
                         qPrintable(QString("%1: stock %2 x=%3 y=%4, got %5 x=%6 y=%7").arg(context, click["over"].toString())
                                    .arg(click["iconX"].toInt()).arg(click["iconY"].toInt()).arg(named)
                                    .arg(hit->click.iconX).arg(hit->click.iconY)));
                QVERIFY2(mapPointerCurrent(*hit, f.session.world(), 0), qPrintable(context + ": not current"));
                over.insert(named); ++clicks;
            }
            for (const auto& entry : row["menus"].toArray()) {
                const auto menu = entry.toObject(); QString context;
                const auto hit = pick(menu["at"], context);
                QVERIFY2(hit, qPrintable(context));
                QStringList stock, listed;
                for (const auto& atom : menu["entries"].toArray()) stock.push_back(atom.toString());
                for (const auto& candidate : mapPointerStack(*hit)) listed.push_back(QString::fromStdString(f.targetName(candidate)));
                QVERIFY2(!stock.empty() && listed == stock,
                         qPrintable(QString("%1: stock menu %2, got %3").arg(context, stock.join(", "), listed.join(", "))));
                ++menus;
            }
        }
        QVERIFY2(remaining.isEmpty(), qPrintable(path + ": missing case " + QStringList(remaining.values()).join(", ")));
        QVERIFY2(over == (QSet<QString>{"floor", "part", "other", "button", "relay", "relay2", "mark", "holder",
                                         "child"}),
                 qPrintable(QStringList(over.values()).join(", ")));
        QVERIFY2(clicks >= 230 && menus >= 201, qPrintable(QString("%1 clicks, %2 menus").arg(clicks).arg(menus)));
    }
    void worldPassMouseLifetimeAndBounds() {
        test::PlaneSourceFixture f;
        f.relay({{"appearanceFlags",double(0x80000204u)}});
        auto hit=f.at({69,37});
        QVERIFY(hit.planeMousePath);QCOMPARE(hit.planeMousePath->hops.size(),size_t(1));
        QCOMPARE(hit.click.over.kind,uint8_t(proto::enc::RefObj));QCOMPARE(hit.click.over.ref,100u);QVERIFY(!hit.screen);
        QCOMPARE(hit.click.loc.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(hit.click.loc.x,uint16_t(5));QCOMPARE(hit.click.loc.y,uint16_t(5));
        QCOMPARE(hit.click.iconX,6);QCOMPARE(hit.click.iconY,27);
        QVERIFY(mapPointerCurrent(hit,f.world,17));QVERIFY(!mapPointerCurrent(hit,f.world,18));
        for(uint32_t id:{200,201}) {
            ++f.world.hudIdentity[id].membership;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].membership;
            ++f.world.hudIdentity[id].key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].key;
            const auto appid=f.world.hudAppid[id];f.world.hudAppid.erase(id);
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
            ++f.world.hudAppid[id];QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
        }
        const auto master=fieldsJson(f.world.appearances[2]);
        setFields(f.world.appearances[2], R"({"appearanceFlags":268435972,"plane":60,"renderTarget":"*rebound"})");
        QVERIFY(!mapPointerCurrent(hit,f.world,17));setFields(f.world.appearances[2], master);
        const auto relay=fieldsJson(f.world.appearances[3]);
        f.relay({{"appearanceFlags",double(0x80000204u)},{"renderSource","*rebound"}});
        QVERIFY(!mapPointerCurrent(hit,f.world,17));setFields(f.world.appearances[3], relay);f.refresh();
        auto& atom=f.world.atoms.at(100);
        ++atom.renderIdentity;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.renderIdentity;
        ++atom.key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.key;
        ++atom.appid;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.appid;
        ++atom.z;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.z;
        f.world.resync.freed.insert(100);QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.resync.freed.clear();
        for(auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click,MapPointer::Hover}) {
            auto event=hit;event.kind=kind;QVERIFY(mapPointerCurrent(event,f.world,17));
        }
        for(auto kind:{MapPointer::DoubleClick,MapPointer::Wheel,MapPointer::Drag,MapPointer::Drop}) {
            auto event=hit;event.kind=kind;event.source=std::make_shared<const MapPointer>(hit);
            QCOMPARE(mapPointerCurrent(event,f.world,17),kind==MapPointer::Drag || kind==MapPointer::Drop);
        }
        const auto original=f.sprites;
        {   // The master's matrix is taken back (stock 516.1689, tests/data/plane-relay-world mturn90): moved one pixel
            // right, the source is found one pixel further right.
            f.sprites=original;
            for(auto& s:f.sprites) if(s.atom==200) {
                auto fx=std::make_shared<PlaneEffects>(); fx->matrix=std::array<double,6>{1,0,1,0,1,0}; s.planeEffects=fx;
            }
            const auto moved=f.at({70,37});
            QVERIFY(moved.planeMousePath); QCOMPARE(moved.click.over.ref,100u); QCOMPARE(moved.click.iconX,hit.click.iconX);
        }
        for(int variant=0;variant<9;++variant) {
            if(variant==2) continue;
            f.sprites=original;
            for(auto& s:f.sprites) {
                if(s.atom==200 && variant<=1) {
                    auto fx=std::make_shared<PlaneEffects>();
                    if(variant==0) fx->ignoredFilters=1;
                    if(variant==1) fx->supported=false;
                    s.planeEffects=fx;
                }
                if(s.atom==100) {
                    if(variant==3) s.targetIdentity=0;
                    if(variant==4) s.haveKey=false;
                    if(variant==5) s.imageIdentity=1;
                    if(variant==6) s.size={64,64};
                    if(variant==7) s.tint[3]=std::numeric_limits<float>::quiet_NaN();
                    if(variant==8) s.image.fill(Qt::transparent);
                }
            }
            QVERIFY(!f.at({69,37}).planeMousePath);
        }
        const auto retained=mapPointerAt({69,37},{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
        QVERIFY(retained && retained->planeMousePath);QVERIFY(sameMapTarget(hit,*retained));
        for(const auto& hop:hit.planeMousePath->hops) {
            QVERIFY(hop.master.renderedScene.empty());QVERIFY(!hop.master.composition);
            QVERIFY(hop.relay.renderedScene.empty());QVERIFY(!hop.relay.composition);
        }
    }
    void worldPassMouseTypedLeaves() {
        for(bool turf:{false,true}) {
            test::PlaneSourceFixture f;f.relay({{"appearanceFlags",double(0x80000204u)}});
            const uint32_t mob=model::kRegistryA|100;
            f.world.atoms[mob]=f.world.atoms.at(100);
            f.world.tiles[{5,5,0}]=1;
            for(auto& s:f.sprites) if(s.atom==100) {
                s.kind=turf ? MapSprite::Turf : MapSprite::Entity;
                s.atom=mob;
            }
            const auto hit=f.at({69,37});QVERIFY(hit.planeMousePath);QVERIFY(!hit.screen);
            QCOMPARE(hit.click.over.kind,uint8_t(turf ? proto::enc::RefTurf : proto::enc::RefMob));
            if(turf) {QCOMPARE(hit.click.over.x,uint16_t(5));QCOMPARE(hit.click.over.y,uint16_t(5));}
            else QCOMPARE(hit.click.over.ref,100u);
            QVERIFY(mapPointerCurrent(hit,f.world,17));
            if(turf) {
                f.world.tiles[{5,5,0}]=2;QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.tiles.erase({5,5,0});QVERIFY(!mapPointerCurrent(hit,f.world,17));
            } else {
                ++f.world.atoms[100].renderIdentity;QVERIFY(mapPointerCurrent(hit,f.world,17)); // other registry
                ++f.world.atoms[mob].appid;QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.atoms.erase(mob);QVERIFY(!mapPointerCurrent(hit,f.world,17));
            }
        }
    }
    void planeSourceLifetimeAndCoverage() {
        for (bool grouped : {false,true}) for (bool blur : {false,true}) {
        test::PlaneSourceFixture f;
        QJsonObject fields;
        if (blur) fields["filters"]=test::PlaneSourceFixture::blur("0000803f");
        if (grouped) { fields["appearanceFlags"]=0x04000204; fields["overlays"]=QJsonArray{4}; }
        f.relay(fields);
        // Stock blur adds visible ink here, but not an interactive source pixel.
        QCOMPARE(f.at({65,40}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(f.at({79,47}).click.over.kind,uint8_t(proto::enc::RefTurf));
        const auto hit=f.at({69,37});
        QCOMPARE(hit.click.over.ref,201u); QVERIFY(hit.relayHit && hit.relayHit->ink);
        QCOMPARE(hit.relayHit->producer.click.over.ref,200u);
        QCOMPARE(hit.relayHit->ink->click.over.ref,100u);
        QVERIFY(hit.relayHit->producer.renderedScene.empty()); QVERIFY(hit.relayHit->ink->renderedScene.empty());
        QVERIFY(mapPointerCurrent(hit,f.world,17));
        for (auto kind:{MapPointer::Hover,MapPointer::Drag,MapPointer::Drop,MapPointer::DoubleClick}) {
            auto gesture=hit; gesture.kind=kind; QVERIFY(!mapPointerCurrent(gesture,f.world,17));
        }
        ++f.world.hudIdentity[200].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[200].membership;
        ++f.world.hudIdentity[201].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[201].membership;
        ++f.world.atoms[100].renderIdentity; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.atoms[100].renderIdentity;
        f.world.resync.freed.insert(100); QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.resync.freed.clear();
        const auto master=f.world.appearances[2];
        setFields(f.world.appearances[2], R"({"appearanceFlags":268435972,"plane":61,"renderTarget":"*relay"})");
        QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.appearances[2]=master;
        f.world.appearances[5]=f.world.appearances[1];f.world.atoms[100].appid=5;
        QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.atoms[100].appid=1;
        f.relay({{"renderSource","*rebound"}}); QVERIFY(!mapPointerCurrent(hit,f.world,17));
        // Picking remains tied to the retained frame, not the newer worker/model scene.
        const auto old=mapPointerAt({69,37},{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
        QVERIFY(old); QCOMPARE(old->click.over.ref,201u);
        f.relay({});
        for (auto& s:f.sprites) if(s.atom==100) s.image.fill(Qt::transparent);
        QCOMPARE(f.at({69,37}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(old->renderedScene[0].image.pixelColor(5,5),QColor(255,220,20)); // retained image is immutable
        f.refresh();
        // A second selected plane with the same name is ambiguous, never guessed.
        auto duplicate=*std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        duplicate.plane=61;f.sprites.push_back(duplicate);
        QVERIFY(!f.at({69,37}).relayHit);
        f.refresh();
        for(auto& s:f.sprites) if(s.atom==100) s.renderSource="*chain";
        QVERIFY(!f.at({69,37}).relayHit);
        f.relay({{"appearanceFlags",double(0x80000204u)}}); // source-owned path, not a consumer-owned relayHit
        QVERIFY(!f.at({69,37}).relayHit);
        f.relay({{"overlays",QJsonArray{4}}});
        auto local=f.sprites.front();local.renderTarget="*relay";local.renderSource.clear();
        local.plane=80;local.localGroup=123;f.sprites.push_back(local);
        QVERIFY(!f.at({69,37}).relayHit); // an extended appearance cannot bind a local/plane collision
        f.relay({});
        const auto plan=CompositionPlan::build(f.sprites,f.frame.source,{true});
        const auto* surface=plan->planeSources.find("*relay");QVERIFY(surface);
        uint64_t work=0;QCOMPARE(PlaneSources::ink(*surface,f.sprites,{69,37},work),qsizetype(-1));
        for(auto& s:f.sprites) if(s.atom==100) s.tint[3]=0.5f;
        QVERIFY(f.at({69,37}).relayHit); // source material does not change original-ink coverage
        f.refresh();f.sprites.resize(65537);QVERIFY(!f.at({69,37}).relayHit);
        }
    }
    void groupedPlaneSourceRenderedOwnership() {
        test::PlaneSourceFixture f;
        f.relay({{"appearanceFlags",0x04000204},{"overlays",QJsonArray{4}},
                 {"filters",test::PlaneSourceFixture::blur("0000803f")}});
        const auto source=f.at({69,37});
        const auto hole=f.at({79,47});
        QVERIFY(source.relayHit && source.relayHit->ink);
        QCOMPARE(hole.click.over.kind,uint8_t(proto::enc::RefTurf));
        // A later ungrouped frame admits child ink, but cannot change the old
        // grouped frame's original-source coverage or fabricate child ownership.
        f.relay({{"overlays",QJsonArray{4}}});
        QCOMPARE(f.at({79,47}).click.over.ref,201u);
        const auto old=mapPointerAt({79,47},{160,96},{5,3},hole.renderedSource,f.frame.geometry,"main.map",true,hole.renderedScene);
        QVERIFY(old); QCOMPARE(old->click.over.kind,uint8_t(proto::enc::RefTurf));
        // Server appearance IDs are immutable; replacing the consumer appearance
        // invalidates even a source-only hit retained from the grouped frame.
        f.world.appearances[5]=f.world.appearances[3]; f.world.hudAppid[201]=5;
        QVERIFY(!mapPointerCurrent(source,f.world,17));
    }
    void planeSourceMatrixRenderedOwnership() {
        QJsonArray rows; QString error;
        QVERIFY2(test::PlaneSourceFixture::capturePixelObservations(
            QString(FUGUE_TEST_DATA_DIR)+"/relay-gate-world/matrix.json",rows,error,false),qPrintable(error));
        test::PlaneSourceFixture f;
        for (const auto& value:rows) {
            const auto row=value.toObject();
            if (!row["admitted"].toBool()) continue;
            f.observation(row);
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); if (!c[2].toBool()) continue;
                const QPoint p(c[0].toInt(),c[1].toInt()); const auto hit=f.at(p);
                QVERIFY(hit.relayHit && hit.relayHit->ink); QVERIFY(mapPointerCurrent(hit,f.world,17));
                const auto scene=hit.renderedScene;
                // A newly prepared hole cannot change the last rendered coverage
                // or the old viewport-space affine/material coordinate envelope.
                f.relay({{"renderSource","*absent"}});
                const auto old=mapPointerAt(p,{160,96},{5,3},f.frame.source,f.frame.geometry,"main.map",true,scene);
                QVERIFY(old); QCOMPARE(old->click.iconX,c[3].toInt()); QCOMPARE(old->click.iconY,c[4].toInt());
                QVERIFY(old->relayHit); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.observation(row); QVERIFY(mapPointerCurrent(hit,f.world,17));
                ++f.world.hudIdentity[201].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[201].membership;
                ++f.world.hudIdentity[200].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[200].membership;
                ++f.world.atoms[100].renderIdentity;
                if (hit.relayHit->ink->click.over.ref==100u) QVERIFY(!mapPointerCurrent(hit,f.world,17));
                --f.world.atoms[100].renderIdentity;
                f.world.hudAppid[201]=5; QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudAppid[201]=3;
            }
        }
    }
    void planeSourceCaptureCoverage() {
        QJsonArray rows; QString error;
        for (const char* name:{"source-coverage.json","capture-pixels.json","precision-model.json","master-arithmetic.json"}) {
            QJsonArray recorded;
            QVERIFY2(test::PlaneSourceFixture::capturePixelObservations(
                QString(FUGUE_TEST_DATA_DIR)+"/relay-gate-world/"+name,recorded,error),qPrintable(error));
            for (const auto& row:recorded) rows.append(row);
        }
        test::PlaneSourceFixture f;
        int clickCount=0;
        for (const auto& value:rows) {
            const auto row=value.toObject(); f.sourceObservation(row);
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); const QPoint p(c[0].toInt(),c[1].toInt()); const auto hit=f.at(p);
                ++clickCount;
                QVERIFY2(bool(hit.relayHit)==c[2].toBool(),qPrintable(row["name"].toString()));
                QCOMPARE(hit.click.iconX,c[3].toInt()); QCOMPARE(hit.click.iconY,c[4].toInt());
                QCOMPARE(hit.click.hasVis,c[5].toInt()!=0);
                if (hit.click.hasVis) { QCOMPARE(hit.click.visX,c[5].toInt()); QCOMPARE(hit.click.visY,c[6].toInt()); }
                QVERIFY(mapPointerCurrent(hit,f.world,17));
                if (!hit.relayHit) continue;
                QCOMPARE(hit.click.over.ref,201u);
                const auto contributor=hit.relayHit->ink->click.over.ref;
                if (row.contains("overlap") && p==QPoint(69,37)) QCOMPARE(contributor,101u);
                const auto atom=f.world.atoms.at(contributor);
                f.world.atoms.erase(contributor); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.atoms[contributor]=atom; ++f.world.atoms[contributor].renderIdentity;
                QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.atoms[contributor]=atom;
                f.world.atoms[contributor].appid=99; QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.atoms[contributor]=atom;
                for (uint32_t endpoint:{200u,201u}) {
                    const auto identity=f.world.hudIdentity.at(endpoint);
                    f.world.hudIdentity.erase(endpoint); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                    f.world.hudIdentity[endpoint]=identity; ++f.world.hudIdentity[endpoint].membership;
                    QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudIdentity[endpoint]=identity;
                    const auto appid=f.world.hudAppid.at(endpoint); f.world.hudAppid[endpoint]=99;
                    QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudAppid[endpoint]=appid;
                }
                f.relay({{"renderSource","*changed"}}); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                const auto old=mapPointerAt(p,{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
                QVERIFY(old && old->relayHit); QCOMPARE(old->click.iconX,c[3].toInt()); QCOMPARE(old->click.iconY,c[4].toInt());
                f.sourceObservation(row); QVERIFY(mapPointerCurrent(hit,f.world,17));
            }
        }
        QCOMPARE(clickCount,574);
        const test::PlaneSourceFixture plain;
        const auto original=plain.sprites;
        for (float alpha:{0.f,1.f/255,0.13f,0.5f,1.f}) {
            f.sprites=original;
            for (auto& s:f.sprites) if(s.atom==100 || s.kind==MapSprite::Master) {
                s.tint[3]=alpha; s.color[3]=2; s.color[19]=-1; // finite alpha-changing matrix class, not a value whitelist
            }
            QVERIFY(f.at({69,37}).relayHit); QVERIFY(!f.at({79,47}).relayHit);
        }
        // Whole surface refusal: a second unsupported contributor cannot leave
        // the first one's seemingly valid original mask active.
        for (int refusal=0;refusal<7;++refusal) {
            f.sprites=original; auto extra=f.sprites.front(); extra.position={120,0};
            switch(refusal) {
            case 0: extra.blend=2; break;
            case 1: extra.color[0]=std::numeric_limits<float>::infinity(); break;
            case 2: extra.renderSource="*chain"; break;
            case 3: extra.localGroup=5; break;
            case 4: extra.size={16,16}; break;
            case 5: extra.maptext=true; break;
            case 6: extra.planeEffects=std::make_shared<PlaneEffects>(); break;
            }
            f.sprites.push_back(extra); QVERIFY(!f.at({69,37}).relayHit);
            f.sprites=original; QVERIFY(f.at({69,37}).relayHit);
        }
    }
    void capturedTurfHasNoFallback() {
        MapGeometry geometry{{0,0,1,1},{1,1},32,0,1};
        geometry.visibility=std::make_shared<const VisibilityFrame>(); // immutable unified-scene marker
        MapSprite turf; turf.kind=MapSprite::Turf; turf.cell={1,1}; turf.iconBottom={0,32};
        turf.image=QImage(32,32,QImage::Format_RGBA8888); turf.image.fill(Qt::white);
        turf.image.setPixelColor(0,16,Qt::transparent);
        MapSprite master; master.kind=MapSprite::Master; master.renderTarget="*capture";
        MapSprite source; source.kind=MapSprite::Screen; source.plane=5; source.renderSource="*capture";
        source.mouseTransparent=true;
        auto pick=[&](MapSprites sprites,QPoint p=QPoint(16,16)) {
            return *mapPointerAt(p,{32,32},{1,1},{0,0,32,32},geometry,"main.map",true,sprites);
        };
        QCOMPARE(pick({turf}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QVERIFY(pick({turf},{0,16}).backgroundHidden); // a transparent turf pixel is not a cell hit
        QVERIFY(pick({}).backgroundHidden); // iconless/absent admitted turf
        const auto captured=pick({turf,master,source});
        QVERIFY(captured.backgroundHidden); QCOMPARE(captured.click.over.kind,uint8_t(proto::enc::RefNull));
        QCOMPARE(captured.click.screenX,1); QCOMPARE(captured.click.screenPX,17);
        QCOMPARE(pick({turf,master}).click.over.kind,uint8_t(proto::enc::RefTurf)); // producer without consumer
        master.renderTarget="capture"; source.renderSource="capture";
        QCOMPARE(pick({turf,master,source}).click.over.kind,uint8_t(proto::enc::RefTurf)); // directly shown producer
        master.renderTarget.clear(); master.tint[3]=0;
        QCOMPARE(pick({turf,master}).click.over.kind,uint8_t(proto::enc::RefTurf));
        master.renderTarget="*capture"; master.tint[3]=1; source.renderSource="*capture";
        auto show=master; show.renderTarget.clear();
        QCOMPARE(pick({turf,master,source,show}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QVERIFY(pick({turf,show,source,master}).backgroundHidden); // last-master-wins
        auto object=turf; object.kind=MapSprite::Entity; object.plane=1; object.atom=42; object.targetIdentity=7;
        const auto pressed=pick({turf,master,source,object}); QCOMPARE(pressed.click.over.ref,42u);
        auto hud=object; hud.kind=MapSprite::Screen; hud.screenInteractive=true; hud.atom=50;
        QCOMPARE(pick({turf,master,source,hud}).click.over.ref,50u);
        model::World w; w.bounds.maxX=w.bounds.maxY=1;
        auto& atom=w.atoms[42]; atom.hasPos=true; atom.renderIdentity=7;
        // Over a pixel nothing is drawn on, a hover names no atom but keeps the control and screen-loc, as stock does
        // over empty space in the map (stock 516.1689, tests/data/hover-world).
        auto hidden=captured; hidden.kind=MapPointer::Hover;
        QVERIFY(mapPointerCurrent(hidden,w,1));
        auto hovered=pressed; hovered.kind=MapPointer::Hover;
        MapHover hover; QVERIFY(!hoverStep(hover,hovered,true,{true}).empty());
        QCOMPARE(hoverStep(hover,hidden,true,{true}),mapPointerBody(hidden));
        QVERIFY(mapPointerBody(hidden)!=proto::enc::encodeHoverLeave());
        hidden.kind=MapPointer::Drop; hidden.source=std::make_shared<const MapPointer>(pressed);
        QVERIFY(mapPointerCurrent(hidden,w,1)); // null drop keeps its rendered source identity
        ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(hidden,w,1));
        geometry.visibility.reset(); // retained flattened-reference compatibility, not live MAP
        QCOMPARE(pick({}).click.over.kind,uint8_t(proto::enc::RefTurf));
    }
    void presentationGeometry() {
        MapPresentationSettings settings;
        const QSize scene(96, 64), view(3, 2);
        MapGeometry geometry{{0,0,3,2},{20,20},32,0,1};
        auto p = mapPresentation({240,240}, scene, 32, settings);
        QCOMPARE(p.destination, QRectF(0,40,240,160));
        QVERIFY(p.linear); // 2.5x
        auto hit = [&](QPointF point) { return mapPointerAtPresentation(point, p.destination, view,
            {0,0,96,64}, geometry, "main.map", true); };
        QCOMPARE(hit({80.1,80})->click.screenX, 2);
        QCOMPARE(hit({80.1,80})->click.screenPX, 1);
        QCOMPARE(hit({80,20})->click.over.kind, uint8_t(proto::enc::RefNull));
        settings.letterbox = false;
        p = mapPresentation({240,240},scene,32,settings);
        QCOMPARE(p.destination, QRectF(-60,0,360,240));
        QCOMPARE(hit({0,10})->click.screenPX, 17); // centered cropped coverage
        settings.zoom = 2;
        p = mapPresentation({240,240},scene,32,settings);
        QCOMPARE(p.destination, QRectF(24,56,192,128)); QVERIFY(p.linear); QVERIFY(p.sharpen);
        QCOMPARE(hit({88,60})->click.screenX, 2);
        settings.mode = MapPresentationSettings::Blur;
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        QVERIFY(!mapPresentation({240,240},scene,32,settings).sharpen);
        settings.zoom = 1.5; settings.mode = MapPresentationSettings::Distort;
        QVERIFY(!mapPresentation({240,240},scene,32,settings).linear);
        QVERIFY(!mapPresentation({240,240},scene,32,settings).sharpen);
        settings.zoom = 0; settings.iconSize = 48;
        QCOMPARE(mapPresentation({240,240},scene,16,settings).scale, 3.0);
        QCOMPARE(mapPresentation({240,240},scene,32,settings).scale, 1.5);
        settings.zoom = 2; // explicit zoom wins when both defaults are positive
        QCOMPARE(mapPresentation({240,240},scene,16,settings).scale, 2.0);
        settings.mode = MapPresentationSettings::Normal;
        settings.zoom = std::nextafter(2.0, 3.0);
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        settings.zoom = std::nextafter(2.0, 1.0);
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        settings.zoom = 1e300;
        QVERIFY(mapPresentation({240,240},scene,32,settings).destination.isEmpty());
        settings.zoom = 1e-300;
        const auto tiny = mapPresentation({240,240},scene,32,settings);
        QVERIFY(!mapPointerAtPresentation({0,0},tiny.destination,view,{0,0,96,64},geometry,"main.map",true));
        settings.zoom = 0; settings.iconSize = 0; settings.letterbox = true;
        QCOMPARE(mapPresentation({241,160},scene,32,settings).destination, QRectF(0.5,0,240,160));
    }
    // Stock 516.1689 (tests/data/hover-world clickleave, dropleave): any release forgets what the pointer was over.
    void releaseForgetsHover() {
        MapPointer source;source.kind=MapPointer::Hover;source.click.over=proto::enc::objRef(42);
        source.click.control="main.map";
        MapHover hover;QVERIFY(!hoverStep(hover,source,true,{true}).empty());
        QVERIFY(hoverStep(hover,source,true,{true}).empty());
        hover.released();
        QCOMPARE(hoverStep(hover,source,true,{true}),mapPointerBody(source)); // the same atom at the same pixel again
        hover.released();
        auto leave=source;leave.kind=MapPointer::Leave;
        QVERIFY(hoverStep(hover,leave,true,{true}).empty()); // nothing picked since the release: nothing to leave
        QVERIFY(!hoverStep(hover,source,true,{true}).empty());
        QCOMPARE(hoverStep(hover,leave,true,{true}),proto::enc::encodeHoverLeave());
    }
    // Stock 516.1689 (tests/data/hover-world pacepost): stock reports every move it reads, but Windows merges the real
    // pointer's moves between reads, about every 1/64 s. Picks in one tick stand for the last alone, weighed with its
    // own gate against the record as the tick found it.
    void hoverMergesPicksInATick() {
        constexpr int64_t tick = MapHover::kPeriodNs;
        MapPointer a; a.kind = MapPointer::Hover; a.click.over = proto::enc::objRef(42); a.click.control = "main.map";
        a.click.iconX = a.click.iconY = 5;
        auto b = a; b.click.over = proto::enc::objRef(43);
        auto moved = a; moved.click.iconX = 6;
        const MapHover::Gate both{false, false, 0x180800u, 0x180800u}, plain{false, false, 0, 0x180800u};
        MapHover hover;
        hover.update(a, true, both, 0);
        QCOMPARE(hover.due(tick), mapPointerBody(a));
        // Off and back within one tick: the pointer never left.
        hover.update(b, true, both, 10 * tick + 1);
        hover.update(a, true, both, 10 * tick + 2);
        QVERIFY(hover.due(11 * tick).empty());
        QVERIFY(!hover.dueAt());
        // The last pick of the tick goes out alone, with its own gate: a move onto an atom without MouseMove, after a
        // move report waiting in the same tick, sends nothing.
        auto further = a; further.click.iconX = 7;
        hover.update(moved, true, both, 20 * tick + 1);
        hover.update(further, true, plain, 20 * tick + 2);
        QVERIFY(hover.due(21 * tick).empty());
        // In separate ticks each pick is its own.
        hover.update(moved, true, both, 30 * tick + 1);
        QCOMPARE(hover.due(31 * tick), mapPointerBody(moved));
        hover.update(a, true, both, 31 * tick + 1);
        QCOMPARE(hover.due(32 * tick), mapPointerBody(a));
        // A leave between picks ends the merging: the pick before it goes out, then the leave.
        hover.update(b, true, both, 40 * tick + 1);
        MapPointer leave; leave.kind = MapPointer::Leave;
        hover.update(leave, true, {false, false, 0, 0x180800u}, 40 * tick + 2);
        QCOMPARE(hover.due(41 * tick), mapPointerBody(b));
        QCOMPARE(hover.due(41 * tick), proto::enc::encodeHoverLeave());
    }
    void hoverTransitionsAndStaleIdentities() {
        MapPointer p; p.kind = MapPointer::Hover; p.session = 7;
        p.click.over = proto::enc::objRef(42); p.click.loc = proto::enc::nullRef();
        p.click.control = "world.map"; p.screen = true; p.haveAtomKey = true; p.atomKey = 9;
        p.click.iconX = 3; p.click.iconY = 4; p.click.screenX = 1; p.click.screenY = 2;
        model::World w; w.hud[42]={}; w.hudIdentity[42] = {2,9};
        w.hudAppid[42]=1; setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
        QVERIFY(mapPointerCurrent(p,w,7));
        QVERIFY(!mapPointerCurrent(p,w,8));
        MapHover quiet;
        QVERIFY(hoverStep(quiet,p,true,{}).empty()); // no hover proc anywhere: nothing
        MapHover hover;
        QCOMPARE(hoverStep(hover,p,true,{true}), mapPointerBody(p));
        QCOMPARE(mapPointerBody(p)[0], proto::enc::kMouseHover);
        auto bytes = mapPointerBody(p);
        QByteArray packet(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        QVERIFY(packet.contains("icon-x=3;icon-y=4;screen-loc=")); QVERIFY(!packet.contains("button="));
        QVERIFY(hoverStep(hover,p,true,{true}).empty());
        p.click.iconX++;
        QVERIFY(hoverStep(hover,p,true,{true}).empty()); // the client's hover bit alone reports no moves
        p.click.iconX++;
        QVERIFY(hoverStep(hover,p,true,{true,false,0x800}).empty()); // nor does MouseEntered
        p.click.iconX++;
        QCOMPARE(hoverStep(hover,p,true,{false,false,0x80000}), mapPointerBody(p)); // the atom's MouseMove does
        p.click.iconX++;
        QVERIFY(hoverStep(hover,p,true,{false,false,0x100800}).empty()); // MouseWheel's bits do not
        p.click.iconX++;
        QCOMPARE(hoverStep(hover,p,true,{true,true}), mapPointerBody(p)); // client/MouseMove does
        auto jumped = p; jumped.click.screenX++;
        QVERIFY(hoverStep(hover,jumped,true,{true,true}).empty()); // another tile, the same pixel in it
        auto moved = p; moved.click.screenPX = p.click.iconX; moved.click.iconX += 2;
        QVERIFY(hoverStep(hover,moved,true,{true,true}).empty()); // the atom moved under a still pointer
        w.hudIdentity[42].key++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        p.click.iconY++;
        QVERIFY(hoverStep(hover,p,false,{true,true}).empty()); // a stale pick changes nothing
        p.atomKey++; QVERIFY(mapPointerCurrent(p,w,7));
        QVERIFY(!hoverStep(hover,p,true,{true,true}).empty());
        p.kind = MapPointer::Leave;
        QVERIFY(hoverStep(hover,p,true,{}).empty()); // leaving an atom without hover procs, none on the client
        QVERIFY(hoverStep(hover,p,true,{true}).empty()); // and nothing is left to leave
        p.kind = MapPointer::Hover;
        QVERIFY(!hoverStep(hover,p,true,{true}).empty());
        p.kind = MapPointer::Leave;
        QCOMPARE(hoverStep(hover,p,true,{false,false,0,0x800}), proto::enc::encodeHoverLeave());
        // Image hits route to the target, but both image and target lifetimes are checked.
        p.kind = MapPointer::Hover; p.screen = false; p.atomKey = 9;
        auto& atom = w.atoms[42]; atom.hasPos = atom.haveKey = true; atom.key = 9; atom.renderIdentity = 10;
        w.imageInstances[1] = {2,42,1,false,20,10};
        p.imageId = 1; p.imageIdentity = 20; p.targetIdentity = 10;
        QVERIFY(mapPointerCurrent(p,w,7));
        w.imageInstances[1].identity++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        w.imageInstances[1].identity--; atom.renderIdentity++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        net::InputModel input;
        Bytes flags(20); flags[15] = 0x82; flags[16] = 1;
        input.onFrame(0x003b, flags);
        QVERIFY(input.clientHover()); QVERIFY(input.clientMove());
        flags[15] = 0; input.onFrame(0x003b,flags);
        QVERIFY(!input.clientHover()); QVERIFY(!input.clientMove());
    }
    void pointerTarget() {
        model::World w; w.viewZ = 2; w.bounds.maxX = w.bounds.maxY = 10;
        w.atoms[42].appid = 5; w.atoms[42 | model::kRegistryA].appid = 6; w.hudAppid[42] = 9;
        MapPointer p; p.session = 7; p.z = 2;
        // A vis_contents mob member is the mob, on the map and under a HUD holder alike, never obj or HUD ref 42.
        auto member = std::make_shared<VisHit>(); member->target = 42 | model::kRegistryA;
        p.click.over = proto::enc::mobRef(42); p.visHit = member;
        for (bool screen : {false, true}) {
            p.screen = screen;
            const auto named = mapPointerTarget(p,w);
            QVERIFY(named && named->appid); QCOMPARE(named->kind,MapPointerTarget::Atom);
            QCOMPARE(named->atom,42u | model::kRegistryA); QCOMPARE(*named->appid,6u);
        }
        p.visHit.reset();
        auto named = mapPointerTarget(p,w); QCOMPARE(named->kind,MapPointerTarget::Hud); QCOMPARE(*named->appid,9u);
        p.screen = false; QCOMPARE(*mapPointerTarget(p,w)->appid,6u);
        p.click.over = proto::enc::objRef(42); QCOMPARE(*mapPointerTarget(p,w)->appid,5u);
        p.click.over = proto::enc::objRef(77); named = mapPointerTarget(p,w); QVERIFY(named && !named->appid);
        // A turf on the map is the cell on the map's z; a copied turf is its own cell on its own z.
        p.click.over = proto::enc::turfRef(4,5);
        const uint16_t x = uint16_t(p.click.over.x), y = uint16_t(p.click.over.y);
        w.tiles[{x,y,2}] = 7; w.tiles[{uint16_t(x+1),y,5}] = 8;
        named = mapPointerTarget(p,w); QCOMPARE(named->kind,MapPointerTarget::Turf);
        QCOMPARE(named->cell,(std::array<uint16_t,3>{x,y,2})); QCOMPARE(*named->appid,7u);
        auto copied = std::make_shared<VisHit>(); copied->turf = true; copied->cell = {uint16_t(x+1),y,5};
        p.visHit = copied; named = mapPointerTarget(p,w);
        QCOMPARE(named->cell,copied->cell); QCOMPARE(*named->appid,8u);
        p.visHit.reset(); p.click.over = proto::enc::nullRef(); QVERIFY(!mapPointerTarget(p,w));

    }
    void hoverName() {
        // The status bar names what a hover pick names, without the text-macro marker, and nothing otherwise.
        model::World w; w.viewZ = 2;
        w.atoms[42].appid = 5; w.atoms[43].appid = 6;
        const auto turf = proto::enc::turfRef(3,4);
        w.tiles[{uint16_t(turf.x),uint16_t(turf.y),2}] = 7;
        w.appearances[5].fields.name = std::string("\xFF\x01wrench");
        w.appearances[7].fields.name = "floor";  // appearance 6 is nameless
        MapPointer p; p.kind = MapPointer::Hover; p.session = 7; p.z = 2;
        p.click.over = proto::enc::objRef(42); QCOMPARE(mapHoverName(p,w), std::string("wrench"));
        p.click.over = turf; QCOMPARE(mapHoverName(p,w), std::string("floor"));
        p.click.over = proto::enc::objRef(43); QVERIFY(mapHoverName(p,w).empty());
        p.click.over = proto::enc::objRef(77); QVERIFY(mapHoverName(p,w).empty());  // gone from the model
        p.click.over = proto::enc::nullRef(); QVERIFY(mapHoverName(p,w).empty());   // empty space on the map
        p.click.over = proto::enc::objRef(42); p.kind = MapPointer::Leave; QVERIFY(mapHoverName(p,w).empty());
    }
    void dragEncodingAndLifetime() {
        MapPointer src; src.session = 7; src.z = 2;
        src.click.over = proto::enc::objRef(42); src.click.loc = proto::enc::turfRef(3,4);
        src.haveAtomKey = true; src.atomKey = 9; src.targetIdentity = 10;
        model::World w; w.viewZ = 2;
        auto& atom = w.atoms[42]; atom.hasPos = atom.haveKey = true;
        atom.key = 9; atom.z = 2; atom.renderIdentity = 10;
        w.bounds.maxX = w.bounds.maxY = 10;
        MapPointer p = src; p.kind = MapPointer::Drag;
        p.click.over = proto::enc::objRef(43); p.click.loc = proto::enc::nullRef();
        p.screen = true; p.atomKey = 11; p.targetIdentity = 20;
        w.hud[43]={}; w.hudIdentity[43] = {2,11,20};
        w.hudAppid[43]=1; setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
        p.source = std::make_shared<const MapPointer>(src);
        p.click.mods = {true,true,true}; p.click.iconX = 5; p.click.iconY = 6;
        QVERIFY(mapPointerCurrent(p,w,7));
        proto::enc::DragCmd expected;
        expected.src = src.click.over; expected.srcLoc = src.click.loc;
        expected.over = p.click.over; expected.overLoc = p.click.loc;
        expected.params = proto::enc::clickParams(p.click);
        QCOMPARE(mapPointerBody(p), proto::enc::encodeDragCmd(expected));
        p.kind = MapPointer::Drop; expected.drop = true;
        QCOMPARE(mapPointerBody(p), proto::enc::encodeDragCmd(expected));
        const auto body = mapPointerBody(p);
        QByteArray golden = QByteArray::fromHex("0700920000ffff00000700012a00000000012b0000000003020003000000ffff0000");
        golden += QByteArrayLiteral("\2mapwindow.map\0\2mapwindow.map\0");
        golden += QByteArrayLiteral("\2icon-x=5;icon-y=6;left=1;button=left;ctrl=1;shift=1;alt=1;screen-loc=1:5,1:6\0");
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(body.data()),qsizetype(body.size())),golden);
        auto hooks = proto::enc::MouseHooks{};
        QVERIFY(!mapPointerWanted(MapPointer::Drag,hooks,false));
        QVERIFY(!mapPointerWanted(MapPointer::Drop,hooks,false));
        hooks.drop = true; QVERIFY(mapPointerWanted(MapPointer::Drop,hooks,false));
        hooks.drag = true; QVERIFY(mapPointerWanted(MapPointer::Drag,hooks,false));
        // Same wire slot/key but a new local lifetime must not receive a gesture.
        atom.renderIdentity++; QVERIFY(!mapPointerCurrent(p,w,7)); atom.renderIdentity--;
        w.resync.freed.insert(42); QVERIFY(!mapPointerCurrent(p,w,7)); w.resync.freed.clear();
        w.hud.erase(43); QVERIFY(!mapPointerCurrent(p,w,7)); w.hud[43]={};
        w.hudIdentity[43].membership++; QVERIFY(!mapPointerCurrent(p,w,7)); w.hudIdentity[43].membership--;
        QVERIFY(!mapPointerCurrent(p,w,8)); w.viewZ++; QVERIFY(!mapPointerCurrent(p,w,7)); w.viewZ--;
        auto imageSource = src; imageSource.imageIdentity = 1;
        p.source = std::make_shared<const MapPointer>(imageSource);
        QVERIFY(!mapPointerCurrent(p,w,7));
        auto visSource = src; visSource.visHit = std::make_shared<VisHit>();
        p.source = std::make_shared<const MapPointer>(visSource);
        QVERIFY(!mapPointerCurrent(p,w,7));
        auto hudSource = p; hudSource.source.reset(); hudSource.kind = MapPointer::Down;
        auto worldDest = src; worldDest.kind = MapPointer::Drop;
        worldDest.source = std::make_shared<const MapPointer>(hudSource);
        QVERIFY(mapPointerCurrent(worldDest,w,7));
        w.hudIdentity[43].membership++; QVERIFY(!mapPointerCurrent(worldDest,w,7)); w.hudIdentity[43].membership--;
        w.hud.erase(43); QVERIFY(!mapPointerCurrent(worldDest,w,7)); w.hud[43]={};
        p.source = std::make_shared<const MapPointer>(src);
        p.outside = true; p.screen = false; p.targetIdentity = 0;
        p.click.over = p.click.loc = proto::enc::nullRef(); p.click.control.clear();
        expected.over = expected.overLoc = proto::enc::nullRef(); expected.overControl.clear();
        expected.overControlNull = expected.overLocBlank = true;
        // Outside every window the drop carries the press's cell without icon-x/icon-y (stock, dragout-world).
        expected.params = proto::enc::clickParams(p.click);
        expected.params.erase(0, expected.params.find(p.click.button + "=1"));
        QVERIFY(mapPointerCurrent(p,w,7));
        QCOMPARE(mapPointerBody(p),proto::enc::encodeDragCmd(expected));
        auto surface = std::make_shared<const uint8_t>(0);
        src.surface = surface; p.source = std::make_shared<const MapPointer>(src);
        QVERIFY(mapPointerCurrent(p,w,7)); surface.reset();
        QVERIFY(!mapPointerCurrent(p,w,7)); // queued gesture after skin/MAP destruction
    }
    void coordinates() {
        MapGeometry geometry{QRect(10, 20, 6, 6), QSize(100, 100), 32, 3, 17};
        // Two tiles in a 200x100 control: 50 px horizontal letterbox on either side.
        auto p = mapPointerAt({50, 0}, {200, 100}, {2, 2}, QRect(64, 64, 64, 64), geometry, "world.map", true);
        QVERIFY(p);
        QCOMPARE(p->click.over.kind, uint8_t(proto::enc::RefTurf));
        QCOMPARE(p->click.over.x, uint16_t(12));
        QCOMPARE(p->click.over.y, uint16_t(23));
        QCOMPARE(p->click.iconX, 1);
        QCOMPARE(p->click.iconY, 32);
        QCOMPARE(p->click.screenX, 1);
        QCOMPARE(p->click.screenY, 2);
        QCOMPARE(p->z, 3);
        QCOMPARE(p->session, uint64_t(17));
        auto glide = mapPointerAt({50, 0}, {200, 100}, {2, 2}, QRect(48, 80, 64, 64), geometry, "world.map", true);
        QCOMPARE(glide->click.over.x, uint16_t(11));
        QCOMPARE(glide->click.over.y, uint16_t(23));
        QCOMPARE(glide->click.iconX, 17);
        QCOMPARE(glide->click.iconY, 16);
        QCOMPARE(glide->click.screenPX, 1); // screen pixel and icon pixel differ while gliding
        QCOMPARE(glide->click.screenPY, 32);
        auto scaled = mapPointerAt({100, 0}, {400, 200}, {2, 2}, QRect(48, 80, 64, 64), geometry, "world.map", true);
        QCOMPARE(proto::enc::clickParams(scaled->click), proto::enc::clickParams(glide->click));
        auto box = mapPointerAt({49, 50}, {200, 100}, {2, 2}, QRect(64, 64, 64, 64), geometry, "world.map", true);
        QCOMPARE(box->click.over.kind, uint8_t(proto::enc::RefNull));
        QVERIFY(!mapPointerAt({}, {}, {}, {}, {}, "world.map", true));
        auto large = mapPointerAt({63, 0}, {64, 64}, {1, 1}, QRect(0, 0, 64, 64),
                                  {QRect(0, 0, 1, 1), QSize(10, 10), 64, 0, 1}, "world.map", true);
        QVERIFY(large);
        QVERIFY(QString::fromStdString(proto::enc::clickParams(large->click)).contains("screen-loc=1:64,1:64"));
    }
    void encodingAndSubscriptions() {
        auto p = mapPointerAt({10, 10}, {64, 64}, {2, 2}, QRect(0, 0, 64, 64),
                              {QRect(0, 0, 2, 2), QSize(10, 10), 32, 0, 1}, "world.camera", false);
        QVERIFY(p);
        QCOMPARE(p->click.control, std::string("world.camera"));
        QCOMPARE(p->click.locStr, std::string("camera"));
        p->click.mods = {true, true, true};
        p->click.button = "right";
        p->kind = MapPointer::Down;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseDown);
        p->kind = MapPointer::Up;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseUp);
        p->kind = MapPointer::DoubleClick;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseDblClick);
        p->kind = MapPointer::Click;
        auto bytes = mapPointerBody(*p);
        QByteArray packet(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        // A click names a second map by its full id; a press by its short one (stock, secmapmouse-world).
        QVERIFY(packet.contains("right=1;button=right;ctrl=1;shift=1;alt=1;screen-loc=world.camera:"));
        p->kind = MapPointer::Down; bytes = mapPointerBody(*p);
        QVERIFY(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).contains("screen-loc=camera:"));
        p->kind = MapPointer::Click;
        p->kind = MapPointer::Wheel; p->dx = -1; p->dy = 1; p->click.button.clear();
        bytes = mapPointerBody(*p);
        QCOMPARE(bytes[0], proto::enc::kMouseExt);
        QCOMPARE(bytes[2], uint8_t(0x0a));
        packet = QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        QVERIFY(packet.contains("screen-loc=camera:"));
        QVERIFY(!packet.contains("button="));
        proto::enc::MouseHooks hooks;
        QVERIFY(!mapPointerWanted(MapPointer::Click, hooks, false));
        hooks.click = true;
        QVERIFY(mapPointerWanted(MapPointer::Click, hooks, false));
        QVERIFY(!mapPointerWanted(MapPointer::Down, hooks, false));
        QVERIFY(!mapPointerWanted(MapPointer::Wheel, hooks, false));
        QVERIFY(mapPointerWanted(MapPointer::Wheel, hooks, true));
    }
};
FUGUE_TEST_GUILESS_MAIN(MapInputTest)
#include "mapinput_test.moc"
