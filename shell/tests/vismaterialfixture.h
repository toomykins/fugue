#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/entitymap.h"
#include "shell/tests/iconfixture.h"
#include <QSet>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <array>
namespace fugue::shell::test {
struct VisMaterialFixture {
    model::World world;
    TurfMap terrain;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    QJsonObject evidence;
    QString error;
    QByteArray basis, floor;
    bool load() {
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/vis-material-world/stock.json";
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& field,const QString& reason) {error=path+": "+field+": "+reason;return false;};
        if (!document.isObject()) return fail("root","Expected an evidence object");
        const auto recorded=document.object();
        if (recorded["sourceMap"].toArray()!=QJsonArray{224,224})
            return fail("sourceMap","Expected the retained 224x224 source map");
        const auto points=recorded["sourceSamplePoints"].toArray();
        if (points.size()!=8) return fail("sourceSamplePoints","Expected eight source sample points");
        for (qsizetype i=0;i<points.size();++i)
            if (!::fugue::testinput::isIntegerTuple(points[i],2,0,223))
                return fail(QString("sourceSamplePoints[%1]").arg(i),"Expected an integer [x,y] inside the source map");
        const auto phases=recorded["phases"].toArray();
        if (phases.size()!=18) return fail("phases","Expected 18 distinct named phases");
        QSet<QString> names;
        for (qsizetype i=0;i<phases.size();++i) {
            const QString field=QString("phases[%1]").arg(i);
            const auto phase=phases[i].toObject(); const auto name=phase["name"].toString();
            if (name.trimmed().isEmpty() || names.contains(name))
                return fail(field+".name","Expected a nonempty, unique phase name");
            names.insert(name);
            const auto samples=phase["samples"].toArray();
            if (samples.size()!=8) return fail(field+".samples","Expected one RGB sample for each of the eight source points");
            for (qsizetype sample=0;sample<samples.size();++sample)
                if (!::fugue::testinput::isIntegerTuple(samples[sample],3,0,255))
                    return fail(field+QString(".samples[%1]").arg(sample),"Expected three integer RGB channels in [0,255]");
        }
        for (const char* name:{"mask","reset","reset-half"})
            if (!names.contains(name)) return fail("phases","Missing checkpoint used by the tests: "+QString(name));
        const auto fields=recorded["fields"].toObject();
        for (const char* name:{"leaf","resetLeaf","halfLeaf","captureMaster","filterMaster"})
            if (fields[name].toObject().isEmpty())
                return fail("fields."+QString(name),"Expected a nonempty retained appearance object");
        const auto filters=fields["filterMaster"].toObject()["filters"].toArray();
        const auto alphaFilters=fields["alphaFilters"].toArray();
        if (filters.size()!=3) return fail("fields.filterMaster.filters","Expected three retained filters");
        if (alphaFilters.size()!=2) return fail("fields.alphaFilters","Expected two retained filters");
        for (const auto& [field,records]:std::array<std::pair<QString,QJsonArray>,2>{{
                {"fields.filterMaster.filters",filters},{"fields.alphaFilters",alphaFilters}}}) {
            for (qsizetype i=0;i<records.size();++i) {
                const auto filter=records[i].toObject();
                const auto bytes=hexDecode(filter["params"].toString().toStdString());
                if (!filter["params"].isString() || !bytes || bytes->empty())
                    return fail(field+QString("[%1].params").arg(i),"Expected nonempty, even-length hexadecimal filter bytes");
            }
        }
        // phase() mutates the flags byte and this exact named-source envelope.
        const auto maskRecord=filters[0].toObject();
        const auto bytes=QByteArray::fromHex(maskRecord["params"].toString().toLatin1());
        if (maskRecord["type"].toInt(-1)!=7 || maskRecord["paramMask"].toInt(-1)!=0x32c ||
            maskRecord["str"].toString()!="*mask" || bytes.size()!=20 || bytes.mid(14)!=QByteArray("*mask\0",6))
            return fail("fields.filterMaster.filters[0]","Expected the retained 20-byte alpha-mask layout with *mask at byte 14");
        evidence=recorded;
        QImage image(32,32,QImage::Format_RGBA8888);
        const QColor colors[]={{255,0,0,0},{0,255,0,64},{0,0,255,128},{0,0,0,255},
            {63,67,59,64},{128,64,32,128},{0,0,0,0},{255,255,255,255}};
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) image.setPixelColor(x,y,colors[(y/16)*4+x/8]);
        basis=iconfixture::png(image); image.fill(QColor(160,120,80)); floor=iconfixture::png(image);
        world.bounds={true,7,7,0,7,7,7,7};world.resync.haveOrigin=true;
        world.resources={{1,0},{2,0}};
        for(int y=0;y<7;++y) for(int x=0;x<7;++x) world.tiles[{uint16_t(x),uint16_t(y),0}]=1;
        world.appearances[1].fields.iconResID=1;setFields(world.appearances[1], "{}");
        world.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(2.f);
        world.appearances[2].fields.iconResID=0;world.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        auto& leaf=world.atoms[43];leaf.appid=2;leaf.renderIdentity=2;leaf.haveKey=true;leaf.key=10;
        world.vis.replace(model::VisContents::turf(3,3,0),0,{{2,43}},[&](uint32_t){return uint64_t(2);});
        auto& mask=world.atoms[44];mask=leaf;mask.appid=3;mask.renderIdentity=3;
        mask.hasPos=true;mask.x=mask.y=4;
        world.appearances[3]=world.appearances[2];
        setFields(world.appearances[3], R"({"hasPlane":true,"plane":3})");
        world.hudOrder={100,101};world.hudAppid={{100,4},{101,5}};
        setFields(world.appearances[5], R"({"appearanceFlags":268435972,"hasPlane":true,"plane":3,"screenLoc":"1,1","renderTarget":"*mask"})");
        return true;
    }
    void phase(const QString& name) {
        const auto fields=evidence["fields"].toObject();
        auto leaf=fields[name.contains("half")?"halfLeaf":name.contains("reset")?"resetLeaf":"leaf"].toObject();
        setFields(world.appearances[2], QJsonDocument(leaf).toJson(QJsonDocument::Compact).toStdString());
        setFields(world.appearances[1], name.contains("reset") ?
            R"({"hasColor":true,"colorR":64,"colorG":128,"colorB":192,"colorA":64})" : "{}");
        QJsonObject master{{"appearanceFlags",268435972},{"hasPlane",true},{"plane",1},{"screenLoc","1,1"}};
        if(name=="capture") master=fields["captureMaster"].toObject();
        if(name=="multiply") master["vars"]=QJsonArray{QJsonObject{{"bit",256},{"data","04"}}};
        auto filters=fields["filterMaster"].toObject()["filters"].toArray();
        QJsonArray selected;
        if(name.contains("mask")) {
            auto mask=filters[0].toObject();auto bytes=QByteArray::fromHex(mask["params"].toString().toLatin1());
            bytes[8]=name.contains("inverse")?1:0;
            if(name.contains("missing")) {bytes.replace(14,5,"*missing");mask["str"]="*missing";}
            mask["params"]=QString::fromLatin1(bytes.toHex());selected.append(mask);
        }
        if(name.contains("color")) {selected.append(filters[1]);selected.append(filters[2]);}
        if(name.startsWith("alpha-color")) {
            selected=fields["alphaFilters"].toArray();
            if(!name.contains("opaque")) selected.removeLast();
        }
        master["filters"]=selected;
        setFields(world.appearances[4], QJsonDocument(master).toJson(QJsonDocument::Compact).toStdString());
        world.hudOrder={100,101};
        if(name=="capture") {
            world.hudOrder.push_back(102);world.hudAppid[102]=6;
            setFields(world.appearances[6], R"({"screenLoc":"SOUTHWEST","hasPlane":true,"plane":2,"renderSource":"*basis"})");
        }
        const auto turf=model::VisContents::turf(3,3,0);
        world.vis.erase(turf);world.vis.erase(45);
        const bool holder=name.contains("holder");
        auto& parent=world.atoms[45];parent.appid=7;parent.hasPos=holder;parent.x=parent.y=4;parent.renderIdentity=4;
        setFields(world.appearances[7], R"({"hasColor":true,"colorR":64,"colorG":128,"colorB":192,"colorA":64})");
        world.appearances[7].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        world.vis.replace(holder?45:turf,holder?4:0,{{2,43}},[&](uint32_t){return uint64_t(2);});
        refresh();
    }
    void refresh() {
        auto read=[&](uint32_t id,QByteArray& out){out=id==1?basis:floor;return true;};
        frame=terrain.prepare(world,read,0,false);
        sprites=entities.prepare(world,frame,read,0,nullptr,true);
    }
};
}
