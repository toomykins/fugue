#pragma once
#include "shell/mapsprite.h"

namespace fugue::shell::test {
inline MapSprite ink(int plane = 0) {
    MapSprite s; s.plane = plane; s.atom = 42; s.haveKey = true; s.key = 7; s.targetIdentity = 9;
    s.image = QImage(32,32,QImage::Format_RGBA8888); s.image.fill(Qt::red);
    s.iconBottom = {0,32}; s.cell = {1,1}; return s;
}
inline MapSprite master(int plane, QString target = {}) {
    MapSprite s; s.kind = MapSprite::Master; s.plane = plane; s.renderTarget = target; return s;
}
inline MapSprite relay(int plane, QString source) {
    MapSprite s; s.kind = MapSprite::Screen; s.plane = plane; s.renderSource = source;
    s.screenInteractive = true; s.atom = 43; return s;
}
// Deterministic small contributors over a large captured plane. Hidden RGB,
// tiny alpha, generated alpha and non-byte material coefficients are deliberate.
inline MapSprites contributorScene(QRect source, int count, int variant = 0) {
    auto raster = QImage(17,13,QImage::Format_RGBA8888);
    const int alpha[] = {0,1,3,7,17,63,128,193,255};
    for (int y=0;y<raster.height();++y) for (int x=0;x<raster.width();++x)
        raster.setPixelColor(x,y,QColor((x*73+y*29)%256,(x*31+y*53)%256,
            (x*17+y*97)%256,alpha[(x+3*y)%9]));
    MapSprites scene;
    for (int i=0;i<count;++i) {
        auto s=ink(1);s.image=raster;
        s.position=source.topLeft()+QPoint((i*19)%(source.width()+20)-10,(i*11)%(source.height()+16)-8);
        s.tint={.73f,.91f,.57f,.67f};s.blend=i%5;
        if (i%3==0) {s.color[19]=.21f;s.color[3]=.17f;s.color[16]=.13f;}
        if (variant==1) {s.size={23+i%7,19+i%3};s.smooth=true;}
        if (variant==7 || variant==8)
            s.position=source.topLeft()+QPoint(31+(i*3)%37,19+(i*7)%31);
        if (variant==2 && i%4==0) {
            StaticAffine a;a.forward=QTransform(.91,.27,-.19,1.03,.375,-.625);
            a.inverse=a.forward.inverted();a.bounds=a.forward.mapRect(QRectF(raster.rect())).toAlignedRect();s.affine=a;
        }
        scene.push_back(s);
    }
    if (variant==3 || variant==4 || variant==7 || variant==8) {
        auto outer=std::make_shared<AppearanceGroup>();outer->tint[3]=.81f;
        if (variant==7) outer->blend=count%5; // generated contributors exercise every supported blend
        if (variant==4 || variant==8) outer->transform=QTransform(.93,.21,-.17,1.07,.375,-.625);
        auto inner=std::make_shared<AppearanceGroup>();inner->parent=outer;inner->color[19]=.11f;
        for (int i=0;i<count;++i) {
            scene[i].appearanceGroup=i%3==0 ? inner : outer;
            scene[i].groupOccurrence=1;scene[i].groupOrigin=source.center();scene[i].groupLayer=float(i);
        }
    }
    auto m=master(1,"capture");
    if (variant==5 || variant==6) {
        auto effects=std::make_shared<PlaneEffects>();
        PlaneFilter outline;outline.kind=PlaneFilter::Outline;outline.size=1.5;outline.color={.7f,.2f,.9f,.53f};
        PlaneFilter blur;blur.size=1;
        if (variant==6) {outline.kind=PlaneFilter::Shadow;outline.x=.5;outline.y=-.75;}
        effects->filters={outline,blur};m.planeEffects=effects;
    }
    scene.push_back(m);return scene;
}
}
