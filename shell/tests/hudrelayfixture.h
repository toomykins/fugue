#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/entitymap.h"
#include "shell/mapinput.h"
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <bit>
#include <limits>

namespace fugue::shell::test {
// Independent hud-relay-world sources/assets; observations are stock, not our renderer.
struct HudRelayFixture {
    model::World world;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    std::map<uint32_t,QByteArray> resources;
    HudRelayFixture(const QString& phase) {
        world.bounds={true,8,6,0,7,5,7,5}; world.resources={{123,0},{124,0}};
        frame.image=QImage(224,160,QImage::Format_RGBA8888);frame.image.fill(QColor(20,70,90));
        frame.source=frame.image.rect();frame.geometry={{0,0,7,5},{8,6},32,0,17};
        for(int i=0;i<2;++i) {
            QImage im(i ? QSize(75,53) : QSize(45,21),QImage::Format_RGBA8888);im.fill(QColor(255,220,20));
            for(int y=7;y<12;++y) for(int x=7;x<12;++x) im.setPixelColor(x,y,Qt::transparent);
            QBuffer buffer(&resources[123+i]);buffer.open(QIODevice::WriteOnly);im.save(&buffer,"PNG");
        }
        auto add=[&](uint32_t id,int plane,uint32_t flags,const QString& loc,const QString& target={},const QString& source={}) {
            auto& ap=world.appearances[id];ap.fields.iconResID=0xffff;ap.fields.layerBits=std::bit_cast<uint32_t>(3.f);
            QJsonObject fields{{"hasPlane",true},{"plane",plane},{"appearanceFlags",double(flags)},
                {"screenLoc",loc},{"renderTarget",target},{"renderSource",source}};
            setFields(ap, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            world.hudOrder.push_back(id);world.hudAppid[id]=id;world.hud[id]={};world.hudIdentity[id]={2,uint16_t(id),id+100};
        };
        QString loc="TOP:-13,CENTER:-20";
        if(phase.contains("right")) loc="RIGHT:-13,CENTER";
        if(phase.contains("corner")) loc="RIGHT:-13,TOP:-13";
        add(1,10,phase.contains("mouse0") ? 0x3204 : phase.contains("mouse2") ? 0x1204 : 0x204,loc);
        world.appearances[1].fields.iconResID=phase.contains("large") ? 1 : 0;
        if(phase.contains("overlay")) {
            world.appearances[6].fields.iconResID=1;world.appearances[6].fields.layerBits=std::bit_cast<uint32_t>(-1.f);
            setFields(world.appearances[6], R"({"pixelX":-10,"pixelY":5})");
            change(1,{{"overlays",QJsonArray{6}}});
        }
        if(phase.contains("relay") || phase.contains("chain")) {
            add(2,10,phase.contains("master0") ? 0x10003204 : 0x10000204,"CENTER","*source");
            add(3,20,phase.contains("nopass") ? 0x04000204 : phase.contains("relay0") ? 0x84003204 : 0x84000204,"1,1",{},"*source");
            if(phase.contains("chain")) {
                add(4,20,0x10000204,"CENTER","*middle");
                add(5,30,0x84000204,"1,1",{},"*middle");
            }
        }
        refresh();
    }
    void change(uint32_t id,const QJsonObject& changes) {
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(world.appearances[id]))).object();
        for(auto it=changes.begin();it!=changes.end();++it) fields[it.key()]=it.value();
        setFields(world.appearances[id], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
    }
    void refresh(EntityMap::Stats* stats=nullptr) {
        sprites=entities.prepare(world,frame,[&](uint32_t id,QByteArray& out){
            const auto it=resources.find(id);if(it==resources.end()) return false;out=it->second;return true;
        },0,stats,true);
    }
    MapPointer at(QPoint p) const {return *mapPointerAt(p,{224,160},{7,5},frame.source,frame.geometry,"main.map",true,sprites);}
    static bool rows(QJsonArray& out,QString& error) {
        out={};
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/hud-relay-world/stock.json";
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& reason) {error=path+": "+reason;return false;};
        const auto evidence=document.object();const auto rows=evidence["rows"].toArray();
        if (!document.isObject() || rows.size()!=12) return fail("Expected 12 retained HUD phases");
        if (evidence["source"].toArray()!=QJsonArray{224,160}) return fail("Expected a 224x160 source");
        const QRect source(0,0,224,160);
        QStringList names;
        for (const auto& value:rows) {
            const auto row=value.toObject();const auto name=row["phase"].toString();
            if (name.isEmpty() || name!=name.trimmed() || names.contains(name))
                return fail("Missing, invalid or duplicate phase name: "+name);
            names.push_back(name);
            if (!::fugue::testinput::isIntegerTuple(row["bounds"],4,0,224))
                return fail(name+": expected four integer ink bounds");
            const auto b=row["bounds"].toArray();
            const QRect ink(QPoint(b[0].toInt(),b[1].toInt()),QPoint(b[2].toInt()-1,b[3].toInt()-1));
            if (ink.isEmpty() || !source.contains(ink)) return fail(name+": ink bounds are outside the source");
            const auto clicks=row["clicks"].toArray();
            if (clicks.size()!=2) return fail(name+": expected two Click observations");
            for (qsizetype i=0;i<clicks.size();++i) {
                const auto click=clicks[i].toObject();const auto point=click["point"].toArray();
                const auto target=click["name"].toString();
                if (!::fugue::testinput::isIntegerTuple(click["point"],2,0,223) ||
                    !source.contains(QPoint(point[0].toInt(),point[1].toInt())) ||
                    !::fugue::testinput::isIntegerTuple(click["icon"],2,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()) ||
                    !click["vis"].isBool() || (target!="floor" && target!="button" && target!="first-relay"))
                    return fail(QString("%1: invalid Click observation %2").arg(name).arg(i));
            }
        }
        out=rows;return true;
    }
    static QString check(const MapPointer& p,const QJsonObject& expected) {
        const auto name=expected["name"].toString();
        if(name=="floor" ? p.click.over.kind!=proto::enc::RefTurf :
            p.click.over.kind!=proto::enc::RefObj || p.click.over.ref!=(name=="button" ? 1u : 3u)) return "target";
        const auto icon=expected["icon"].toArray();
        if(p.click.iconX!=icon[0].toInt() || p.click.iconY!=icon[1].toInt()) return "icon coordinates";
        if(p.click.hasVis!=expected["vis"].toBool()) return "vis coordinates";
        return {};
    }
};
}
