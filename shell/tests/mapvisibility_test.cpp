#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QDataStream>
#include <QSet>
#include <bit>
#include <utility>
#include "shell/entitymap.h"
#include "shell/tests/affinefixture.h"
using namespace fugue;
namespace {
model::World world(int view=9) {
    model::World w;
    w.bounds={true,11,11,0,view,view,view,view,view,view}; w.resync.haveOrigin=true;
    w.haveEye=w.haveSight=w.haveSeeInDark=true; w.eyeX=5; w.eyeY=6;
    for (uint32_t id=1;id<=4;++id) {
        auto& a=w.appearances[id]; setFields(a, "{}"); setOpaque(a, id==2);
        a.fields.iconResID=0; a.fields.layerBits=std::bit_cast<uint32_t>(id<=2 ? 2.f : 3.f);
    }
    setLuminosity(w.appearances[100], 1);
    w.turfBinds[0]=100;
    for (int z=0;z<2;++z) for (int y=0;y<11;++y) for (int x=0;x<11;++x) {
        const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),uint16_t(z)};
        w.tiles[cell]=1; w.areaCells[cell]=0;
    }
    w.resources={{123,0}};
    return w;
}
void wall(model::World& w) { for (int y=0;y<11;++y) w.tiles[{5,uint16_t(y),0}]=2; }
void atom(model::World& w,uint32_t key,int x,int y,uint32_t appearance=3) {
    auto& a=w.atoms[key]; a.hasPos=true; a.x=x; a.y=y; a.appid=appearance;
    a.haveKey=true; a.key=uint16_t(key); a.renderIdentity=key+1;
    auto& cells=key&model::kRegistryA ? w.cellMobs : w.cellObjs;
    cells[{x,y,0}].push_back(key&~model::kRegistryA);
}
shell::TurfMap::Read reader() {
    QImage image(32,32,QImage::Format_RGBA8888); image.fill(Qt::red);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG");
    return [bytes](uint32_t,QByteArray& out) { out=bytes;return true; };
}
bool has(const shell::MapSprites& sprites,uint32_t key) {
    return std::any_of(sprites.begin(),sprites.end(),[&](const auto& s) { return s.kind==shell::MapSprite::Entity && s.atom==key; });
}
}
class MapVisibilityTest : public QObject {
    Q_OBJECT
    bool darknessDataReady_ = false;
private slots:
    void wireScalarsAndPresence() {
        model::Session session;
        auto u32=[](Bytes& b,uint32_t v) { for(int i=0;i<4;++i) b.push_back(uint8_t(v>>(8*i))); };
        auto u16=[](Bytes& b,uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v>>8)); };
        for (uint32_t flags : {4u,5u,6u,7u}) {
            Bytes b; u32(b,1); b.insert(b.end(),2,0); u32(b,0); b.insert(b.end(),3,0);
            u16(b,32); u16(b,32); u16(b,0); u16(b,0); b.push_back(2); u32(b,flags);
            b.insert(b.end(),3,0); u32(b,0xffff); u16(b,0); u16(b,0); u16(b,0); u32(b,0x40000000); b.insert(b.end(),2,0);
            session.ingest(0x26,b); QVERIFY(session.world().appearances.count(1));
            const auto& a=session.world().appearances.at(1); QCOMPARE(a.opaque(),bool(flags&1));
        }
        auto vision=[&](Bytes b) { b.insert(b.end(),10,0); session.ingest(7,b); };
        vision({0x20,8}); QVERIFY(session.world().haveSight); QCOMPARE(session.world().sight,8u);
        QVERIFY(session.world().haveNarrowSight); QCOMPARE(session.world().narrowSight,8u);
        QVERIFY(!session.world().haveWideSight);
        vision({0x80,4,0,2,0,0}); QCOMPARE(session.world().sight,512u);
        QVERIFY(session.world().haveWideSight); QCOMPARE(session.world().wideSight,512u);
        QVERIFY(session.world().haveNarrowSight); QCOMPARE(session.world().narrowSight,8u); // presence retained
        vision({0x80,2,50}); QCOMPARE(session.world().seeInvisible,uint8_t(50)); QCOMPARE(session.world().sight,512u);
        vision({0x80,1,2}); QCOMPARE(session.world().seeInvisible,uint8_t(50)); QCOMPARE(session.world().sight,512u);
        vision({0x20,32}); QCOMPARE(session.world().sight,32u);
        vision({0x20,0}); QCOMPARE(session.world().sight,0u); QCOMPARE(session.world().seeInvisible,uint8_t(50));
    }
    void darknessWireScalars() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/darkness-world/wire-scalars.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto data=document.object();
        const auto appearances=data["appearances"].toArray(),vision=data["vision"].toArray();
        const auto bounds=data["bounds"].toArray(),rebinds=data["area_rebinds"].toArray();
        QCOMPARE(appearances.size(),9);QCOMPARE(vision.size(),4);QCOMPARE(bounds.size(),2);QCOMPARE(rebinds.size(),2);
        model::Session session;
        auto ingest=[&](uint16_t type,const QJsonObject& row) {
            const auto bytes=hexDecode(row["body"].toString().toStdString());
            if (!bytes || bytes->empty()) {error=path+": missing or malformed frame body";return false;}
            const auto before=session.ingestErrors();session.ingest(type,*bytes);
            if (session.ingestErrors()!=before) {error=path+": production decoder rejected frame "+QString::number(type,16);return false;}
            return true;
        };
        QSet<int> ids;
        for (const auto& value:appearances) {
            const auto row=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["id"]},1,1,15),qPrintable(path+": invalid appearance id"));
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["luminosity"]},1,0,6),qPrintable(path+": invalid luminosity expectation"));
            const int id=row["id"].toInt();QVERIFY(!ids.contains(id));ids.insert(id);
            QVERIFY2(ingest(0x26,row),qPrintable(error));
            const auto found=session.world().appearances.find(uint32_t(id));
            QVERIFY2(found!=session.world().appearances.end(),qPrintable(path+": missing decoded appearance "+QString::number(id)));
            const auto& a=found->second;
            QCOMPARE(a.luminosity(),row["luminosity"].toInt());
        }
        QCOMPARE(ids,(QSet<int>{1,2,8,10,11,12,13,14,15}));
        QVERIFY(!session.world().haveSeeInDark);
        QSet<int> sightValues;
        for (const auto& value:vision) {
            const auto row=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["see"]},1,0,6),qPrintable(path+": invalid see_in_dark expectation"));
            sightValues.insert(row["see"].toInt());
            QVERIFY2(ingest(7,row),qPrintable(error));
            QVERIFY(session.world().haveSeeInDark); QCOMPARE(int(session.world().seeInDark),row["see"].toInt());
        }
        QCOMPARE(sightValues,(QSet<int>{0,1,2,6}));
        // An unrelated optional update does not reset dark vision or its presence.
        Bytes invisible{0x80,2,50}; invisible.insert(invisible.end(),10,0); session.ingest(7,invisible);
        QVERIFY(session.world().haveSeeInDark); QCOMPARE(session.world().seeInDark,uint8_t(0));
        QSet<int> widths;
        for (const auto& value:bounds) {
            const auto row=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["win"],row["light"],row["view"]},3,1,65535),qPrintable(path+": invalid bounds expectation"));
            widths.insert(row["win"].toInt());
            QVERIFY2(ingest(0x3e,row),qPrintable(error));
            QVERIFY(session.world().bounds.have);
            QCOMPARE(session.world().bounds.winW,row["win"].toInt());
            QCOMPARE(session.world().bounds.lightW,row["light"].toInt());
            QCOMPARE(session.world().bounds.lightH,row["light"].toInt());
            QCOMPARE(session.world().bounds.viewW,row["view"].toInt());
        }
        QCOMPARE(widths,(QSet<int>{19,21}));
        session.worldMut().tiles[{0,0,0}]=2;
        QSet<int> appids;
        for (const auto& value:rebinds) {
            const auto row=value.toObject();
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["appid"]},1,1,15),qPrintable(path+": invalid area appearance expectation"));
            appids.insert(row["appid"].toInt());
            QVERIFY2(ingest(0xf1,row),qPrintable(error));
            const auto binding=session.world().turfBinds.find(0);QVERIFY(binding!=session.world().turfBinds.end());
            QCOMPARE(binding->second,uint32_t(row["appid"].toInt()));
            const auto tile=session.world().tiles.find({0,0,0});QVERIFY(tile!=session.world().tiles.end());
            QCOMPARE(tile->second,2u); // binding-only delta, no tile replacement
        }
        QCOMPARE(appids,(QSet<int>{1,8}));
        QCOMPARE(appearances.last().toObject()["id"].toInt(),15);
        auto unsupported=hexDecode(appearances.last().toObject()["body"].toString().toStdString());
        QVERIFY(unsupported && !unsupported->empty()); unsupported->back()=7; session.ingest(0x26,*unsupported);
        QVERIFY(session.world().appearances.count(15));
        QCOMPARE(session.world().appearances.at(15).luminosity(),-1); // do not approximate unverified values
        model::Session replacement; QVERIFY(!replacement.world().haveSeeInDark);
    }
    void stockDarkness_data() {
        darknessDataReady_ = false;
        QTest::addColumn<QJsonObject>("row");
        struct Recording {const char* name;int cases,size;};
        const Recording recordings[]={{"stock-observations.json",13,9},{"extended-observations.json",12,9},
            {"source-window-observations.json",7,17}};
        const QRegularExpression mask("^(?:[01W]{9}/){8}[01W]{9}\\z"),integer("^-?[0-9]+\\z");
        const QStringList numeric{"ambient","see","wall","wallx","lamp","lampx","lampy","lamp_invisible","eyex","eyey","offset"};
        const QStringList positions{"wallx","lampx","lampy","eyex","eyey"};
        const QStringList sights{"normal","blackness","turfs","objs","ghost","through","self"};
        QJsonArray retained;QSet<QString> names;
        for (const auto& recording:recordings) {
            const QString path=QString(FUGUE_TEST_DATA_DIR)+"/darkness-world/"+recording.name;
            QJsonDocument document;QString error;
            QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
            const auto data=document.object();const auto size=data["world_size"];
            // The first two recordings deliberately omit world_size (the 9x9 default).
            if (data.contains("world_size") || recording.size!=9)
                QVERIFY2(testinput::isIntegerTuple(QJsonArray{size},1,recording.size,recording.size),qPrintable(path+": invalid world_size"));
            const auto cases=data["cases"].toArray();QCOMPARE(cases.size(),recording.cases);
            for (const auto& value:cases) {
                auto row=value.toObject();const auto name=row["phase"].toString();
                const auto context=path+": "+name;
                QVERIFY2(!name.isEmpty() && name==name.trimmed() && !names.contains(name),qPrintable(context+": missing or duplicate phase"));
                names.insert(name);
                QVERIFY2(mask.match(row["mask"].toString()).hasMatch(),qPrintable(context+": expected a 9x9 mask of 0, 1 or W"));
                QVERIFY2(row["visible_markers"].isArray(),qPrintable(context+": missing visible_markers array"));
                QSet<QString> markers;
                for (const auto& marker:row["visible_markers"].toArray()) {
                    const auto text=marker.toString();
                    QVERIFY2((text=="target" || text=="lamp" || text=="observer") && !markers.contains(text),qPrintable(context+": invalid or duplicate marker"));
                    markers.insert(text);
                }
                const auto config=row["config"].toString();QVERIFY2(!config.isEmpty(),qPrintable(context+": missing config"));
                const QUrlQuery query(config);QSet<QString> keys;
                for (const auto& item:query.queryItems()) {
                    const auto& key=item.first;const auto& text=item.second;
                    QVERIFY2(!keys.contains(key) && !text.isEmpty(),qPrintable(context+": duplicate or empty query item "+key));
                    keys.insert(key);
                    if (key=="sight") {QVERIFY2(sights.contains(text),qPrintable(context+": unsupported sight"));}
                    else if (key=="door") {QVERIFY2(text=="open" || text=="closed",qPrintable(context+": invalid door"));}
                    else if (key!="run") {
                        bool valid=false;const int number=text.toInt(&valid);
                        QVERIFY2(numeric.contains(key) && valid && integer.match(text).hasMatch(),qPrintable(context+": invalid numeric query item "+key));
                        const int low=positions.contains(key) ? 1 : key=="offset" ? -32768 : 0;
                        const int high=positions.contains(key) ? recording.size : key=="offset" ? 32767 :
                            key=="ambient" || key=="wall" || key=="lamp_invisible" ? 1 : 6;
                        QVERIFY2(number>=low && number<=high,qPrintable(context+": query item outside fixture range: "+key));
                    }
                }
                QVERIFY2(keys.contains("ambient") && keys.contains("see") && keys.contains("sight"),qPrintable(context+": missing ambient, see or sight input"));
                row["world_size"]=recording.size;retained.append(row);
            }
        }
        for (const auto& value:retained) {
            const auto row=value.toObject();QTest::newRow(qPrintable(row["phase"].toString())) << row;
        }
        darknessDataReady_ = true;
    }
    void stockDarkness() {
        // QtTest can still enter the body after a failing _data() function.
        QVERIFY2(darknessDataReady_,"No validated darkness observations");
        QFETCH(QJsonObject,row);
        const QUrlQuery p(row["config"].toString());
        auto number=[&](const char* key,int fallback) { return p.hasQueryItem(key) ? p.queryItemValue(key).toInt() : fallback; };
        const int size=row["world_size"].toInt();
        auto w=world(); w.bounds={true,size,size,4,21,21,9,9,13,13};
        w.eyeX=number("eyex",5); w.eyeY=number("eyey",5);
        for (int y=0;y<size;++y) for (int x=0;x<size;++x) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),0}; w.tiles[cell]=1; w.areaCells[cell]=0;
        }
        w.seeInDark=uint8_t(number("see",0)); setLuminosity(w.appearances[100], uint8_t(number("ambient",1)));
        const QString sight=p.queryItemValue("sight");
        w.sight=sight=="blackness" ? 0x400u : sight=="objs" ? 8u : sight=="turfs" ? 16u : sight=="ghost" ? 28u :
            sight=="through" ? 512u : sight=="self" ? 32u : 0u;
        if (number("wall",0)) for (int y=0;y<size;++y)
            if (!(p.queryItemValue("door")=="open" && y==4)) w.tiles[{uint16_t(number("wallx",6)-1),uint16_t(y),0}]=2;
        w.appearances[10]=w.appearances[3]; setLuminosity(w.appearances[10], uint8_t(number("lamp",0)));
        setFields(w.appearances[3], "{\"pixelX\":"+std::to_string(number("offset",0))+"}");
        if (number("lamp_invisible",0)) setFields(w.appearances[10], R"({"vars":[{"bit":4,"data":"32"}]})");
        atom(w,1,8,5,3); atom(w,2,number("lampx",3),number("lampy",5),10); atom(w,model::kRegistryA|3,w.eyeX,w.eyeY,4);
        w.haveSelf=true; w.selfRef=3;
        shell::TurfMap terrain; shell::EntityMap entities; shell::MapVisibility visibility;
        auto map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
        const auto sprites=entities.prepare(w,map,reader(),0,nullptr,true);
        const auto rows=row["mask"].toString().split('/'); QCOMPARE(rows.size(),9);
        for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
            const bool expected=rows[8-y][x]!='0';
            const bool drawn=std::any_of(sprites.begin(),sprites.end(),[&](const auto& s) {
                return s.kind==shell::MapSprite::Turf && s.cell==QPoint(x+w.eyeX-4,y+w.eyeY-4);
            });
            QVERIFY2(drawn==expected,qPrintable(QString("tile (%1,%2): drawn=%3 expected=%4").arg(x+1).arg(y+1).arg(drawn).arg(expected)));
        }
        const auto markers=row["visible_markers"].toArray();
        QCOMPARE(has(sprites,1),markers.contains("target"));
        QCOMPARE(has(sprites,2),markers.contains("lamp"));
        QCOMPARE(has(sprites,model::kRegistryA|3),markers.contains("observer"));
        QCOMPARE(visibility.prepare(w),map.geometry.visibility); QVERIFY(visibility.reused());
    }
    void lightingWindowUnknownAndInvalidation() {
        auto w=world(); w.bounds={true,17,17,4,21,21,9,9,13,13}; w.eyeX=w.eyeY=9;
        setLuminosity(w.appearances[100], 0);
        for (int y=0;y<17;++y) for (int x=0;x<17;++x) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),0}; w.tiles[cell]=1; w.areaCells[cell]=0;
        }
        setLuminosity(w.appearances[3], 6); atom(w,1,4,9);
        shell::MapVisibility visibility;
        auto count=[&](const auto& f) { return std::count(f->outcomes.begin(),f->outcomes.end(),uint8_t(shell::VisibilityOutcome::Visible)); };
        const auto first=visibility.prepare(w); QCOMPARE(count(first),45);
        w.cellObjs.clear(); atom(w,1,3,9); QCOMPARE(count(visibility.prepare(w)),36);
        w.cellObjs.clear(); atom(w,1,2,9); QCOMPARE(count(visibility.prepare(w)),0); // in buffer, outside source window
        w.bounds.lightW=15; QCOMPARE(count(visibility.prepare(w)),27); QVERIFY(!visibility.reused());
        QCOMPARE(count(first),45); // immutable previous illumination
        w.resync.freed.insert(1); QCOMPARE(count(visibility.prepare(w)),0);
        w.resync.freed.clear(); w.cellObjs.clear(); w.atoms.clear();
        w.areaCells.erase({8,8,0}); QCOMPARE(visibility.prepare(w)->cell(8,8),shell::VisibilityOutcome::Unknown);
        w.areaCells[{8,8,0}]=0; QCOMPARE(visibility.prepare(w)->cell(8,8),shell::VisibilityOutcome::Hidden);
        setLuminosity(w.appearances[100], 1); QCOMPARE(count(visibility.prepare(w)),81);
        w.haveSeeInDark=false; QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Unknown);
        w.haveSeeInDark=true; setLuminosity(w.appearances[100], 0);
        setLuminosity(w.appearances[3], -1); atom(w,1,9,9);
        QCOMPARE(visibility.prepare(w)->cell(8,8),shell::VisibilityOutcome::Unknown);
        setLuminosity(w.appearances[3], 1);
        QCOMPARE(visibility.prepare(w)->cell(8,8),shell::VisibilityOutcome::Visible);
        // Stale/wrong-position entries and exhausted traversal cannot become clear lights.
        w.atoms[1].x=10; QCOMPARE(visibility.prepare(w)->cell(8,8),shell::VisibilityOutcome::Unknown);
        w.cellObjs.clear(); w.cellObjs[{3,3,0}].resize(shell::MapVisibility::maxVisits+1,999);
        auto budget=visibility.prepare(w); QVERIFY(budget->visits<=shell::MapVisibility::maxVisits);
        QVERIFY(budget->cell(8,8)!=shell::VisibilityOutcome::Visible);
    }
    void lightAnchorAndBlacknessSpill() {
        auto w=world(); setLuminosity(w.appearances[100], 0); w.seeInDark=2;
        shell::MapVisibility visibility;
        const QRect eyePixels(4*32,-6*32,32,32);
        auto f=visibility.prepare(w);
        QCOMPARE(f->test(eyePixels,shell::VisibilityFrame::Object,{7,5}),shell::VisibilityOutcome::Hidden);
        const QRect spilling=eyePixels.adjusted(0,0,64,0);
        QCOMPARE(f->test(spilling,shell::VisibilityFrame::Object,{4,5}),shell::VisibilityOutcome::Visible);
        w.sight=0x400; f=visibility.prepare(w);
        QCOMPARE(f->test(spilling,shell::VisibilityFrame::Object,{4,5}),shell::VisibilityOutcome::Visible);
        QCOMPARE(f->test(eyePixels,shell::VisibilityFrame::Object,{4,5}),shell::VisibilityOutcome::Visible);
        QCOMPARE(f->test(eyePixels,shell::VisibilityFrame::Object,{7,5}),shell::VisibilityOutcome::Hidden);
        QVERIFY(f->blacknessReady); QVERIFY(!f->blackness.empty());
        auto covered=[](const auto& frame,QPoint p) {
            return std::any_of(frame->blackness.begin(),frame->blackness.end(),[&](QRect r) { return r.contains(p); });
        };
        QVERIFY(!covered(f,{4,5})); QVERIFY(covered(f,{7,5}));
        const auto previous=f;
        // Category and self bypass admission, not coverage. All three categories
        // together suppress coverage, including in the fully dark stock control.
        w.seeInDark=0;
        for (uint32_t bypass:{4u,8u,16u,12u,20u,24u,32u}) {
            w.sight=0x400u|bypass; f=visibility.prepare(w);
            QVERIFY(f->blacknessReady); QVERIFY(covered(f,{4,5}));
            QCOMPARE(f->test(eyePixels,shell::VisibilityFrame::Category(bypass),{4,5}),shell::VisibilityOutcome::Sight);
        }
        w.sight=0x41c; f=visibility.prepare(w); QVERIFY(!f->hasBlackness()); QVERIFY(f->blackness.empty());
        w.sight=0x400; setLuminosity(w.appearances[100], 1);
        for (int y=0;y<11;++y) w.tiles[{6,uint16_t(y),0}]=2;
        f=visibility.prepare(w); QVERIFY(f->blacknessReady);
        QVERIFY(!covered(f,{6,5})); QVERIFY(covered(f,{7,5})); // lit but geometrically hidden
        QVERIFY(!covered(previous,{4,5})); QVERIFY(covered(previous,{7,5})); // immutable
        // No guessed covering pass for incomplete fields or unsupported sight.
        w.tiles.erase({4,5,0}); f=visibility.prepare(w);
        QVERIFY(!f->blacknessReady); QVERIFY(f->blackness.empty());
        QCOMPARE(f->test(spilling,shell::VisibilityFrame::Object,{4,5}),shell::VisibilityOutcome::Unsupported);
        w.sight=0x401; QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Unsupported); // BLIND
    }
    // The retained stock comparisons found no >8-level pixel differences from plain sight.
    void infraAndPixelSightDrawLikePlain() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/sight-flags-world/stock.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto sets=document.object()["sets"].toObject();
        QCOMPARE(sets.keys(),(QStringList{"blind","infra","pixels","plain"}));
        const std::pair<const char*,int> flags[]={{"plain",0},{"blind",1},{"infra",64},{"pixels",256}};
        for (const auto& [name,sight]:flags) {
            const auto row=sets[name].toObject();const auto context=path+": "+name;
            QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["sight"]},1,sight,sight),qPrintable(context+": invalid sight input"));
            if (sight==1) {
                QVERIFY2(row["allBlack"].isBool(),qPrintable(context+": missing allBlack expectation"));
                QVERIFY(row["allBlack"].toBool());
            } else {
                QVERIFY2(testinput::isIntegerTuple(QJsonArray{row["mapDiffOver8"]},1,0,0),qPrintable(context+": missing zero mapDiffOver8 expectation"));
            }
        }
        auto w=world(11); setLuminosity(w.appearances[100], 0); w.seeInDark=0;
        for (int y=0;y<11;++y) w.tiles[{6,uint16_t(y),0}]=2; // an opaque column
        shell::MapVisibility plainVisibility; const auto plain=plainVisibility.prepare(w);
        QCOMPARE(plain->status,shell::VisibilityOutcome::Visible);
        for (const char* name:{"infra","pixels"}) {
            const auto stock=sets[name].toObject();
            w.sight=uint32_t(stock["sight"].toInt());
            shell::MapVisibility visibility; const auto f=visibility.prepare(w);
            QCOMPARE(f->status,shell::VisibilityOutcome::Visible);
            QCOMPARE(f->outcomes,plain->outcomes); QCOMPARE(f->lighting,plain->lighting);
        }
    }
    void denseLightingIsBoundedAndReused() {
        auto w=world(129); w.bounds={true,129,129,64,139,139,129,129,139,139}; w.eyeX=w.eyeY=65;
        setLuminosity(w.appearances[100], 0); setLuminosity(w.appearances[1], 6);
        for (int y=0;y<129;++y) for (int x=0;x<129;++x) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),0}; w.tiles[cell]=1; w.areaCells[cell]=0;
        }
        shell::MapVisibility visibility; const auto f=visibility.prepare(w);
        QCOMPARE(f->status,shell::VisibilityOutcome::Visible); QVERIFY(f->visits<=shell::MapVisibility::maxVisits);
        QVERIFY(f->lighting.size()<=size_t(139*139)); QCOMPARE(f->outcomes.size(),size_t(129*129));
        QCOMPARE(std::count(f->outcomes.begin(),f->outcomes.end(),uint8_t(shell::VisibilityOutcome::Visible)),129*129);
        QCOMPARE(visibility.prepare(w),f); QVERIFY(visibility.reused());
    }
    void columnInputsReuseAndMutation() {
        // Dense shared roots, sparse requested-level cells and long runs of other
        // Z levels exercise both cursor advancement and its bounded seek fallback.
        auto w=world(15); w.bounds={true,19,23,0,25,27,15,15,19,19};
        w.eyeX=10; w.eyeY=12; w.viewZ=7;
        w.tiles.clear(); w.areaCells.clear();
        for (int x=0;x<19;++x) for (int y=0;y<23;++y) for (int z=0;z<20;++z) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),uint16_t(z)};
            if (z!=7 || (x*17+y*13)%7) w.tiles[cell]=(x+y)%5 ? 1u : 2u;
            if (z!=7 || (x+y)%11) w.areaCells[cell]=uint32_t((x+y)%3);
        }
        w.turfBinds[1]=101; w.turfBinds[2]=102; // missing bindings/appearances are inputs too
        w.appearances[101]=w.appearances[100]; setLuminosity(w.appearances[101], 0);
        for (uint32_t id=1;id<=60;++id) {
            const auto key=id | (id%2 ? model::kRegistryA : 0u);
            atom(w,key,int(id%17)+1,int(id%21)+1,id%4+1);
            w.atoms[key].z=7;
            auto& cells=id%2 ? w.cellMobs : w.cellObjs;
            cells[{w.atoms[key].x,w.atoms[key].y,7}].push_back(id);
        }
        // These cells must not leak into neighboring columns or the requested Z.
        for (int z=0;z<20;++z) if (z!=7) w.cellObjs[{8,9,z}]={999};
        w.cellObjs[{8,10000,7}]={999};
        shell::MapVisibility visibility;
        QRect displayed;
        QCryptographicHash sequence(QCryptographicHash::Sha256);
        auto bytes=[](const shell::VisibilityFrame& f) {
            QByteArray result; QDataStream out(&result,QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0);
            out << f.cells << f.eye << f.z << f.tile << f.sight << f.seeInDark << int(f.status) << f.lightCells;
            auto values=[&](const auto& list) { out << quint64(list.size()); for (auto v:list) out << quint32(v); };
            values(f.opacity); values(f.outcomes); values(f.lighting);
            for (const auto& sum:f.sums) values(sum);
            out << quint64(f.blackness.size()); for (const auto& run:f.blackness) out << run;
            out << f.blacknessReady << quint64(f.visits) << quint64(f.unknownCells) << quint64(f.deferredCells);
            return result;
        };
        std::shared_ptr<const shell::VisibilityFrame> previous;
        QByteArray previousBytes;
        auto check=[&] {
            const auto f=visibility.prepare(w,displayed);
            if (previous) QCOMPARE(bytes(*previous),previousBytes);
            const auto saved=bytes(*f);
            shell::MapVisibility fresh;
            QCOMPARE(bytes(*fresh.prepare(w,displayed)),saved);
            QCOMPARE(visibility.prepare(w,displayed),f); QVERIFY(visibility.reused());
            QCOMPARE(bytes(*f),saved); // immutable published field
            sequence.addData(saved); previous=f; previousBytes=saved;
        };
        check();
        setOpaque(w.appearances[1], true); check();
        setOpaque(w.appearances[1], false); check();
        setLuminosity(w.appearances[1], 4); check();
        setLuminosity(w.appearances[1], -1); check();
        w.appearances.erase(1); check();
        w.appearances[1]=w.appearances[3]; check(); // arrival after cached absence
        w.appearances[102]=w.appearances[100]; check();
        w.turfBinds[1]=100; check(); // binding-only ambient change
        w.turfBinds.erase(0); check();
        w.areaCells[{9,11,7}]=2; w.tiles[{9,11,7}]=3; check();
        w.tiles.erase({9,11,7}); w.areaCells.erase({9,12,7}); check();
        w.atoms[2].appid=1; w.atoms[2].x=9; check(); // stale cell list
        w.cellObjs.clear(); w.cellObjs[{9,w.atoms[2].y,7}]={2}; check(); // relink
        w.resync.freed.insert(2); check();
        w.resync.freed.clear(); w.atoms.erase(2); check();
        w.cellMobs.clear(); check();
        for (uint32_t sight:{0u,4u,8u,16u,32u,0x200u,0x400u,0x41cu}) {
            w.sight=sight; w.seeInDark=3; check();
        }
        w.sight=0; w.eyeX=1; w.eyeY=1; check(); // negative world edge
        displayed=QRect(-8,-8,18,18); check(); // gliding viewport union
        w.bounds.lightW=13; w.bounds.lightH=11; check();
        w.resync.originX=2; w.resync.originY=3; check();
        w.viewZ=8; check();
        w.bounds.viewW=9; w.bounds.viewH=7; displayed={}; check();
        w.eyeX=10; w.eyeY=12;
        w.cellObjs[{4,8,8}].resize(shell::MapVisibility::maxVisits+1,999); check();
        w.cellObjs.clear(); check(); // exhausted work must not poison the next attempt
        w.haveSight=false; check();
        w.haveSight=true; w.sight=1; check();
        w.sight=0; w.bounds.viewW=130; check();
        w=world(); check(); // complete replacement, including reused appearance IDs
        // Exact fields, lighting, summed-area admission and blackness from the
        // pre-optimization collector, over the entire mutation sequence above.
        QCOMPARE(sequence.result().toHex(),QByteArray("24d0c8d4f990f381a3b35f73f29d2f1789041af4afd625e9f7f635721dfac574"));
    }
    void stockFields() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/visibility-world/stock-masks.json";
        QJsonDocument document;QString error;
        QVERIFY2(testinput::readRecordedJson(path,document,error),qPrintable(error));
        const auto cases=document.object()["cases"].toArray();QCOMPARE(cases.size(),80);
        const QRegularExpression mask("^(?:[01]{11}/){11}\\z");
        QSet<QString> layouts;int index=0;
        for (const auto& value:cases) {
            auto w=world(11); shell::MapVisibility visibility;
            const auto row=value.toObject();const auto context=path+": case "+QString::number(++index);
            const auto blocks=row["blocks"].toArray();
            QVERIFY2(blocks.size()>=1 && blocks.size()<=24,qPrintable(context+": expected 1 to 24 blocks"));
            QSet<QString> points;
            for (const auto& b:blocks) {
                QVERIFY2(testinput::isIntegerTuple(b,2,2,10),qPrintable(context+": invalid interior block coordinate"));
                const auto p=b.toArray();const auto point=QString("%1,%2").arg(p[0].toInt()).arg(p[1].toInt());
                QVERIFY2(!points.contains(point),qPrintable(context+": duplicate block"));points.insert(point);
            }
            QStringList sorted=points.values();sorted.sort();const auto layout=sorted.join(';');
            QVERIFY2(!layouts.contains(layout),qPrintable(context+": duplicate block layout"));layouts.insert(layout);
            QVERIFY2(mask.match(row["mask"].toString()).hasMatch(),qPrintable(context+": expected eleven binary 11-cell rows, each ending in /"));
            for (const auto& b:blocks) {
                const auto p=b.toArray(); w.tiles[{uint16_t(p[0].toInt()-1),uint16_t(p[1].toInt()-1),0}]=2;
            }
            const auto f=visibility.prepare(w);
            const auto rows=row["mask"].toString().split('/');
            // The retained view(5,camera), eye=(5,6) comparison covers world X 1..10.
            // Keep the eleventh mask column outside this supplementary server-view check.
            for (int y=0;y<11;++y) for (int x=0;x<10;++x) {
                const bool expected=rows[10-y][x]=='1';
                QCOMPARE(f->cell(x,y)==shell::VisibilityOutcome::Visible,expected);
            }
        }
    }
    void invalidationAndUnknown() {
        auto w=world(); wall(w); shell::MapVisibility visibility;
        auto f=visibility.prepare(w);
        QCOMPARE(f->cell(4,5),shell::VisibilityOutcome::Visible);
        QCOMPARE(f->cell(5,5),shell::VisibilityOutcome::Visible);
        QCOMPARE(f->cell(7,5),shell::VisibilityOutcome::Hidden);
        QCOMPARE(visibility.prepare(w),f); QVERIFY(visibility.reused());
        // No resources or clock are consulted; unrelated appearance changes reuse geometry.
        w.appearances[3].fields.iconState="different"; QCOMPARE(visibility.prepare(w),f);
        w.tiles[{5,5,0}]=1; auto door=visibility.prepare(w);
        QCOMPARE(door->cell(7,5),shell::VisibilityOutcome::Visible);
        QCOMPARE(f->cell(7,5),shell::VisibilityOutcome::Hidden); // immutable old frame
        w.tiles[{5,5,0}]=99; auto late=visibility.prepare(w);
        QCOMPARE(late->cell(7,5),shell::VisibilityOutcome::Unknown);
        QCOMPARE(late->cell(3,5),shell::VisibilityOutcome::Visible);
        w.appearances[99]=w.appearances[2]; QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Hidden);
        w.eyeX=8; QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Visible);
        w.viewZ=1; QCOMPARE(visibility.prepare(w)->cell(4,5),shell::VisibilityOutcome::Visible);
        w.bounds.viewW=5; QCOMPARE(visibility.prepare(w)->cells.width(),5);
        w.bounds.viewW=130; QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Budget);
        w.bounds.viewW=9; w.haveEye=false; QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Unknown);
        w.haveEye=true; w.sight=0x80000000; QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Unsupported);
        w=world(); QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Visible); // replacement/reconnect
    }
    void movableOpacityAndBudget() {
        auto w=world(); shell::MapVisibility visibility;
        atom(w,7,6,6,2);
        QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Hidden);
        setOpaque(w.appearances[2], false); QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Visible);
        setOpaque(w.appearances[2], true);
        setFields(w.appearances[2], "{\"hasColor\":true,\"colorA\":0}"); // visual alpha does not remove blockers
        QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Hidden);
        w.cellObjs.clear(); w.atoms[7].x=7; w.cellObjs[{7,6,0}]={7};
        QCOMPARE(visibility.prepare(w)->cell(6,5),shell::VisibilityOutcome::Visible);
        QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Hidden);
        w.resync.freed.insert(7); QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Visible);
        w.resync.freed.clear(); w.atoms.erase(7); QCOMPARE(visibility.prepare(w)->cell(7,5),shell::VisibilityOutcome::Unknown);
        w.cellObjs.clear(); w.cellObjs[{1,2,0}].resize(shell::MapVisibility::maxVisits+1,999);
        auto limited=visibility.prepare(w); QVERIFY(limited->visits<=shell::MapVisibility::maxVisits);
        QVERIFY(limited->cell(7,5)!=shell::VisibilityOutcome::Visible);
    }
    void glideViewportKeepsDisplayedTiles_data() {
        QTest::addColumn<int>("dx"); QTest::addColumn<int>("dy");
        for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) if (x || y)
            QTest::newRow(qPrintable(QString("%1,%2").arg(x).arg(y))) << x << y;
    }
    void glideViewportKeepsDisplayedTiles() {
        QFETCH(int,dx); QFETCH(int,dy);
        auto w=world(9);
        w.bounds={true,15,15,0,9,9,9,9,9,9};
        for (int z=0;z<2;++z) for (int y=0;y<15;++y) for (int x=0;x<15;++x) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),uint16_t(z)};
            w.tiles[cell]=1; w.areaCells[cell]=0;
        }
        // Opaque columns on both sides: the extra displayed tile is clear, but the
        // cells behind each wall must stay hidden through every glide direction.
        for (int y=0;y<15;++y) { w.tiles[{5,uint16_t(y),0}]=2; w.tiles[{9,uint16_t(y),0}]=2; }
        w.haveEye=true; w.eyeX=w.eyeY=8; w.haveSight=true; w.sight=0;
        const int64_t start=1'000'000'000;
        shell::TurfMap terrain; shell::EntityMap entities; shell::MapVisibility visibility;
        shell::TurfMap::Frame map; shell::MapSprites sprites;
        auto prepare=[&](int64_t now) {
            map=terrain.prepare(w,{},now,false);
            map.geometry.visibility=visibility.prepare(w,shell::MapVisibility::displayedCells(
                map.geometry.region,map.geometry.tile,map.source));
            map.geometry.session=1;
            sprites=entities.prepare(w,map,reader(),now,nullptr,true);
            return map.geometry.visibility;
        };
        auto displayed=[&] { return shell::MapVisibility::displayedCells(map.geometry.region,map.geometry.tile,map.source); };
        auto present=[&](int x,int y) {
            return std::any_of(sprites.begin(),sprites.end(),[&](const auto& s) {
                return s.kind==shell::MapSprite::Turf && s.cell==QPoint(x+1,y+1); });
        };
        auto check=[&](int64_t now) {
            const auto field=prepare(now);
            const QRect shown=displayed();
            QVERIFY(shown.width()>=9 && shown.height()>=9); // part of an extra tile is presented
            QVERIFY(field->cells.contains(shown.topLeft()));
            QVERIFY(field->cells.contains(shown.bottomRight()));
            int expected=0, turfs=0;
            for (int y=shown.top();y<=shown.bottom();++y) for (int x=shown.left();x<=shown.right();++x) {
                if (x<0||y<0||x>=w.bounds.maxX||y>=w.bounds.maxY) continue;
                const auto outcome=field->cell(x,y);
                if (outcome==shell::VisibilityOutcome::Visible) { ++expected; QVERIFY(present(x,y)); }
                else QVERIFY(!present(x,y));
            }
            for (const auto& s:sprites) if (s.kind==shell::MapSprite::Turf && shown.contains(s.cell-QPoint(1,1))) ++turfs;
            QCOMPARE(turfs,expected); // no black band: every presented in-world tile is drawn
            QCOMPARE(field->cell(4,7),shell::VisibilityOutcome::Hidden);
            QCOMPARE(field->cell(10,7),shell::VisibilityOutcome::Hidden);
            QVERIFY(!present(4,7)); QVERIFY(!present(10,7)); // wall occlusion survives the glide
            prepare(now); // same inputs, same presented viewport: warm reuse
            QVERIFY(visibility.reused());
            const auto again=entities.prepare(w,map,reader(),now,nullptr,true);
            QCOMPARE(sprites,again);
        };
        // Start, intermediate glide, completion, then an identical repeated move.
        for (int pass=0;pass<2;++pass) {
            const int64_t offset=start+pass*250'000'000;
            w.eyeGliding=true;
            w.eyeGlide=model::glideMotion(-dx,-dy,32,32,100'000'000,false,offset,false);
            for (int step=0;step<=4;++step) check(offset+w.eyeGlide.durNs*step/4);
            w.eyeGliding=false;
            check(offset+w.eyeGlide.durNs*2);
        }
        // Padded-region recentering after a plain eye jump stays fully admitted.
        w.eyeX=10; w.eyeY=10; w.eyeGliding=false;
        prepare(0); const auto recentred=map.geometry.visibility;
        const QRect shown=displayed();
        QVERIFY(shown==QRect(QPoint(5,5),QSize(9,9)));
        QVERIFY(recentred->cells.contains(shown.topLeft()));
        QVERIFY(recentred->cells.contains(shown.bottomRight()));
        for (int y=shown.top();y<=shown.bottom();++y) for (int x=shown.left();x<=shown.right();++x)
            if (recentred->cell(x,y)==shell::VisibilityOutcome::Visible) QVERIFY(present(x,y));
    }
    void blacknessSightIsOrdinaryLos() {
        auto w=world(); wall(w); atom(w,2,8,6); atom(w,model::kRegistryA|3,8,7); atom(w,1,4,6); shell::MapVisibility visibility;
        const auto check=[&](uint32_t sight) {
            w.sight=sight;
            shell::TurfMap terrain; shell::EntityMap entities;
            auto map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
            return entities.prepare(w,map,reader(),0,nullptr,true);
        };
        // SEE_INFRA and SEE_PIXELS draw like plain sight (sight-flags-world).
        for (uint32_t sight : {0u,0x400u,0x420u,0x40u,0x100u,0x500u}) {
            const auto s=check(sight);
            QVERIFY(has(s,1));
            QVERIFY(!has(s,2)); // a wall still hides what is behind it
            QVERIFY(!has(s,model::kRegistryA|3));
        }
        // Blind sight is still refused, not approximated.
        for (uint32_t sight : {1u,0x401u,0x101u}) {
            const auto s=check(sight);
            QVERIFY(!has(s,1)); QVERIFY(!has(s,2));
            QCOMPARE(visibility.prepare(w)->status,shell::VisibilityOutcome::Unsupported);
        }
        // Living and ghost states replace each other without reusing a stale field.
        w.sight=0x420u; const auto livingFrame=visibility.prepare(w);
        QCOMPARE(livingFrame->status,shell::VisibilityOutcome::Visible);
        const auto ghost=check(0x1cu); // SEE_MOBS|SEE_OBJS|SEE_TURFS
        QVERIFY(has(ghost,2)); QVERIFY(has(ghost,model::kRegistryA|3));
        const auto ghostFrame=visibility.prepare(w);
        QVERIFY(ghostFrame!=livingFrame);
        w.sight=0x420u;
        const auto backFrame=visibility.prepare(w);
        QVERIFY(!visibility.reused());
        QVERIFY(backFrame!=ghostFrame); // no stale permissive field is reused
        const auto back=check(0x420u);
        QVERIFY(!has(back,2)); QVERIFY(!has(back,model::kRegistryA|3));
        w.haveSelf=true; w.selfRef=3;
        const auto self=check(0x420u);
        QVERIFY(!has(self,2));
    }
    void transformedImageAdmissionAndAnchor() {
        auto w=world(); wall(w); atom(w,2,8,6);
        w.imageInstances[7]={4,2,1,false,1,w.atoms[2].renderIdentity};
        setFields(w.appearances[4], R"({"pixelX":-64})");
        shell::MapVisibility visibility; shell::TurfMap terrain; shell::EntityMap entities;
        auto prepare=[&]{auto map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
            return entities.prepare(w,map,reader(),0,nullptr,true);};
        auto images=[](const auto& sprites){return std::count_if(sprites.begin(),sprites.end(),
            [](const auto& s){return s.kind==shell::MapSprite::Image;});};
        setFields(w.appearances[3], affinefixture::fields({0,-1,0,1,0,0}));
        const auto visible=prepare(); QVERIFY(has(visible,2)); QCOMPARE(images(visible),1);
        setFields(w.appearances[3], affinefixture::fields({0,-1,32,1,0,0}));
        const auto hidden=prepare(); QVERIFY(!has(hidden,2)); QCOMPARE(images(hidden),0);
        setFields(w.appearances[3], affinefixture::fields({1,0,0,0,1,0}));
        QCOMPARE(images(prepare()),1); QCOMPARE(images(visible),1);
        // Geometry reaching a lit cell does not move the target's light anchor.
        for(int y=0;y<11;++y) w.tiles[{5,uint16_t(y),0}]=1;
        setLuminosity(w.appearances[100], 0); w.seeInDark=2; w.sight=0x400;
        setFields(w.appearances[4], R"({"pixelX":-96})");
        setFields(w.appearances[3], affinefixture::fields({0,-1,0,1,0,0}));
        QCOMPARE(images(prepare()),0);
        w.sight|=8; const auto bypass=prepare(); QCOMPARE(images(bypass),1);
        QVERIFY(std::any_of(bypass.begin(),bypass.end(),[](const auto& s){return s.kind==shell::MapSprite::Blackness;}));
        // A lit holder can spill into dark cells, but remains BELOW plane-0 blackness.
        w.cellObjs.clear(); atom(w,2,5,6); w.imageInstances[7].targetIdentity=w.atoms[2].renderIdentity;
        w.sight=0x400; setFields(w.appearances[4], R"({"pixelX":96})");
        const auto spill=prepare(); QCOMPARE(images(spill),1);
        auto image=std::find_if(spill.begin(),spill.end(),[](const auto& s){return s.kind==shell::MapSprite::Image;});
        auto black=std::find_if(spill.begin(),spill.end(),[](const auto& s){return s.kind==shell::MapSprite::Blackness;});
        QVERIFY(image<black); QVERIFY(image->affine);
        QVERIFY(std::any_of(black,spill.end(),[&](const auto& s){return s.kind==shell::MapSprite::Blackness && s.rect().intersects(image->rect());}));
    }
    void mobImageCategoriesBoundsAndSelf() {
        auto w=world(); wall(w); const uint32_t mob=2u|model::kRegistryA;
        atom(w,mob,8,6); atom(w,2,8,6);
        w.imageInstances[7]={4,mob,2,false,1,w.atoms[mob].renderIdentity};
        shell::MapVisibility visibility; shell::TurfMap terrain; shell::EntityMap entities;
        auto prepare=[&]{auto map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
            return entities.prepare(w,map,reader(),0,nullptr,true);};
        auto images=[](const auto& sprites){return std::count_if(sprites.begin(),sprites.end(),
            [](const auto& s){return s.kind==shell::MapSprite::Image;});};
        QCOMPARE(images(prepare()),0);
        w.sight=8; QCOMPARE(images(prepare()),0); // SEE_OBJS must not admit mob images
        w.sight=4; QCOMPARE(images(prepare()),1);
        w.sight=0x20; w.haveSelf=true; w.selfRef=2; QCOMPARE(images(prepare()),1);
        w.selfRef=3; QCOMPARE(images(prepare()),0);
        w.sight=0; setFields(w.appearances[4], R"({"pixelX":-64})");
        w.appearances[5]=w.appearances[3]; setFields(w.appearances[5], "{}");
        setFields(w.appearances[3], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[5]"));
        const auto visible=prepare(); QCOMPARE(images(visible),1); QVERIFY(has(visible,mob));
        QVERIFY(!has(visible,2)); // same-number object's bounds must NOT acquire the mob image
        setFields(w.appearances[3], affinefixture::fields({0,-1,32,1,0,0},",\"overlays\":[5]"));
        QCOMPARE(images(prepare()),0);
        for(int y=0;y<11;++y) w.tiles[{5,uint16_t(y),0}]=1;
        setLuminosity(w.appearances[100], 0); w.seeInDark=2; w.sight=0x400;
        setFields(w.appearances[4], R"({"pixelX":-96})");
        setFields(w.appearances[3], affinefixture::fields({0,-1,0,1,0,0},",\"overlays\":[5]"));
        QCOMPARE(images(prepare()),0); // lit spill does not move the mob's illumination anchor
        w.sight|=4; const auto bypass=prepare(); QCOMPARE(images(bypass),1);
        QVERIFY(std::any_of(bypass.begin(),bypass.end(),[](const auto& s){return s.kind==shell::MapSprite::Blackness;}));
    }
    void transformedOrdinaryTreeAdmissionAndAnchor() {
        auto w=world(); wall(w); atom(w,2,8,6);
        setFields(w.appearances[4], R"({"pixelX":-64})");
        shell::MapVisibility visibility; shell::TurfMap terrain; shell::EntityMap entities;
        auto prepare=[&]{auto map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
            return entities.prepare(w,map,reader(),0,nullptr,true);};
        auto pose=[&](float x){setFields(w.appearances[3], affinefixture::fields({0,-1,x,1,0,0},",\"overlays\":[4]"));};
        pose(0); const auto visible=prepare(); QVERIFY(has(visible,2));
        pose(32); QVERIFY(!has(prepare(),2)); QVERIFY(has(visible,2));
        for(int y=0;y<11;++y) w.tiles[{5,uint16_t(y),0}]=1;
        setLuminosity(w.appearances[100], 0); w.seeInDark=2; w.sight=0x400;
        setFields(w.appearances[4], R"({"pixelX":-96})"); pose(0);
        QVERIFY(!has(prepare(),2)); // lit spill cannot replace dark tile-anchor admission
        w.sight|=8; QVERIFY(has(prepare(),2));
        w.cellObjs.clear(); atom(w,2,5,6); w.sight=0x400;
        setFields(w.appearances[4], R"({"pixelX":96})");
        const auto spill=prepare(); QVERIFY(has(spill,2));
        auto black=std::find_if(spill.begin(),spill.end(),[](const auto& s){return s.kind==shell::MapSprite::Blackness;});
        QVERIFY(black!=spill.end()); int parts=0;
        for(auto it=spill.begin();it!=black;++it) if(it->atom==2 && it->kind==shell::MapSprite::Entity) {
            ++parts; QVERIFY(it->rootAffine);
            if(parts==2) QVERIFY(std::any_of(black,spill.end(),[&](const auto& s){return s.kind==shell::MapSprite::Blackness && s.rect().intersects(it->rect());}));
        }
        QCOMPARE(parts,2);
    }
    void attachmentsAndLastFrame() {
        auto w=world(); wall(w); atom(w,2,8,6); shell::MapVisibility visibility;
        shell::TurfMap terrain; shell::EntityMap entities; shell::TurfMap::Frame map;
        auto prepare=[&] { map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w); map.geometry.session=1;
            return entities.prepare(w,map,reader(),0,nullptr,true); };
        setFields(w.appearances[4], R"({"pixelX":-96})");
        w.imageInstances[7]={4,2,1,false,1,w.atoms[2].renderIdentity};
        auto s=prepare(); QVERIFY(has(s,2));
        QVERIFY(std::any_of(s.begin(),s.end(),[](const auto& p) { return p.kind==shell::MapSprite::Image; }));
        w.imageInstances.clear(); QVERIFY(!has(prepare(),2));
        // Vis copies use their placed bounds without extending the holder's admission.
        atom(w,3,1,1,4); w.atoms[3].hasPos=false; w.cellObjs.erase({1,1,0});
        QVERIFY(w.vis.replace(2,w.atoms[2].renderIdentity,{{2,3}},[&](uint32_t key) { return w.atoms.at(key).renderIdentity; }));
        s=prepare(); QVERIFY(!has(s,2));
        QVERIFY(std::any_of(s.begin(),s.end(),[](const auto& p) { return p.kind==shell::MapSprite::Vis; }));
        w.atoms[2].pixelX=-96; setFields(w.appearances[4], R"({"pixelX":96})");
        s=prepare(); QVERIFY(has(s,2));
        QVERIFY(std::none_of(s.begin(),s.end(),[](const auto& p) { return p.kind==shell::MapSprite::Vis; }));
        w.vis.erase(2); w.atoms[2].pixelX=0;
        auto hidden=prepare(); const auto hiddenMap=map;
        auto pick=[&](const auto& frame,const auto& sprites) { return shell::mapPointerAt({240,144},{288,288},{9,9},frame.source,frame.geometry,"main.map",true,sprites); };
        QVERIFY(pick(hiddenMap,hidden)->backgroundHidden);
        w.sight=8; auto visible=prepare(); const auto visibleMap=map;
        auto hit=pick(visibleMap,visible); QVERIFY(hit); QCOMPARE(hit->click.over.ref,uint32_t(2));
        QVERIFY(pick(hiddenMap,hidden)->backgroundHidden); // publication is not rendering
        w.sight=0; prepare(); QVERIFY(pick(visibleMap,visible));
        QVERIFY(shell::mapPointerCurrent(*hit,w,1));
        shell::MapPointer drop; drop.kind=shell::MapPointer::Drop; drop.session=1; drop.z=0;
        drop.source=std::make_shared<const shell::MapPointer>(*hit); drop.click.over=proto::enc::nullRef(); drop.outside=true;
        QVERIFY(shell::mapPointerCurrent(drop,w,1));
        ++w.atoms[2].renderIdentity; QVERIFY(!shell::mapPointerCurrent(*hit,w,1)); QVERIFY(!shell::mapPointerCurrent(drop,w,1));
    }
    void sceneAndSight() {
        auto w=world(); wall(w); shell::MapVisibility visibility; shell::TurfMap terrain; shell::EntityMap entities;
        atom(w,1,4,6); atom(w,2,8,6); atom(w,model::kRegistryA|3,8,7);
        shell::AtomTrace trace; trace.key=2; entities.setDiagnostics(&trace);
        shell::TurfMap::Frame map;
        shell::EntityMap::Stats stats;
        auto prepare=[&] { map=terrain.prepare(w,{},0,false); map.geometry.visibility=visibility.prepare(w);
            return entities.prepare(w,map,reader(),0,&stats,true); };
        auto s=prepare(); QVERIFY(has(s,1)); QVERIFY(!has(s,2)); QVERIFY(!has(s,model::kRegistryA|3));
        QCOMPARE(trace.map.placement,shell::TraceReason::Occluded);
        for (const auto& p:s) if (p.kind==shell::MapSprite::Turf) QVERIFY(p.cell.x()<=6);
        const auto warm=prepare(); QCOMPARE(warm,s); QCOMPARE(stats.frameDecodes,uint64_t(0));
        for (uint32_t sight : {4u,8u,16u,28u,512u}) {
            w.sight=sight; s=prepare();
            QCOMPARE(has(s,2),bool(sight&(8|512))); QCOMPARE(has(s,model::kRegistryA|3),bool(sight&(4|512)));
            const bool hiddenTurf=std::any_of(s.begin(),s.end(),[](const auto& p) { return p.kind==shell::MapSprite::Turf && p.cell.x()==8; });
            QCOMPARE(hiddenTurf,bool(sight&(16|512)));
        }
        w.sight=512; setFields(w.appearances[3], "{\"vars\":[{\"bit\":4,\"data\":\"32\"}]}");
        QVERIFY(!has(prepare(),2)); w.seeInvisible=50; QVERIFY(has(prepare(),2));
        w.sight=0; QVERIFY(!has(prepare(),2)); // invisibility and LOS independent
        setFields(w.appearances[3], "{}");
        w.atoms[2].pixelX=-48; QVERIFY(has(prepare(),2)); // whole bounds, not anchor or pixel clipping
        w.atoms[2].pixelX=0; setFields(w.appearances[3], affinefixture::fields({1,0,-96,0,1,0}));
        QVERIFY(has(prepare(),2));
        setFields(w.appearances[3], "{\"overlays\":[4]}");
        setFields(w.appearances[4], "{\"pixelX\":-96}"); QVERIFY(has(prepare(),2));
        setFields(w.appearances[3], "{}"); w.haveSelf=true; w.selfRef=3; w.sight=32;
        QVERIFY(has(prepare(),model::kRegistryA|3)); QVERIFY(!has(prepare(),2));
        setFields(w.appearances[3], R"({"vars":[{"bit":4,"data":"32"}]})"); w.seeInvisible=0;
        QVERIFY(has(prepare(),model::kRegistryA|3)); QVERIFY(!has(prepare(),2));
        w.sight=4; QVERIFY(!has(prepare(),model::kRegistryA|3)); // only self has the invisibility exception
        w.sight=32;
        setFields(w.appearances[3], R"({"vars":[{"bit":4,"data":"32"}],"overlays":[4]})");
        setFields(w.appearances[4], R"({"pixelX":32,"vars":[{"bit":4,"data":"32"}]})");
        const auto self=prepare();
        QCOMPARE(std::count_if(self.begin(),self.end(),[](const auto& p) { return p.kind==shell::MapSprite::Entity && p.atom==(model::kRegistryA|3); }),2);
        QVERIFY(!has(self,2));
        setFields(w.appearances[3], "{}");
        // HUD membership is an independent surface even with the same hidden object ID.
        w.hudOrder={2}; w.hudAppid[2]=4; setFields(w.appearances[4], "{\"screenLoc\":\"NORTHEAST\"}");
        s=prepare(); QVERIFY(std::any_of(s.begin(),s.end(),[](const auto& p) { return p.kind==shell::MapSprite::Screen; }));
        // Last-rendered metadata remains immutable when a later scene reveals the target.
        const auto hidden=s; w.sight=8; const auto visible=prepare(); QVERIFY(!has(hidden,2)); QVERIFY(has(visible,2));
    }
};
FUGUE_TEST_MAIN(MapVisibilityTest)
#include "mapvisibility_test.moc"
