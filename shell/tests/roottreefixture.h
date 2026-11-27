#pragma once
#include "tests/appearancejson.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/iconfixture.h"
#include "model/world.h"
#include <QPainter>

namespace roottreefixture {
namespace model = fugue::model;
inline QImage image(int index) {
    const QSize sizes[]={{32,32},{48,16},{16,48},{16,16},{32,32}};
    const QColor colors[]={{240,40,40},{40,180,240},{240,40,220},{40,230,70},{255,220,0}};
    QImage out(sizes[index],QImage::Format_RGBA8888); out.fill(Qt::transparent);
    QPainter p(&out); p.fillRect(QRect(2,3,4,out.height()-5),colors[index]);
    p.fillRect(QRect(6,out.height()-6,out.width()-8,4),colors[index]);
    return out;
}
inline bool read(uint32_t id,QByteArray& bytes) { bytes=iconfixture::png(image(int(id)-100)); return true; }
inline model::World world(bool mob=false,bool nested=false,bool wideRoot=false) {
    model::World w; w.bounds={true,20,20,0,5,5,5,5}; w.resync.haveOrigin=true;
    w.resync.originX=w.resync.originY=4;
    for(uint32_t i=0;i<5;++i) w.resources.push_back({100+i,0});
    auto& a=w.atoms[42 | (mob ? model::kRegistryA : 0)];
    a.hasPos=true; a.x=a.y=7; a.appid=1; a.key=99; a.haveKey=true; a.renderIdentity=10;
    for(uint32_t i=1;i<=4;++i) {w.appearances[i].fields.iconResID=i-1; setFields(w.appearances[i], "{}");
        w.appearances[i].fields.layerBits=std::bit_cast<uint32_t>(i==1 ? 3.f : -1.f);}
    if(wideRoot) w.appearances[1].fields.iconResID=1;
    setFields(w.appearances[1], R"({"pixelX":7,"pixelY":5,"underlays":[3],"overlays":[2]})");
    setFields(w.appearances[2], nested ? R"({"pixelX":25,"pixelY":9,"overlays":[4]})" : R"({"pixelX":25,"pixelY":9})");
    setFields(w.appearances[3], R"({"pixelX":-11,"pixelY":-7})");
    setFields(w.appearances[4], affinefixture::fields({1,0,0,0,1,0},",\"pixelX\":13,\"pixelY\":-5"));
    return w;
}
inline void pose(model::World& w,std::array<float,6> m) {
    setFields(w.appearances[1], affinefixture::fields(m,",\"pixelX\":7,\"pixelY\":5,\"underlays\":[3],\"overlays\":[2]"));
}
}
