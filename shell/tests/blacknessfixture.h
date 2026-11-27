// The small darkness-world fixture, using already verified decoded wire fields.
#pragma once
#include "tests/appearancejson.h"
#include "shell/entitymap.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QBuffer>
#include <bit>

namespace fugue::shell::test {
inline model::World blacknessWorld(const QString& config) {
    const QUrlQuery p(config);
    auto number=[&](const char* key,int fallback) { return p.hasQueryItem(key) ? p.queryItemValue(key).toInt() : fallback; };
    model::World w;
    w.bounds={true,9,9,0,9,9,9,9,9,9}; w.resync.haveOrigin=true;
    // Stock clamps the presented eye to the center in this view-sized world.
    w.haveEye=w.haveSight=w.haveSeeInDark=w.haveSelf=true; w.eyeX=w.eyeY=5; w.selfRef=44;
    w.seeInDark=uint8_t(number("see",0));
    const auto sight=p.queryItemValue("sight");
    w.sight=sight=="blackness" ? 0x400u : sight=="self" ? 32u : sight=="through" ? 512u :
        sight=="objs" ? 8u : sight=="turfs" ? 16u : sight=="mobs" ? 4u : sight=="ghost" ? 28u :
        sight=="mobobj" ? 12u : sight=="turfobj" ? 24u : sight=="turfmob" ? 20u : 0u;
    if (number("black",0)) w.sight|=0x400u;
    for (uint32_t id=1;id<=8;++id) w.resources.push_back({id,0});
    for (uint32_t id=1;id<=10;++id) {
        auto& a=w.appearances[id]; setFields(a, "{}"); setOpaque(a, id==2);
        a.fields.iconResID=id<=8 ? id-1 : UINT32_MAX;
        a.fields.layerBits=std::bit_cast<uint32_t>(id<=2 ? 2.f : id==6 ? 4.f : 3.f);
    }
    auto fields=[&](uint32_t id,QJsonObject f) { setFields(w.appearances[id], QJsonDocument(f).toJson(QJsonDocument::Compact).toStdString()); };
    fields(3,{{"hasPlane",true},{"plane",number("plane",0)}});
    w.appearances[3].fields.iconResID=number("wide",0) ? 3 : 2;
    w.appearances[3].fields.layerBits=std::bit_cast<uint32_t>(float(number("layer",3)));
    fields(6,{{"hasPlane",true},{"plane",number("selfplane",0)}});
    fields(7,{{"screenLoc","NORTHEAST"},{"hasPlane",true},{"plane",number("hudplane",0)}});
    for (uint32_t id:{1u,2u}) {
        fields(id,{{"hasPlane",true},{"plane",number("turfplane",0)}});
        if (p.queryItemValue("flooricon")=="none") w.appearances[id].fields.iconResID=UINT32_MAX;
    }
    fields(8,{{"screenLoc","1,1 to 9,9"},{"hasPlane",true},{"plane",number("backplane",-5)},{"appearanceFlags",0x3000}});
    w.appearances[8].fields.layerBits=std::bit_cast<uint32_t>(float(number("backlayer",3)));
    setLuminosity(w.appearances[100], uint8_t(number("ambient",1)));
    w.turfBinds[0]=100;
    for (int y=0;y<9;++y) for (int x=0;x<9;++x) {
        const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),0};
        w.tiles[cell]=number("wall",0) && x==5 ? 2 : 1; w.areaCells[cell]=0;
    }
    auto atom=[&](uint32_t key,uint32_t ap,int x,int y,int offset) {
        auto& a=w.atoms[key]; a.hasPos=true; a.x=x; a.y=y; a.pixelX=offset; a.appid=ap;
        a.haveKey=true; a.key=uint16_t(key); a.renderIdentity=key+1ull;
        auto& cells=key&model::kRegistryA ? w.cellMobs : w.cellObjs;
        cells[{x,y,0}].push_back(key&~model::kRegistryA);
    };
    atom(42,3,number("targetx",8),5,number("offset",0)); atom(43,5,3,5,0);
    atom(model::kRegistryA|44,6,number("eyex",5),number("eyey",5),number("selfoffset",0));
    auto hud=[&](uint32_t key,uint32_t ap) {
        w.hud[key]={}; w.hudOrder.push_back(key); w.hudAppid[key]=ap; w.hudIdentity[key]={2,uint16_t(key),key+1ull};
    };
    hud(50,7); if (number("backdrop",0)) hud(51,8);
    const auto master=p.queryItemValue("master");
    if (!master.isEmpty()) {
        QJsonObject f{{"screenLoc","SOUTHWEST"},{"hasPlane",true},{"plane",number("masterplane",0)},{"appearanceFlags",0x10000000}};
        if (master=="alpha" || master=="targetalpha" || master=="zero") {
            f["hasColor"]=true; f["colorR"]=255; f["colorG"]=255; f["colorB"]=255;
            f["colorA"]=master=="zero" ? 0 : 128;
        }
        QJsonArray vars;
        if (master=="matrix") {
            auto color=identityColor; color[16]=.25f; color[18]=.5f;
            QByteArray bytes;
            for (float value:color) {
                auto bits=std::bit_cast<uint32_t>(value);
                for (int i=0;i<4;++i) bytes.append(char(bits>>(8*i)));
            }
            vars.append(QJsonObject{{"bit",1024},{"data",QString::fromLatin1(bytes.toHex())}});
        }
        if (master=="add" || master=="multiply")
            vars.append(QJsonObject{{"bit",256},{"data",master=="add" ? "02" : "04"}});
        f["vars"]=vars;
        if (master=="target" || master=="targetalpha") {
            const auto target=p.hasQueryItem("targetname") ? p.queryItemValue("targetname") : QString("*coverage");
            f["renderTarget"]=target;
            fields(10,{{"hasPlane",true},{"plane",5},{"screenLoc","SOUTHWEST"},
                       {"renderSource",target},{"appearanceFlags",0x3000}});
            if (p.queryItemValue("source")!="none") hud(53,10);
        }
        fields(9,f); hud(52,9);
    }
    return w;
}
inline TurfMap::Read blacknessReader() {
    std::array<QByteArray,8> data;
    const std::array<QColor,8> colors={QColor(60,80,100),QColor(200,200,200),QColor(240,50,40),QColor(240,50,40),
        QColor(50,200,240),QColor(40,230,70),QColor(230,210,30),QColor(80,40,120)};
    for (size_t i=0;i<data.size();++i) {
        QImage image(i==3 ? 64 : 32,32,QImage::Format_RGBA8888); image.fill(Qt::transparent);
        const int inset=i<2 ? 1 : i==6 ? 4 : i==7 ? 0 : 8;
        for (int y=inset;y<32-inset;++y) for (int x=inset;x<image.width()-inset;++x) image.setPixelColor(x,y,colors[i]);
        QBuffer buffer(&data[i]); buffer.open(QIODevice::WriteOnly); image.save(&buffer,"PNG");
    }
    return [data](uint32_t id,QByteArray& out) { if (!id || id>data.size()) return false; out=data[id-1]; return true; };
}
}
