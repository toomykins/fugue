#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/entitymap.h"
#include <QBuffer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <array>
#include <bit>
#include <climits>
#include <map>

namespace fugue::shell::test {
// Synthetic assets/geometry from relay-gate-world; expectations in the tests
// are the independent stock observations, not a CPU rendering oracle.
struct PlaneSourceFixture {
    model::World world;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    std::map<uint32_t,QByteArray> resources;
    PlaneSourceFixture() {
        world.bounds={true,11,11,0,5,3,5,3};
        world.resources={{123,0},{124,0},{125,0},{126,0},{127,0}};
        frame.image=QImage(160,96,QImage::Format_RGBA8888); frame.image.fill(QColor(20,70,90));
        frame.source=frame.image.rect(); frame.geometry={{3,4,5,3},{11,11},32,0,17};
        const std::array<QColor,8> patches{{{240,30,70,1},{73,149,211,3},{123,61,199,7},{91,173,47,17},
                                           {37,119,203,63},{211,53,107,128},{33,181,229,193},{177,93,41,255}}};
        for (int i=0;i<5;++i) {
            QImage im(32,32,QImage::Format_RGBA8888); im.fill(Qt::transparent);
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                if (i==4) { if (x<16 && y<16) im.setPixelColor(x,y,QColor(230,210,30)); }
                else if (i==3) im.setPixelColor(x,y,patches[(y/16)*4+x/8]);
                else if (i!=1) {
                    if (x>=2 && x<=25 && y>=2 && y<=27) im.setPixelColor(x,y,QColor(240,30,30));
                    if (x>=2 && x<=9 && y>=2 && y<=8) im.setPixelColor(x,y,QColor(255,220,20));
                    if (x>=12 && x<=19 && y>=12 && y<=19) im.setPixelColor(x,y,Qt::transparent);
                    if (i==2 && x>=22 && x<=25 && y>=2 && y<=27)
                        im.setPixelColor(x,y,QColor(240,30,30,y<=12 ? 1 : 128));
                } else if (x<16 && y<16) im.setPixelColor(x,y,QColor(30,220,240));
            }
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); im.save(&buffer,"PNG");
            resources[123+i]=bytes;
        }
        auto& ink=world.appearances[1]; ink.fields.iconResID=0; ink.fields.layerBits=std::bit_cast<uint32_t>(3.f);
        setFields(ink, R"({"hasPlane":true,"plane":60,"appearanceFlags":516})");
        auto& atom=world.atoms[100]; atom.appid=1; atom.x=atom.y=6; atom.hasPos=true;
        atom.haveKey=true; atom.key=7; atom.renderIdentity=1;
        world.hudOrder={200,201}; world.hudAppid={{200,2},{201,3}};
        world.hud={{200,proto::Hud::Record{}},{201,proto::Hud::Record{}}}; world.hudIdentity={{200,{2,8,2}},{201,{2,9,3}}};
        auto& master=world.appearances[2]; master.fields.iconResID=0xffff; master.fields.layerBits=std::bit_cast<uint32_t>(3.f);
        setFields(master, R"({"hasPlane":true,"plane":60,"appearanceFlags":268435972,"screenLoc":"1,1","renderTarget":"*relay"})");
        auto& child=world.appearances[4]; child.fields.iconResID=1; child.fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        setFields(child, R"({"pixelX":68,"pixelY":32,"hasPlane":true,"plane":-32767})");
        world.appearances[3].fields.iconResID=0xffff; world.appearances[3].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        relay({});
    }
    void relay(QJsonObject changes) {
        QJsonObject fields{{"hasPlane",true},{"plane",70},{"appearanceFlags",516},
                           {"screenLoc","1,1"},{"renderSource","*relay"}};
        for (auto it=changes.begin();it!=changes.end();++it) fields[it.key()]=it.value();
        setFields(world.appearances[3], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        refresh();
    }
    void observation(const QJsonObject& row) {
        setFields(world.appearances[4], QJsonDocument(row["child"].toObject()).toJson(QJsonDocument::Compact).toStdString());
        relay(row["root"].toObject());
    }
    static bool capturePixelObservations(const QString& path,QJsonArray& out,QString& error,bool pixelParity=true) {
        out={};
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& field,const QString& reason) {error=path+": "+field+": "+reason;return false;};
        if (!document.isObject()) return fail("root","Expected a retained observation object");
        const auto corpus=document.object();
        const auto filename=QFileInfo(path).fileName(); const bool matrix=filename=="matrix.json";
        const auto appearances=corpus["appearances"].toArray();
        const auto pixelSets=corpus["pixels"].toArray(),clickSets=corpus["clicks"].toArray();
        const auto defaults=corpus["appearanceDefaults"].toObject();
        if (defaults.isEmpty()) return fail("appearanceDefaults","Expected retained appearance defaults");
        if (appearances.isEmpty()) return fail("appearances","Expected an appearance table");
        for (qsizetype i=0;i<appearances.size();++i)
            if (!appearances[i].isObject()) return fail(QString("appearances[%1]").arg(i),"Expected an appearance object");
        if (pixelSets.isEmpty() || clickSets.isEmpty()) return fail("pixels/clicks","Expected both observation tables");
        const bool indexedPixels=corpus.contains("samples"); const auto samples=corpus["samples"].toArray();
        if (indexedPixels && samples.isEmpty()) return fail("samples","Expected the indexed pixel sample table");
        const auto validIndex=[](const QJsonValue& value,qsizetype count) {
            return ::fugue::testinput::isIntegerTuple(QJsonArray{value},1,0,int(count)-1);
        };
        const auto validPixel=[](const QJsonValue& value) {
            if (!::fugue::testinput::isIntegerTuple(value,5,0,255)) return false;
            const auto pixel=value.toArray(); return pixel[0].toInt()<160 && pixel[1].toInt()<96;
        };
        for (qsizetype i=0;i<samples.size();++i)
            if (!validPixel(samples[i]))
                return fail(QString("samples[%1]").arg(i),"Expected [x,y,r,g,b] inside 160x96 with RGB bytes");
        const auto recordedRows=corpus["rows"].toArray();
        if (recordedRows.isEmpty()) return fail("rows","Expected retained observation rows");
        if (matrix && recordedRows.size()!=27) return fail("rows","Expected all 27 retained matrix rows");
        QJsonArray rows;
        QSet<QString> names,unsupported;
        int admittedRows=0,admittedPixels=0,admittedClicks=0;
        for (qsizetype i=0;i<recordedRows.size();++i) {
            const QString field=QString("rows[%1]").arg(i);
            auto row=recordedRows[i].toObject(); const auto name=row["name"].toString();
            if (name.trimmed().isEmpty() || (matrix && names.contains(name)))
                return fail(field+".name","Expected a nonempty name, unique within the matrix");
            names.insert(name);
            for (const char* flag:{"admitted","pixelParity","partial","precision","yellow"})
                if ((row.contains(flag) || (matrix && QString(flag)=="admitted")) && !row[flag].isBool())
                    return fail(field+'.'+flag,"Expected an explicit boolean");
            const QStringList required=matrix ? QStringList{"root","child"} : QStringList{"root","master","member"};
            for (const auto& role:required)
                if (!row.contains(role)) return fail(field+'.'+role,"Missing required appearance reference");
            for (const char* role:{"root","master","member","child","overlap"}) {
                if (!row.contains(role)) continue;
                if (!validIndex(row[role],appearances.size()))
                    return fail(field+'.'+role,"Appearance index is missing, noninteger, or outside the retained table");
                auto fields=defaults;
                const auto overrides=appearances[row[role].toInt()].toObject();
                for (auto it=overrides.begin();it!=overrides.end();++it) fields[it.key()]=it.value();
                row[role]=fields;
            }
            if (!validIndex(row["pixels"],pixelSets.size()) || !validIndex(row["clicks"],clickSets.size()))
                return fail(field+".pixels/clicks","Expected valid integer indices into both observation tables");
            const auto selectedPixels=pixelSets[row["pixels"].toInt()];
            const auto selectedClicks=clickSets[row["clicks"].toInt()];
            if (!selectedPixels.isArray() || selectedPixels.toArray().isEmpty())
                return fail(field+".pixels","Expected at least one retained pixel observation");
            const bool numericalOnly=filename=="master-arithmetic.json" &&
                (name=="a-scalar-tint" || name=="a-matrix-tint") &&
                !corpus["numericalTintProvenance"].toString().trimmed().isEmpty();
            if (!selectedClicks.isArray() || (!numericalOnly && selectedClicks.toArray().isEmpty()))
                return fail(field+".clicks","Expected retained Click receipts (except documented numerical-only tint rows)");
            QJsonArray pixels;
            const auto pixelValues=selectedPixels.toArray();
            for (qsizetype sample=0;sample<pixelValues.size();++sample) {
                QJsonValue pixel=pixelValues[sample];
                if (indexedPixels) {
                    if (!validIndex(pixel,samples.size()))
                        return fail(field+QString(".pixels[%1]").arg(sample),"Pixel sample index is outside the retained table");
                    pixel=samples[pixel.toInt()];
                }
                if (!validPixel(pixel))
                    return fail(field+QString(".pixels[%1]").arg(sample),"Expected [x,y,r,g,b] inside 160x96 with RGB bytes");
                pixels.append(pixel);
            }
            const auto clicks=selectedClicks.toArray();
            for (qsizetype click=0;click<clicks.size();++click) {
                const auto c=clicks[click].toArray();
                if (c.size()!=7 || !c[2].isBool() ||
                    !::fugue::testinput::isIntegerTuple(QJsonArray{c[0],c[1]},2,0,159) || c[1].toInt()>=96 ||
                    !::fugue::testinput::isIntegerTuple(QJsonArray{c[3],c[4],c[5],c[6]},4,INT_MIN,INT_MAX))
                    return fail(field+QString(".clicks[%1]").arg(click),"Expected [x,y,hit,iconX,iconY,visX,visY] inside 160x96");
            }
            if (!matrix || row["admitted"].toBool()) {++admittedRows;admittedPixels+=pixels.size();admittedClicks+=clicks.size();}
            else unsupported.insert(name);
            row["pixels"]=pixels; row["clicks"]=clicks;
            if (pixelParity) row["pixelParity"]=true;
            rows.append(row);
        }
        if (matrix) {
            if (admittedRows!=27 || admittedPixels!=403 || admittedClicks!=216)
                return fail("rows","Expected 27 admitted matrix rows with 403 pixel observations and 216 Click receipts");
            if (!unsupported.isEmpty())
                return fail("rows.admitted","Every matrix row is admitted, the quarter turns included");
            for (const char* name:{"m-scale-blur-group","m-blur-group","m-outline-group"})
                if (!names.contains(name)) return fail("rows","Missing matrix checkpoint: "+QString(name));
        } else {
            // Portable consumers must retain the same complete evidence as the
            // native replay, even when run on their own.
            const std::map<QString,std::array<int,3>> inventories{
                {"source-coverage.json",{21,184,184}}, {"capture-pixels.json",{19,147,147}},
                {"precision-model.json",{13,117,117}}, {"master-arithmetic.json",{16,132,126}}};
            const auto expected=inventories.find(filename);
            if (expected==inventories.end() || expected->second!=std::array<int,3>{admittedRows,admittedPixels,admittedClicks})
                return fail("rows","Incomplete retained row/pixel/Click inventory");
        }
        out=rows;
        return true;
    }
    void sourceObservation(const QJsonObject& row) {
        for (const auto& [name,id]:std::initializer_list<std::pair<const char*,uint32_t>>{{"member",1},{"master",2},{"root",3}})
            setFields(world.appearances[id], QJsonDocument(row[name].toObject()).toJson(QJsonDocument::Compact).toStdString());
        world.appearances[1].fields.iconResID=row["yellow"].toBool() ? 4 : row["precision"].toBool() ? 3 : row["partial"].toBool() ? 2 : 0;
        if (row.contains("child"))
            setFields(world.appearances[4], QJsonDocument(row["child"].toObject()).toJson(QJsonDocument::Compact).toStdString());
        world.atoms.erase(101);
        if (row.contains("overlap")) {
            auto& ap=world.appearances[6]; ap.fields.iconResID=row["precision"].toBool() ? 3 : 1; ap.fields.layerBits=std::bit_cast<uint32_t>(4.f);
            setFields(ap, QJsonDocument(row["overlap"].toObject()).toJson(QJsonDocument::Compact).toStdString());
            auto& atom=world.atoms[101]; atom=world.atoms[100]; atom.appid=6; atom.key=10; atom.renderIdentity=4;
        }
        refresh();
    }
    void refresh(EntityMap::Stats* stats=nullptr) {
        sprites=entities.prepare(world,frame,[&](uint32_t id,QByteArray& out) {
            const auto it=resources.find(id); if(it==resources.end()) return false;
            out=it->second; return true;
        },0,stats,true);
    }
    MapPointer at(QPoint p) const {
        return *mapPointerAt(p,{160,96},{5,3},frame.source,frame.geometry,"main.map",true,sprites);
    }
    static QJsonArray blur(const char* bits) {
        return {QJsonObject{{"type",1},{"flags",1},{"paramMask",1},{"params",bits}}};
    }
};
}
