#include "tests/appearancejson.h"
#include <QtTest>
#include <QSet>
#include "tests/testmain.h"
#include <bit>
#include "shell/entitymap.h"
#include "shell/namedmap.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/vismaterialfixture.h"
#include "shell/tests/attachmenttreefixture.h"
#include "shell/compositionplan.h"
using namespace fugue;
namespace {
model::World world() {
    model::World w; w.bounds={true,20,20,0,3,3,3,3}; w.resync.haveOrigin=true;
    w.resync.originX=w.resync.originY=4; w.resources={{123,0}};
    auto& ap=w.appearances[1]; setFields(ap, "{}");ap.fields.layerBits=std::bit_cast<uint32_t>(3.f);
    auto& root=w.atoms[42];root.hasPos=true;root.x=root.y=6;root.appid=1;
    root.key=99;root.haveKey=true;root.renderIdentity=1;
    w.appearances[2]=ap;w.appearances[2].fields.iconResID=0;
    auto& child=w.atoms[43];child.appid=2;child.key=100;child.haveKey=true;child.renderIdentity=2;child.pixelX=16;
    return w;
}
bool bind(model::World& w,uint32_t id,std::vector<model::VisReference> refs) {
    return w.vis.replace(id,w.atoms.at(id).renderIdentity,refs,[&](uint32_t child){
        auto found=w.atoms.find(child);return found==w.atoms.end()?uint64_t(0):found->second.renderIdentity;});
}
auto reader() { return [](uint32_t,QByteArray& out){out= iconfixture::animation();return true;}; }
struct Scene {
    shell::TurfMap terrain;shell::EntityMap scene;
    shell::MapSprites draw(const model::World& w,int64_t now=1,shell::EntityMap::Stats* stats=nullptr) {
        return scene.prepare(w,terrain.prepare(w,{},now,false),reader(),now,stats,true);
    }
};
void feed(model::Session& s,uint16_t type,const Bytes& bytes){s.ingest(type,bytes);}
Bytes membership(uint32_t holder,uint16_t key,std::vector<model::VisReference> refs) {
    Bytes bytes(9,0);bytes.push_back(1);putLe32(bytes,holder);putLe16(bytes,key);putLe16(bytes,uint16_t(refs.size()));
    for(auto ref:refs){bytes.push_back(ref.kind);putLe32(bytes,ref.ref);}return bytes;
}
void record(model::Session& s,uint32_t id,uint16_t key){Bytes b{2};putLe32(b,id);putLe16(b,key);feed(s,0xc7,b);}
}
class VisContentsTest:public QObject {
 Q_OBJECT
private slots:
 void hudCopiesOrderAndLifetimes() {
    auto w=world();w.atoms[42].hasPos=false;w.atoms[43].pixelX=0;
    setFields(w.appearances[1], R"({"screenLoc":"1,1 to 2,1"})");
    w.hud[42]={};w.hudOrder={42};w.hudAppid[42]=1;w.hudIdentity[42]={2,99,20};
    bind(w,42,{{2,43}});Scene scene;
    auto first=scene.draw(w);QCOMPARE(first.size(),2);
    QCOMPARE(first[0].kind,shell::MapSprite::Screen);QVERIFY(first[0].visHit);
    QCOMPARE(first[0].visHit->hudMembership,uint64_t(20));
    QCOMPARE(first[1].position-first[0].position,QPoint(32,0));
    QCOMPARE(first[0].image.cacheKey(),first[1].image.cacheKey());
    const auto frame=scene.terrain.prepare(w,{},1,false);
    auto hit=shell::mapPointerAt(first[0].position-frame.source.topLeft()+QPoint(16,16),
        {96,96},{3,3},frame.source,frame.geometry,"main.map",true,first);
    QVERIFY(hit && hit->visHit);QVERIFY(hit->screen);QCOMPARE(hit->click.loc.kind,proto::enc::RefNull);
    QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    w.hud.erase(42);QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
    w.hud[42]={};w.hudIdentity[42].membership=21;QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
    auto reentered=scene.draw(w);QCOMPARE(reentered.size(),2);QCOMPARE(first[0].visHit->hudMembership,uint64_t(20));
    setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
    w.atoms[44]=w.atoms[43];w.atoms[44].renderIdentity=3;w.atoms[44].key=101;
    bind(w,42,{{2,44},{2,43}});auto reversed=scene.draw(w);QCOMPARE(reversed.size(),2);
    QCOMPARE(reversed[0].atom,44u);QCOMPARE(reversed[1].atom,43u);
    // Sibling ordering comes from membership, not recyclable atom IDs.
    bind(w,42,{{2,43},{2,44}});auto ordered=scene.draw(w);QCOMPARE(ordered[0].atom,43u);
    // A world and screen occurrence of one holder share assets, not coordinates.
    w.atoms[42].hasPos=true;QCOMPARE(scene.draw(w).size(),4);
    w.atoms[44].renderIdentity=99;QVERIFY(scene.draw(w).isEmpty());
 }
 void namedMapHudVisIsolation() {
    auto w=world();w.atoms[42].hasPos=false;w.atoms[43].pixelX=0;
    w.hud[42]={};w.hudOrder={42};w.hudAppid[42]=1;w.hudIdentity[42]={2,99,20};
    QVERIFY(bind(w,42,{{2,43}}));Scene scene;
    setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
    const auto local=scene.draw(w);QCOMPARE(local.size(),1);QVERIFY(local[0].visHit);
    setFields(w.appearances[1], R"({"screenLoc":"preview:1,1"})");
    QVERIFY(scene.draw(w).empty()); // must not expand a foreign holder's children
    shell::NamedMap named("preview");
    const auto frame=named.prepare(w,reader(),1,42);
    QCOMPARE(frame.sprites.size(),1);QVERIFY(frame.sprites[0].visHit);
    auto hit=shell::mapPointerAt({8,8},{32,32},{1,1},frame.terrain.source,
        frame.terrain.geometry,"popup.preview",false,frame.sprites);
    QVERIFY(hit);QVERIFY(shell::mapPointerCurrent(*hit,w,42));
    w.atoms[42].hasPos=true;
    const auto worldCopy=scene.draw(w);QCOMPARE(worldCopy.size(),1);
    QCOMPARE(worldCopy[0].kind,shell::MapSprite::Vis); // world occurrence is unaffected
    w.atoms[42].hasPos=false;
    setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
    QVERIFY(!shell::mapPointerCurrent(*hit,w,42));
    QVERIFY(named.prepare(w,reader(),1,42).sprites.empty());
    auto restored=scene.draw(w);QCOMPARE(restored.size(),local.size());
    QVERIFY(restored[0].visHit);QCOMPARE(*restored[0].visHit,*local[0].visHit);
    restored[0].visHit=local[0].visHit; // metadata values, not allocation identity
    QCOMPARE(restored,local); // same authoritative identities, cached assets
 }
 void sharedGroupPlaybackAndRefusal() {
    shell::test::AttachmentTreeFixture f;QVERIFY2(f.load(),qPrintable(f.error));f.phase("group-half");
    auto copies=[&] {shell::MapSprites out;for(const auto& s:f.sprites) if(s.visHit)out.push_back(s);return out;};
    auto first=copies();QCOMPARE(first.size(),2);QVERIFY(first[0].appearanceGroup);
    QCOMPARE(first[0].appearanceGroup,first[1].appearanceGroup);
    shell::EntityMap::Stats stats;f.refresh(1,&stats);QCOMPARE(stats.templateBuilds,uint64_t(0));
    f.world.appearances[7]=f.world.appearances[3];
    auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[7]))).object();
    fields["pixelX"]=16;fields["colorA"]=64;
    setFields(f.world.appearances[7], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
    f.world.appearances[7].fields.hasPixelOffsets=true;
    auto& child=f.world.atoms[43];child.appid=7;
    child.chain={1000,100,{{3,7,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
    f.refresh(1500,&stats);auto middle=copies();QCOMPARE(middle.size(),2);
    QCOMPARE(middle[0].position-first[0].position,QPoint(8,0));
    QCOMPARE(middle[0].appearanceGroup->tint[3],96.f/255);
    QCOMPARE(first[0].appearanceGroup->tint[3],128.f/255);
    QCOMPARE(middle[0].image.cacheKey(),first[0].image.cacheKey());QCOMPARE(stats.frameDecodes,uint64_t(0));
    fields["appearanceFlags"]=0x3004;
    setFields(f.world.appearances[7], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
    f.refresh(1500);QVERIFY(copies().empty()); // no speculative grouping transition
    child.chain={};child.appid=3;f.phase("group-half");
    // Duplicate/share without folding independently placed captures together.
    auto& second=f.world.atoms[45];second=f.world.atoms[42];second.renderIdentity=4;second.key=102;second.appid=8;
    f.world.appearances[8]=f.world.appearances[2];setFields(f.world.appearances[8], R"({"screenLoc":"5,3"})");
    f.world.hud[45]={};f.world.hudOrder.push_back(45);f.world.hudAppid[45]=8;f.world.hudIdentity[45]={2,102,22};
    f.world.vis.replace(45,4,{{2,43}},[&](uint32_t){return uint64_t(2);});f.refresh();auto shared=copies();QCOMPARE(shared.size(),4);
    QCOMPARE(shared[0].appearanceGroup,shared[2].appearanceGroup);
    QVERIFY(shared[0].groupOccurrence!=shared[2].groupOccurrence);
    QCOMPARE(shared[2].position-shared[0].position,QPoint(64,0));
    const auto plan=shell::CompositionPlan::build(f.sprites,f.frame.source,{true});QVERIFY(plan->supported);
    // Untransformed groups keep their parts' ink (stock 516.1688, tests/data/click-world kt-root).
    for(qsizetype i=0;i<f.sprites.size();++i) if(f.sprites[i].appearanceGroup)
        QCOMPARE(plan->bindings[size_t(i)].refused,!f.sprites[i].appearanceGroup->transform.isIdentity());
    // Unsupported grouped vis holders refuse the forest, not its siblings.
    f.world.vis.replace(43,2,{{2,44}},[&](uint32_t){return uint64_t(3);});f.refresh();QVERIFY(copies().empty());
    f.world.vis.erase(43);
    fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[3]))).object();
    QJsonArray tooMany;for(int i=0;i<4097;++i)tooMany.append(4);fields["overlays"]=tooMany;
    setFields(f.world.appearances[3], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
    f.refresh();QVERIFY(copies().empty());
 }
 void maskAndColorWireEnvelope() {
    shell::test::VisMaterialFixture f;QVERIFY2(f.load(),qPrintable(f.error));
    const auto records=f.evidence["fields"].toObject()["filterMaster"].toObject()["filters"].toArray();
    auto decode=[](QJsonArray filters) {return shell::decodeSurfaceEffects(model::appearanceOf(
        QJsonDocument(QJsonObject{{"filters",filters}}).toJson(QJsonDocument::Compact).toStdString()));};
    const auto valid=decode(records);QCOMPARE(valid.filters.size(),size_t(3));QCOMPARE(valid.ignoredFilters,size_t(0));
    QCOMPARE(valid.filters[0].source,QString("*mask"));QCOMPARE(valid.filters[0].flags,1);
    QCOMPARE(valid.filters[1].material[16],.125f);QCOMPARE(valid.filters[2].material[16],-.125f);
    QVERIFY(!valid.boundedTreeFilters());QVERIFY(!valid.relayFilters());QVERIFY(!valid.sourceCoverage({32,32}));
    // Offsets, MASK_SWAP and any instance byte decode (stock 516.1689, stock-parity-world plane-mask-filters).
    // An icon resource decodes too (alpha-masks).
    for(int mutation:{3,5,6,7}) {
        auto record=records[mutation<5?0:1].toObject();
        auto bytes=QByteArray::fromHex(record["params"].toString().toLatin1());
        if(mutation==3) record["str"]="different";
        if(mutation==5) bytes[0]=1; // non-RGB space
        if(mutation==6) bytes.replace(2,4,QByteArray::fromHex("0000807f"));
        if(mutation==7) bytes.chop(1);
        record["params"]=QString::fromLatin1(bytes.toHex());
        const auto refused=decode({record});QVERIFY(refused.filters.empty());QCOMPARE(refused.ignoredFilters,size_t(1));
    }
    auto mixed=valid; shell::PlaneFilter blur;mixed.filters.push_back(blur);QVERIFY(mixed.valid({32,32}));
    f.phase("mask");
    auto grouped=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[4]))).object();
    grouped["appearanceFlags"]=int(0x14000204);
    setFields(f.world.appearances[4], QJsonDocument(grouped).toJson(QJsonDocument::Compact).toStdString());f.refresh();
    const auto master=std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==shell::MapSprite::Master && s.plane==1;});
    QVERIFY(master!=f.sprites.end());QVERIFY(master->planeEffects);QVERIFY(!master->planeEffects->supported);
 }
 void finiteLeafMaterialsAndAtomicRefusal() {
    shell::test::VisMaterialFixture f;QVERIFY2(f.load(),qPrintable(f.error));f.phase("reset");
    auto copies=[&] {return std::count_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return bool(s.visHit);});};
    QCOMPARE(copies(),1);
    const auto published=f.sprites;
    const auto first=*std::find_if(published.begin(),published.end(),[](const auto& s){return bool(s.visHit);});
    QCOMPARE(first.tint,(std::array<float,4>{1,1,1,1}));QVERIFY(first.color!=shell::identityColor);
    f.phase("reset-half");QCOMPARE(copies(),1);
    const auto half=*std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return bool(s.visHit);});
    QCOMPARE(half.color[19],.5f);QCOMPARE(first.color[19],1.f);QCOMPARE(first.image.cacheKey(),half.image.cacheKey());
    // Matrix inheritance now shares ordinary and nested-vis material composition.
    auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[2]))).object();
    fields["overlays"]=QJsonArray{3};
    setFields(f.world.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());f.refresh();QCOMPARE(copies(),2);
    f.phase("reset");
    f.world.vis.replace(43,2,{{2,44}},[&](uint32_t){return uint64_t(3);});f.refresh();QCOMPARE(copies(),2);
    f.world.vis.erase(43);f.phase("reset");
    fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[2]))).object();
    auto vars=fields["vars"].toArray();
    for(int i=0;i<vars.size();++i) if(vars[i].toObject()["bit"].toInt()==1024) {
        auto v=vars[i].toObject();auto data=v["data"].toString();data.replace(0,8,"0000807f");v["data"]=data;vars[i]=v;
    }
    fields["vars"]=vars;
    setFields(f.world.appearances[2], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());f.refresh();QCOMPARE(copies(),0);
    // Published copies retain their old lifetime; deleting membership cannot mutate them.
    f.world.vis.erase(model::VisContents::turf(3,3,0));QCOMPARE(first.visHit->target,43u);
 }
 void flagsAreSeparateVars() {
    shell::SceneAppearance ap(model::appearanceOf(R"({"appearanceFlags":4294967295,"vars":[{"bit":8192,"data":"2000"}]})"));
    QCOMPARE(ap.visFlags,uint16_t(0x20));
    QCOMPARE(shell::SceneAppearance(model::appearanceOf(R"({"appearanceFlags":32})")).visFlags,uint16_t(0));
 }
 void ingestOrderAndHolderValidation() {
    model::Session s;auto& w=s.worldMut();
    const std::vector<model::VisReference> refs{{2,5},{1,123},{3,7},{2,5},{9,8}};
    feed(s,0x8,membership(42,1,refs));QVERIFY(w.vis.lists.empty());QVERIFY(w.visContents.empty());QVERIFY(w.visTurfs.empty());
    record(s,42,1);feed(s,0x8,membership(42,1,refs));
    QCOMPARE(w.vis.lists.at(42).members.size(),refs.size());
    for(size_t i=0;i<refs.size();++i)QVERIFY(w.vis.lists.at(42).members[i].reference==refs[i]);
    QCOMPARE(w.visContents.at(42),(std::vector<uint32_t>{5,model::kRegistryA|7,5}));
    QCOMPARE(w.visTurfs.at(42),(std::vector<uint32_t>{123}));
    auto identity=w.vis.lists.at(42).identity;
    feed(s,0x8,membership(42,1,refs));QCOMPARE(w.vis.lists.at(42).identity,identity);
    feed(s,0x8,membership(42,2,refs)); // record replacement must not erase the NEW turf list
    QVERIFY(w.vis.lists.at(42).identity!=identity);QCOMPARE(w.visTurfs.at(42).size(),size_t(1));
    QCOMPARE(w.vis.lists.at(42).holderIdentity,w.atoms.at(42).renderIdentity);
    feed(s,0x8,membership(42,2,{}));QVERIFY(w.vis.lists.empty());QVERIFY(w.visContents.empty());QVERIFY(w.visTurfs.empty());
    QCOMPARE(s.ingestErrors(),0);
 }
 void lateBindingDeletionAndReuse() {
    model::Session s;auto& w=s.worldMut();record(s,42,1);
    feed(s,0x8,membership(42,1,{{2,43},{2,43}}));QCOMPARE(w.vis.pending(),size_t(2));
    record(s,43,2);QCOMPARE(w.vis.pending(),size_t(0));
    const auto pinned=w.vis.lists.at(42).members[0].childIdentity;
    QCOMPARE(pinned,w.atoms.at(43).renderIdentity);
    record(s,43,0);QVERIFY(pinned!=w.atoms.at(43).renderIdentity);
    feed(s,0x8,membership(42,1,{{2,43},{2,43}}));QCOMPARE(w.vis.lists.at(42).members[0].childIdentity,pinned);
    feed(s,0x8,membership(42,1,{}));feed(s,0x8,membership(42,1,{{2,44}}));
    Bytes gone;putLe32(gone,44);feed(s,0x6f,gone);record(s,44,3);
    QCOMPARE(w.vis.lists.at(42).members[0].childIdentity,UINT64_MAX);QCOMPARE(w.vis.pending(),size_t(0));
    w.resync.freed.insert(42);record(s,42,1);QVERIFY(w.vis.lists.empty());
    feed(s,0x8,membership(42,1,{{2,45}}));gone.clear();putLe32(gone,42);feed(s,0x6f,gone);
    QVERIFY(w.vis.lists.empty());QCOMPARE(w.vis.pending(),size_t(0));
 }
 void orderedTurfIngestRejectsUnsupportedMembers() {
    model::Session s;auto& w=s.worldMut();w=world();
    Bytes bytes;putLe16(bytes,1);bytes.push_back(1);putLe16(bytes,5);putLe16(bytes,5);putLe16(bytes,0);
    putLe32(bytes,1);putLe16(bytes,3);
    for(auto ref:std::vector<model::VisReference>{{2,43},{1,99},{3,2}}){bytes.push_back(ref.kind);putLe32(bytes,ref.ref);}
    feed(s,0xd8,bytes);
    const auto key=model::VisContents::turf(5,5,0);QCOMPARE(w.vis.lists.at(key).members.size(),size_t(3));
    QCOMPARE(w.vis.lists.at(key).members[1].reference.kind,uint8_t(1));
    Scene scene;QVERIFY(scene.draw(w).isEmpty());
    bytes.resize(13);bytes[9]=bytes[10]=255;bytes[11]=bytes[12]=0;putLe16(bytes,0);feed(s,0xd8,bytes);
    QVERIFY(!w.vis.lists.count(key));QCOMPARE(s.ingestErrors(),0);
 }
 // A copied turf is clicked as that turf, and an atom standing on it as itself (stock 516.1688,
 // tests/data/click-world turf-member: copied-turf, copied-content; location is the holder's turf).
 void turfMembersClickLikeStock() {
    auto w=world(); w.tiles[{10,10,0}]=2;
    const uint32_t flat=(0u*20+10)*20+10;
    QVERIFY(bind(w,42,{{1,flat}}));
    w.atoms[43].hasPos=true; w.atoms[43].x=11; w.atoms[43].y=11; w.atoms[43].pixelX=0;
    Scene scene; const auto sprites=scene.draw(w);
    const auto frame=scene.terrain.prepare(w,{},1,false);
    auto click=[&](const shell::MapSprite& s) {
        return shell::mapPointerAt(s.position-frame.source.topLeft()+QPoint(16,16),frame.source.size(),
            frame.source.size()/32,frame.source,frame.geometry,"map",true,sprites);
    };
    const shell::MapSprite *turf=nullptr,*content=nullptr;
    for (const auto& s:sprites) if (s.visHit) (s.visHit->turf ? turf : content)=&s;
    QVERIFY(turf && content);
    auto hit=click(*content);
    QVERIFY(hit && hit->visHit); QCOMPARE(hit->click.over.kind,proto::enc::RefObj); QCOMPARE(hit->click.over.ref,43u);
    QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    w.atoms[43].x=12; QVERIFY(!shell::mapPointerCurrent(*hit,w,0)); w.atoms[43].x=11;
    // The turf's own pixels, with the content gone.
    w.atoms[43].hasPos=false;
    const auto bare=scene.draw(w);
    turf=nullptr; for (const auto& s:bare) if (s.visHit && s.visHit->turf) turf=&s;
    QVERIFY(turf);
    hit=shell::mapPointerAt(turf->position-frame.source.topLeft()+QPoint(16,16),frame.source.size(),
        frame.source.size()/32,frame.source,frame.geometry,"map",true,bare);
    QVERIFY(hit && hit->visHit); QCOMPARE(hit->click.over.kind,proto::enc::RefTurf);
    QCOMPARE(hit->click.over.x,10); QCOMPARE(hit->click.over.y,10); // zero-based on the wire
    QCOMPARE(hit->click.loc.kind,proto::enc::RefTurf); QCOMPARE(hit->click.loc.x,5); QCOMPARE(hit->click.loc.y,5);
    QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    w.tileRevision[{10,10,0}]=5; QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
 }
 // What a menu entry for a vis member targets, against the picks MEASURED on stock DreamSeeker 516.1689
 // (tests/data/vismenu-world/stock.tsv part B, entry 1: the member itself). A verb ran on the member at its own place;
 // for the turf from another z stock ran nothing, and the target is then not on the map's z.
 void menuTargetsLikeStock() {
    const QString path=QString(FUGUE_TEST_DATA_DIR)+"/vismenu-world/stock.tsv";
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(path+": "+file.errorString()));
    bool partB=false; int rows=0,partRows=0,lineNumber=0;
    QSet<QString> picks;
    auto cell=[](const QString& text,std::array<int,3>& out) {
        const auto parts=text.split(',');
        if(parts.size()!=3) return false;
        std::array<int,3> parsed{};
        for(int i=0;i<3;++i) {
            bool valid=false; parsed[size_t(i)]=parts[i].toInt(&valid);
            if(!valid || parsed[size_t(i)]<0 || parsed[size_t(i)]>(i==2 ? 65535 : 20)) return false;
        }
        if(parsed!=std::array<int,3>{0,0,0} && (parsed[0]==0 || parsed[1]==0 || parsed[2]==0)) return false;
        out=parsed;
        return true;
    };
    for (const auto& raw:file.readAll().split('\n')) {
        ++lineNumber;
        const QString context=path+QString(":%1: ").arg(lineNumber);
        const auto line=QString::fromUtf8(raw).trimmed();
        if (line.startsWith("# part ")) partB=line.startsWith("# part B");
        if (!partB || line.isEmpty() || line.startsWith('#')) continue;
        const auto column=line.split('\t');
        QVERIFY2(column.size()==5,qPrintable(context+"expected five tab-separated menu-pick fields"));
        for(const auto& field:column) QVERIFY2(!field.isEmpty(),qPrintable(context+"menu-pick fields must be explicit"));
        QVERIFY2(column[2]=="1" || column[2]=="2",qPrintable(context+"entry must be 1 or 2"));
        QVERIFY2(column[3]=="near" || column[3]=="anywhere",qPrintable(context+"expected the recorded near or anywhere verb"));
        const QString pick=column[0]+'\t'+column[2]+'\t'+column[3];
        QVERIFY2(!picks.contains(pick),qPrintable(context+"duplicate label/entry/verb pick"));
        picks.insert(pick); ++partRows;
        if (column[2]!="1") continue;
        ++rows;
        const auto member=column[1].split(' '), ran=column[4].split(' ');
        QVERIFY2(member.size()==2 && (member[0]=="mob" || member[0]=="turf"),
                 qPrintable(context+"member must be a mob or turf followed by its coordinates, or mob -"));
        const bool mob=member[0]=="mob", placed=member[1]!="-";
        std::array<int,3> at{},where{};
        QVERIFY2(placed || mob,qPrintable(context+"only a mob may be in nullspace"));
        if(placed) QVERIFY2(cell(member[1],at) && at[2]>0,qPrintable(context+"member requires three positive cell coordinates"));
        if(column[4]=="-") {
            QVERIFY2(!mob,qPrintable(context+"the retained mob picks must name the mob and its coordinates"));
        } else {
            QVERIFY2(ran.size()==2 && ran[0]==column[0] && cell(ran[1],where),
                     qPrintable(context+"ran_on must name this member and three coordinates; nullspace is explicitly 0,0,0"));
        }
        auto w=world(); w.atoms.erase(43);
        const uint32_t key=5|model::kRegistryA;
        if (mob) {
            w.atoms[5].appid=1; w.atoms[5].renderIdentity=7; // an obj with the mob's number is not the member
            auto& m=w.atoms[key]; m.appid=2; m.key=101; m.haveKey=true; m.renderIdentity=8;
            if (placed) { m.hasPos=true; m.x=uint16_t(at[0]); m.y=uint16_t(at[1]); m.z=uint16_t(at[2]-1); }
            QVERIFY(bind(w,42,{{3,5}}));
        } else {
            w.tiles[{uint16_t(at[0]-1),uint16_t(at[1]-1),uint16_t(at[2]-1)}]=2;
            QVERIFY(bind(w,42,{{1,uint32_t(((at[2]-1)*20+at[1]-1)*20+at[0]-1)}}));
        }
        Scene scene; const auto sprites=scene.draw(w);
        const auto frame=scene.terrain.prepare(w,{},1,false);
        const shell::MapSprite* shown=nullptr;
        for (const auto& s:sprites) if (s.visHit) shown=&s;
        QVERIFY2(shown,qPrintable(column[0]));
        const auto hit=shell::mapPointerAt(shown->position-frame.source.topLeft()+QPoint(16,16),frame.source.size(),
            frame.source.size()/32,frame.source,frame.geometry,"map",true,sprites);
        QVERIFY2(hit && hit->visHit && shell::mapPointerCurrent(*hit,w,0),qPrintable(column[0]));
        const auto named=shell::mapPointerTarget(*hit,w);
        QVERIFY2(named && named->appid,qPrintable(column[0])); QCOMPARE(*named->appid,2u);
        if (mob) {
            QCOMPARE(named->kind,shell::MapPointerTarget::Atom); QCOMPARE(named->atom,key);
            const auto& m=w.atoms.at(named->atom);
            QCOMPARE(m.hasPos,where[2]!=0);
            if (m.hasPos) QCOMPARE((std::array<int,3>{m.x,m.y,m.z+1}),where);
        } else {
            QCOMPARE(named->kind,shell::MapPointerTarget::Turf);
            const std::array<int,3> own{named->cell[0]+1,named->cell[1]+1,named->cell[2]+1};
            QCOMPARE(own,at);
            if (ran[0]=="-") QVERIFY(named->cell[2]!=hit->z); // no verb reaches it by cell on the map's z
            else { QCOMPARE(own,where); QCOMPARE(int(named->cell[2]),hit->z); }
        }
    }
    QCOMPARE(partRows,7);
    QCOMPARE(rows,5);
 }
 void turfAndImageTailLifetimes() {
    auto w=world(); w.tiles[{5,5,0}]=1;
    const auto turf=model::VisContents::turf(5,5,0);
    auto bindSpecial=[&](uint64_t holder,uint64_t identity) {
        return w.vis.replace(holder,identity,{{2,43}},[&](uint32_t id){return w.atoms.at(id).renderIdentity;});
    };
    QVERIFY(bindSpecial(turf,0)); Scene scene;
    const auto first=scene.draw(w); QCOMPARE(first.size(),1); QCOMPARE(first[0].atom,43u);
    const auto frame=scene.terrain.prepare(w,{},1,false);
    auto hit=shell::mapPointerAt({220,120},{320,240},{3,3},frame.source,frame.geometry,"map",true,first);
    QVERIFY(hit && hit->visHit); QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    w.vis.erase(turf); QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
    QVERIFY(bindSpecial(turf,0)); QVERIFY(!shell::mapPointerCurrent(*hit,w,0));
    w.vis.erase(turf);
    // A blank image holder still publishes its independent tail; child's clock,
    // mouse target and origin do not become the image's appearance/identity.
    w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], R"({"pixelX":64})");
    w.imageInstances[7]={3,42,1,true,20,1};
    const auto holder=model::VisContents::image(7); QVERIFY(bindSpecial(holder,20));
    const auto tail=scene.draw(w); QCOMPARE(tail.size(),1); QCOMPARE(tail[0].position,first[0].position);
    hit=shell::mapPointerAt({220,120},{320,240},{3,3},frame.source,frame.geometry,"map",true,tail);
    QVERIFY(hit && hit->visHit); QVERIFY(shell::mapPointerCurrent(*hit,w,0));
    w.imageInstances[7].identity=21; QVERIFY(!shell::mapPointerCurrent(*hit,w,0)); QVERIFY(scene.draw(w).empty());
    w.imageInstances[7].identity=20; w.atoms[43].renderIdentity=9; QVERIFY(scene.draw(w).empty());
    QCOMPARE(first[0].visHit->targetIdentity,uint64_t(2));
 }
 void imageTailIngestBindingAndDeletion() {
    model::Session s; record(s,42,1);
    auto image=[&](uint16_t type,uint32_t target,std::vector<uint32_t> children) {
        Bytes b; putLe32(b,7); putLe32(b,3); b.push_back(1); putLe32(b,target); putLe16(b,uint16_t(children.size()));
        for(uint32_t child:children){b.push_back(2);putLe32(b,child);} feed(s,type,b);
    };
    image(0x33,42,{43}); auto& w=s.worldMut(); const auto key=model::VisContents::image(7);
    QVERIFY(w.vis.lists.count(key)); QCOMPARE(w.vis.pending(),size_t(1));
    record(s,43,2); const auto pinned=w.vis.lists.at(key).members[0].childIdentity; QVERIFY(pinned);
    const auto membershipId=w.vis.lists.at(key).identity;
    image(0x35,42,{43}); QCOMPARE(w.vis.lists.at(key).identity,membershipId);
    record(s,43,3); QCOMPARE(w.vis.lists.at(key).members[0].childIdentity,pinned);
    image(0x35,44,{43}); QVERIFY(w.vis.lists.at(key).identity!=membershipId);
    Bytes gone;putLe32(gone,7);feed(s,0x34,gone);QVERIFY(!w.vis.lists.count(key)); QCOMPARE(w.vis.pending(),size_t(0));
    QCOMPARE(s.ingestErrors(),0);
 }
 void retainedAndPendingBudgets() {
    model::VisContents vis;auto missing=[](uint32_t){return uint64_t(0);};
    std::vector<model::VisReference> refs(4096,{2,123});
    for(uint32_t i=0;i<16;++i)QVERIFY(vis.replace(i,i+1,refs,missing));
    QCOMPARE(vis.retained(),size_t(65536));QCOMPARE(vis.pending(),size_t(65536));
    QVERIFY(!vis.replace(16,17,{{2,1}},missing));QCOMPARE(vis.lists.size(),size_t(16));
    refs.push_back({2,1});QVERIFY(!vis.replace(0,1,refs,missing));QVERIFY(!vis.lists.count(0));
    for(uint32_t i=0;i<16;++i)vis.erase(i);
    QCOMPARE(vis.retained(),size_t(0));QCOMPARE(vis.pending(),size_t(0));
 }
 void sharedNestedIndependentAndImmutable() {
    auto w=world();bind(w,42,{{2,43},{2,43}});
    w.atoms[44]=w.atoms[43];w.atoms[44].renderIdentity=3;w.atoms[44].pixelY=16;
    bind(w,43,{{2,44}});
    w.atoms[45]=w.atoms[42];w.atoms[45].renderIdentity=4;w.atoms[45].x=5;bind(w,45,{{2,43}});
    w.atoms[43].hasPos=true;w.atoms[43].x=6;w.atoms[43].y=5;
    Scene scene;shell::EntityMap::Stats stats;const auto first=scene.draw(w);
    QCOMPARE(first.size(),8); // three attached child+grandchild pairs, independent child and its grandchild
    std::set<const shell::VisHit*> hits;for(const auto& sprite:first)if(sprite.visHit)hits.insert(sprite.visHit.get());
    QCOMPARE(hits.size(),size_t(7));
    const auto unchanged=scene.draw(w,1,&stats);QCOMPARE(unchanged.constData(),first.constData());
    QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.sortedSprites,uint64_t(0));
    w.atoms[42].pixelX=3;const auto moved=scene.draw(w,1,&stats);QCOMPARE(stats.templateBuilds,uint64_t(0));
    QVERIFY(moved!=first);QCOMPARE(first.size(),8);
 }
 void cyclesDepthAndUnsupportedFailWholeForest() {
    auto w=world();bind(w,42,{{2,43}});Scene scene;QCOMPARE(scene.draw(w).size(),1);
    bind(w,43,{{2,42}});QVERIFY(scene.draw(w).isEmpty());w.vis.erase(43);
    bind(w,42,{{2,43},{1,99}});QVERIFY(scene.draw(w).isEmpty());
    bind(w,42,{{3,43}});QVERIFY(scene.draw(w).isEmpty());bind(w,42,{{2,43}});
    // vis_flags up to VIS_HIDE are drawn (tests/data/vis-world set b); VIS_HIDE omits the copy; unknown bits refuse.
    // VIS_INHERIT_ICON takes this iconless holder's icon, so it draws nothing.
    for(auto [flag,drawn]:std::initializer_list<std::pair<int,int>>{{1,0},{2,1},{4,1},{8,1},{16,1},{64,1},{127,0},{126,1},{128,0},{256,0}}){
        QByteArray bytes(2,0);bytes[0]=char(flag&255);bytes[1]=char(flag>>8);
        setFields(w.appearances[2], QString("{\"vars\":[{\"bit\":8192,\"data\":\"%1\"}]}").arg(QString::fromLatin1(bytes.toHex())).toStdString());
        QVERIFY2(scene.draw(w).size()==qsizetype(drawn),qPrintable(QString::number(flag)));
    }
    w.atoms[44]=w.atoms[43];w.atoms[44].renderIdentity=44;bind(w,43,{{2,44}});
    setFields(w.appearances[2], R"({"vars":[{"bit":8192,"data":"2000"}]})");
    QVERIFY(scene.draw(w).isEmpty()); // inherited-ID atoms with vis children are unverified
    w.vis.erase(43);setFields(w.appearances[2], "{}");
    for(uint32_t id=44;id<62;++id){w.atoms[id]=w.atoms[43];w.atoms[id].renderIdentity=id;bind(w,id-1,{{2,id}});}
    QVERIFY(scene.draw(w).isEmpty());
 }
 void boundsMovementInheritanceAndInput() {
    auto w=world();bind(w,42,{{2,43}});Scene scene;
    setFields(w.appearances[1], R"({"hasPlane":true,"plane":5,"hasColor":true,"colorR":128,"colorG":255,"colorB":255,"colorA":128})");
    setFields(w.appearances[2], R"({"hasPlane":true,"plane":-32767})");
    const auto first=scene.draw(w);QCOMPARE(first.size(),1);QCOMPARE(first[0].plane,5);QCOMPARE(first[0].tint[0],128.f/255);
    setFields(w.appearances[2], "{}");QCOMPARE(scene.draw(w)[0].plane,0);
    setFields(w.appearances[2], R"({"hasPlane":true,"plane":-32767})");
    const auto frame=scene.terrain.prepare(w,{},1,false);
    auto p=shell::mapPointerAt({220,120},{320,240},{3,3},frame.source,frame.geometry,"main.map",true,first);
    QVERIFY(p&&p->visHit);QCOMPARE(p->click.over.ref,43u);QCOMPARE(p->click.iconX,25);QCOMPARE(p->click.iconY,16);
    QVERIFY(shell::mapPointerCurrent(*p,w,0));
    w.atoms[42].pixelX=4;w.atoms[43].hasPos=true;w.atoms[43].x=1;w.atoms[43].y=1;
    w.atoms[43].gliding=true;w.atoms[43].glide={-1,0,1,100};
    auto moved=scene.draw(w,50);QCOMPARE(moved.size(),1);QCOMPARE(moved[0].position-first[0].position,QPoint(4,0));
    w.atoms[42].gliding=true;w.atoms[42].glide={-1,0,100,100};
    QCOMPARE(scene.draw(w,150)[0].position-first[0].position,QPoint(-12,0));w.atoms[42].gliding=false;
    setFields(w.appearances[2], R"({"vars":[{"bit":8192,"data":"2000"}]})");
    auto inherited=scene.draw(w);p=shell::mapPointerAt({220,120},{320,240},{3,3},frame.source,frame.geometry,"main.map",true,inherited);
    QVERIFY(p&&p->visHit);QCOMPARE(p->click.over.ref,42u);QCOMPARE(p->click.iconX,37);QVERIFY(shell::mapPointerCurrent(*p,w,0));
    bind(w,42,{});QVERIFY(!shell::mapPointerCurrent(*p,w,0));QCOMPARE(first[0].atom,43u);
    bind(w,42,{{2,43}});QVERIFY(!shell::mapPointerCurrent(*p,w,0));
    w.atoms[43].renderIdentity=99;QVERIFY(scene.draw(w).isEmpty());
 }
 void missingAppearanceResourceAndTransitiveRemap() {
    auto w=world();bind(w,42,{{2,43}});setFields(w.appearances[2], R"({"overlays":[3]})");
    Scene scene;QVERIFY(scene.draw(w).isEmpty());w.appearances[3]=w.appearances[2];setFields(w.appearances[3], "{}");
    QCOMPARE(scene.draw(w).size(),2);
    w.resources={{100,0},{200,0},{300,0}};w.appearances[2].fields.iconState=w.appearances[3].fields.iconState="base";
    bool ready=false;auto read=[&](uint32_t id,QByteArray& out){
        if(id==100)out=iconfixture::recipe(iconfixture::select(1,""));
        else if(id==200)out=iconfixture::recipe(iconfixture::select(2,"base"));
        else {out=iconfixture::sheet(id==300?Qt::red:Qt::blue);return ready;}return true;};
    const auto frame=scene.terrain.prepare(w,{},0,false);
    QVERIFY(scene.scene.prepare(w,frame,read,1,nullptr,true).isEmpty());ready=true;
    const auto first=scene.scene.prepare(w,frame,read,1,nullptr,true);QCOMPARE(first.size(),2);QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::red));
    w.resources[2]={400,0};const auto second=scene.scene.prepare(w,frame,read,1,nullptr,true);
    QCOMPARE(second[0].image.pixelColor(16,16),QColor(Qt::blue));QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::red));
 }
 void oversizedOffviewAndRootKindOmissions() {
    auto w=world();bind(w,42,{{2,43}});w.atoms[42].x=3;w.atoms[43].pixelX=96;
    Scene scene;QCOMPARE(scene.draw(w).size(),1); // root outside view, attached pixels inside
    setFields(w.appearances[2], R"({"hasTransform":true})");QVERIFY(scene.draw(w).isEmpty());
    setFields(w.appearances[2], "{}");w.atoms[42].hasPos=false;QVERIFY(scene.draw(w).isEmpty());
    w.atoms[model::kRegistryA|42]=w.atoms[42];w.atoms[model::kRegistryA|42].hasPos=true;
    bind(w,model::kRegistryA|42,{{2,43}});QCOMPARE(scene.draw(w).size(),1); // a mob holder (vis-world set a: mob-holder)
 }
 void warmSharedWorkAndBudgets() {
    auto w=world();const auto root=w.atoms.at(42);
    for(uint32_t id=1000;id<11000;++id){w.atoms[id]=root;w.atoms[id].renderIdentity=id;QVERIFY(bind(w,id,{{2,43}}));}
    Scene scene;shell::EntityMap::Stats stats;const auto first=scene.draw(w);QCOMPARE(first.size(),10000);
    QElapsedTimer timer;timer.start();
    for(int i=0;i<20;++i){const auto same=scene.draw(w,1,&stats);QCOMPARE(same.constData(),first.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.sortedSprites,uint64_t(0));}
    qInfo("10,000 holders sharing one child, warm unchanged: %.3f ms/prepare",double(timer.nsecsElapsed())/20e6);
    w.vis=model::VisContents{};
    std::vector<model::VisReference> many(4096,{2,43});QVERIFY(::bind(w,42,many));
    QCOMPARE(scene.draw(w).size(),4096);
    many.push_back({2,43});QVERIFY(!::bind(w,42,many));QVERIFY(scene.draw(w).isEmpty());
    // A failed forest must charge its cached appearance work too. Otherwise
    // unsupported last edges bypass both the scene budget and template retention bound.
    std::string overlays="{\"overlays\":[";
    for(int i=0;i<33000;++i){if(i)overlays+=',';overlays+='3';}overlays+="]}";
    setFields(w.appearances[2], overlays);w.appearances[3].fields.iconResID=0;
    QVERIFY(bind(w,42,{{2,43},{1,99}}));QVERIFY(bind(w,1000,{{2,43}}));
    QVERIFY(scene.draw(w).isEmpty());QVERIFY(scene.draw(w).isEmpty());
 }
 void animationPhaseAndReentry() {
    auto w=world();w.atoms[43].iconStartNs=900000000;
    bind(w,42,{{2,43}});Scene scene;
    auto draw=[&](int64_t now){return scene.scene.prepare(w,scene.terrain.prepare(w,{},now,false),
        [](uint32_t,QByteArray& out){out=iconfixture::animation(1);return true;},now,nullptr,true);};
    const auto first=draw(1000000000);QCOMPARE(first.size(),1);
    QCOMPARE(first[0].image.pixelColor(16,16),QColor(130,30,20)); // child elapsed 100 ms, not world/holder phase
    bind(w,42,{});QVERIFY(draw(1100000000).isEmpty());bind(w,42,{{2,43}});
    const auto again=draw(1000000000);QCOMPARE(again[0].image,first[0].image);
    w.atoms[42].hasPos=false;QVERIFY(draw(1000000000).isEmpty());w.atoms[42].hasPos=true;
    QCOMPARE(draw(1000000000)[0].image,first[0].image);
    QCOMPARE(draw(2000000000)[0].image.pixelColor(16,16),QColor(210,30,20));
 }
};
FUGUE_TEST_MAIN(VisContentsTest)
#include "viscontents_test.moc"
