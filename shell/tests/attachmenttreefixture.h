#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/entitymap.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/affinefixture.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
namespace fugue::shell::test {
struct AttachmentTreeFixture {
    model::World world;
    TurfMap terrain; EntityMap entities;
    TurfMap::Frame frame; MapSprites sprites;
    QJsonObject evidence;
    QString error;
    std::array<QByteArray,4> assets;
    static void scalar(QJsonObject& object,QColor value) {
        object["hasColor"]=true;object["colorR"]=value.red();object["colorG"]=value.green();
        object["colorB"]=value.blue();object["colorA"]=value.alpha();
    }
    static QJsonObject matrixMaterial() {
        const std::array<float,20> matrix{0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, .125f,0,0,0};
        QByteArray bytes;
        for (float f:matrix) {const auto bits=std::bit_cast<uint32_t>(f);for (int b=0;b<4;++b) bytes.append(char(bits>>(8*b)));}
        return {{"bit",1024},{"data",QString::fromLatin1(bytes.toHex())}};
    }
    bool load() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/attachment-tree-world/stock.json";
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& field,const QString& reason) {error=path+": "+field+": "+reason;return false;};
        if (!document.isObject()) return fail("root","Expected an evidence object");
        const auto recorded=document.object();
        if (recorded["source"].toArray()!=QJsonArray{224,224})
            return fail("source","Expected the retained 224x224 source map");
        const auto points=recorded["points"].toArray();
        if (points.size()!=24) return fail("points","Expected 24 source sample points");
        for (qsizetype i=0;i<points.size();++i)
            if (!::fugue::testinput::isIntegerTuple(points[i],2,0,223))
                return fail(QString("points[%1]").arg(i),"Expected an integer [x,y] inside the source map");
        const auto phases=recorded["phases"].toObject();
        if (phases.size()!=42) return fail("phases","Expected 42 distinct named phases");
        for (auto phase=phases.begin();phase!=phases.end();++phase) {
            if (phase.key().trimmed().isEmpty()) return fail("phases","Phase names must not be empty");
            const auto samples=phase.value().toArray();
            const QString field="phases."+phase.key();
            if (samples.size()!=24) return fail(field,"Expected one RGB sample for each of the 24 source points");
            for (qsizetype i=0;i<samples.size();++i)
                if (!::fugue::testinput::isIntegerTuple(samples[i],3,0,255))
                    return fail(field+QString("[%1]").arg(i),"Expected three integer RGB channels in [0,255]");
        }
        if (!phases.contains("input") || !phases.contains("group-half"))
            return fail("phases","Missing the input or group-half checkpoint used by the tests");
        const auto clicks=recorded["clicks"].toArray();
        if (clicks.size()!=2) return fail("clicks","Expected two retained Click receipts");
        for (qsizetype i=0;i<clicks.size();++i) {
            const QString field=QString("clicks[%1]").arg(i);
            const auto click=clicks[i].toObject();
            if (!::fugue::testinput::isIntegerTuple(click["point"],2,0,223))
                return fail(field+".point","Expected an integer [x,y] inside the source map");
            if (click["target"].toString()!="child" || click["location_null"].toDouble(-1)!=1)
                return fail(field,"Expected a child Click with null world location");
            const auto params=click["params"].toObject();
            for (const char* key:{"icon-x","icon-y"}) {
                bool valid=false; params[key].toString().toInt(&valid);
                if (!valid) return fail(field+".params."+key,"Expected an integer string");
            }
            if (params["button"].toString()!="left" || params["left"].toString()!="1" ||
                params["screen-loc"].toString().trimmed().isEmpty())
                return fail(field+".params","Expected a left-button Click with screen-loc");
        }
        evidence=recorded;
        const QColor colors[]={{24,32,48},{40,80,200},{200,40,20},{20,200,60}};
        for (size_t i=0;i<assets.size();++i) {QImage image(32,32,QImage::Format_RGBA8888);image.fill(colors[i]);assets[i]=iconfixture::png(image);}
        world.bounds={true,7,7,0,7,7,7,7};world.resync.haveOrigin=true;
        world.resources={{1,0},{2,0},{3,0},{4,0}};
        world.appearances[1].fields.iconResID=0;setFields(world.appearances[1], "{}");
        world.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(2.f);
        for (uint16_t y=0;y<7;++y) for (uint16_t x=0;x<7;++x) world.tiles[{x,y,0}]=1;
        world.appearances[2].fields.iconResID=UINT32_MAX;world.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        world.appearances[3].fields.iconResID=1;world.appearances[3].fields.layerBits=std::bit_cast<uint32_t>(2.f);
        world.appearances[4].fields.iconResID=2;world.appearances[4].fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        world.appearances[5].fields.iconResID=3;world.appearances[5].fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        world.appearances[6]=world.appearances[3];
        auto& holder=world.atoms[42];holder.appid=2;holder.renderIdentity=1;holder.haveKey=true;holder.key=99;
        holder.x=holder.y=3;
        auto& child=world.atoms[43];child.appid=3;child.renderIdentity=2;child.haveKey=true;child.key=100;
        auto& tail=world.atoms[44];tail.appid=6;tail.renderIdentity=3;tail.haveKey=true;tail.key=101;
        world.vis.replace(42,1,{{2,43}},[&](uint32_t){return uint64_t(2);});
        return true;
    }
    void phase(const QString& name) {
        QJsonObject holder{{"screenLoc","3,3"},{"appearanceFlags",0x3004}};
        QJsonObject child{{"hasPlane",true},{"plane",1},{"appearanceFlags",0x3004|(name.contains("group")?0x04000000:0)},
            {"overlays",QJsonArray{4}}};
        QJsonObject overlay{{"hasPlane",true},{"plane",-32767},{"pixelX",12},{"appearanceFlags",4}};
        QJsonObject leaf{{"hasPlane",true},{"plane",-32767},{"pixelY",12},{"appearanceFlags",4}};
        QJsonObject tail{{"hasPlane",true},{"plane",1},{"appearanceFlags",0x3004}};
        world.atoms[44].pixelY=32;
        int overlayFlags=4;
        if (name.contains("apart")) overlayFlags|=0x08000000;
        if (name.contains("reset")) overlayFlags|=0x00c00000;
        if (name.contains("nested")) {
            overlayFlags|=0x04000000;scalar(overlay,{255,255,255,128});overlay["overlays"]=QJsonArray{5};
        }
        overlay["appearanceFlags"]=overlayFlags;
        world.appearances[4].fields.layerBits=std::bit_cast<uint32_t>(name.contains("inner-layer")?1.f:-1.f);
        if(name.contains("inner-add")) overlay["vars"]=QJsonArray{QJsonObject{{"bit",256},{"data","02"}}};
        if(name.contains("under")) {child.remove("overlays");child["underlays"]=QJsonArray{4};}
        if(name=="input") child["appearanceFlags"]=4;
        if (name.contains("half")) scalar(child,{255,255,255,128});
        if (name.contains("tint")) scalar(child,{128,64,192,255});
        QJsonArray vars;
        if (name.contains("matrix")) {child.remove("hasColor");vars.append(matrixMaterial());}
        int blend=name.contains("multiply")?4:name.contains("subtract")?3:name.contains("add")?2:0;
        if (blend) vars.append(QJsonObject{{"bit",256},{"data",QString("0%1").arg(blend)}});
        child["vars"]=vars;
        std::array<float,6> m{1,0,0,0,1,0};
        if (name.contains("rotate")) m={0,-1,0,1,0,0};
        if (name.contains("shear")) m={1,.5f,0,0,1,0};
        if (name.contains("mirror")) m={-1,0,0,0,1,0};
        if (name.contains("shift")) m={1,0,7,0,1,5};
        if (name.contains("scale")) m={.5f,0,0,0,.5f,0};
        if (m!=std::array<float,6>{1,0,0,0,1,0}) {
            const auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(affinefixture::fields(m))).object();
            child["hasTransform"]=true;child["transform"]=fields["transform"];
        }
        if (name.contains("holder")) {
            scalar(holder,{128,192,64,128});holder["vars"]=QJsonArray{QJsonObject{{"bit",256},{"data","02"}}};
        }
        // Wire object offsets are instance fields, not appearance JSON.
        world.atoms[42].pixelX=name.contains("offset")?5:0;world.atoms[42].pixelY=name.contains("offset")?7:0;
        world.atoms[43].pixelX=name.contains("offset")?9:0;world.atoms[43].pixelY=name.contains("offset")?3:0;
        if (name.contains("tail-reset")) tail["appearanceFlags"]=0x00c03004;
        world.vis.erase(43);
        if (name.contains("tail")) world.vis.replace(43,2,{{2,44}},[&](uint32_t){return uint64_t(3);});
        world.appearances[3].fields.iconResID=name.contains("iconless")?UINT32_MAX:1;
        world.hud.clear();world.hudIdentity.clear();world.hudAppid.clear();world.hudOrder.clear();
        world.atoms[42].hasPos=name.contains("world");
        if (!name.contains("world")) {
            const uint32_t key=name.contains("direct")?43:42;
            world.hud[key]={};world.hudOrder={key};world.hudAppid[key]=key==42?2:3;
            world.hudIdentity[key]={2,world.atoms[key].key,10};
            if (key==43) child["screenLoc"]="3,3";
        }
        for (const auto& [id,fields]:std::map<uint32_t,QJsonObject>{{2,holder},{3,child},{4,overlay},{5,leaf},{6,tail}})
            setFields(world.appearances[id], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        refresh();
    }
    void refresh(int64_t now=0,EntityMap::Stats* stats=nullptr) {
        auto read=[&](uint32_t id,QByteArray& out){if (!id || id>assets.size()) return false;out=assets[id-1];return true;};
        frame=terrain.prepare(world,read,now,false);
        sprites=entities.prepare(world,frame,read,now,stats,true);
    }
};
}
