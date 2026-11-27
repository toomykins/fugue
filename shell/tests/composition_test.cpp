// Layer-band ordering, menu stacks and HUD hit coordinates.
// Flat rasterization is only an ordering oracle; native tests exercise QRhi.
#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <limits>
#include <bit>
#include <cmath>
#include "shell/entitymap.h"
#include "shell/mapinput.h"
#include "shell/drawkey.h"
#include "shell/localtargets.h"
#include "shell/blurkernel.h"
#include "shell/surfaceplan.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/mastertreefixture.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/masterplacementfixture.h"
#include "model/verbmenu.h"
#include "model/statpanel.h"
using namespace fugue;
using namespace fugue::shell;
namespace {
QByteArray bytes(const QString& file) { QFile f(file); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
QList<QList<QByteArray>> rows(const QString& file) {
    QList<QList<QByteArray>> out;
    for (auto line : bytes(file).split('\n')) {
        line = line.trimmed(); if (!line.isEmpty() && !line.startsWith('#')) out.push_back(line.split('\t'));
    }
    return out;
}
struct Fixture : test::ReplayFixture {
    bool load(const QString& path, const QString& frames = "frames.txt") {
        return ReplayFixture::load(frames,path) && !frame.image.isNull();
    }
    std::optional<MapPointer> at(int x,int y) const {
        return mapPointerAt({double(x),double(frame.source.height()-1-y)},frame.source.size(),
            frame.source.size()/frame.geometry.tile,frame.source,frame.geometry,"mapwindow.map",true,sprites);
    }
    std::string name(const MapPointer& p) const { return targetName(p); }
    QImage raster() const {
        QImage out(frame.source.size(),QImage::Format_ARGB32_Premultiplied); out.fill(Qt::black);
        QPainter painter(&out);
        for (const auto& s : sprites) {
            if (s.image.isNull()) continue;
            painter.setOpacity(s.tint[3]); painter.drawImage(s.rect().translated(-frame.source.topLeft()),s.image);
        }
        return out;
    }
};
}
class Composition : public QObject {
    Q_OBJECT
private slots:
    void stockRotatedPlaneInput() {
        test::ReplayFixture f; QVERIFY(f.load("frames-plane-rotate.txt","master-tree-world"));
        auto at=[&](QPoint point,const MapSprites& sprites,QRect source) {
            return *mapPointerAt(point,source.size(),{7,7},source,f.frame.geometry,"main.map",true,sprites);
        };
        const auto ink=at({104,120},f.sprites,f.frame.source);
        QCOMPARE(f.targetName(ink),std::string("red"));
        QCOMPARE(ink.click.iconX,9); QCOMPARE(ink.click.iconY,24);
        QVERIFY(ink.click.hasVis); QCOMPARE(ink.click.visX,9); QCOMPARE(ink.click.visY,8);
        const auto edge=at({104,112},f.sprites,f.frame.source);
        QCOMPARE(f.targetName(edge),std::string("floor")); QVERIFY(!edge.click.hasVis);
        // The visible edge samples ink at its center, but stock tests the inverse
        // integer corner, which lands in the original source's alpha hole.
        const auto member=std::find_if(f.sprites.begin(),f.sprites.end(),[&](const auto& s){return s.kind==MapSprite::Entity && s.atom==ink.click.over.ref;});
        QVERIFY(member!=f.sprites.end());
        const auto master=std::find_if(f.sprites.begin(),f.sprites.end(),[&](const auto& s){return s.kind==MapSprite::Master && s.plane==member->plane;});
        QVERIFY(master!=f.sprites.end()); QVERIFY(master->planeEffects && master->planeEffects->matrix);
        const auto affine=StaticAffine::make(*master->planeEffects->matrix,f.frame.source.size()); QVERIFY(affine);
        auto alpha=[&](QPointF delta) {
            const auto p=affine->inverse.map(QPointF(104,112)+delta)+f.frame.source.topLeft()-member->position;
            return member->image.pixelColor(int(std::floor(p.x())),int(std::floor(p.y()))).alpha();
        };
        QVERIFY(alpha({.5,.5})>0); QCOMPARE(alpha({}),0);
        // Viewport origin is not the transform pivot in scene coordinates.
        // Translating the immutable frame cannot change any callback coordinate.
        auto moved=f.sprites; const QPoint offset(32,64);
        for(auto& s:moved) {s.position+=offset; s.iconBottom+=offset;}
        const auto shifted=at({104,120},moved,f.frame.source.translated(offset));
        QCOMPARE(shifted.click.iconX,ink.click.iconX); QCOMPARE(shifted.click.iconY,ink.click.iconY);
        QCOMPARE(shifted.click.hasVis,ink.click.hasVis);
        QCOMPARE(shifted.click.visX,ink.click.visX); QCOMPARE(shifted.click.visY,ink.click.visY);
        f.sprites.clear(); // later publications cannot alter this hit's scene
        QVERIFY(mapPointerCurrent(ink,f.session.world(),0));
        const auto retained=at({104,120},ink.renderedScene,ink.renderedSource);
        QCOMPARE(retained.click.visX,9); QCOMPARE(retained.click.visY,8);
        ++f.session.worldMut().atoms.at(ink.click.over.ref).renderIdentity;
        QVERIFY(!mapPointerCurrent(ink,f.session.world(),0));
    }
    void transientSurfaceLifetimes() {
        std::vector<SurfaceLifetime> chain;
        for(int i=0;i<200;++i) chain.push_back({QSize(512,512),i+1});
        auto plan=planSurfaces({32,32},chain,2);QVERIFY(plan.supported);
        QCOMPARE(plan.sizes.size(),size_t(3)); // two scratch slots and pinned output
        QCOMPARE(plan.storagePixels,uint64_t(32*32+3*512*512*2));
        QCOMPARE(plan.workPixels,uint64_t(32*32+200*512*512*2));
        for(int i=1;i<200;++i) QVERIFY(plan.slotForPass[i]!=plan.slotForPass[i-1]);
        // Work above the warning threshold still fits three textures and is admitted.
        for(auto& p:chain) p.size={1700,1700};
        QVERIFY(planSurfaces({32,32},chain,2).supported);
        chain.assign(2049,{{1,1},0});
        for(int i=0;i<2049;++i) chain[i].lastRead=i+1;
        QVERIFY(planSurfaces({1,1},chain,2).supported);
        chain.pop_back();chain.back().lastRead=2048;
        QVERIFY(planSurfaces({1,1},chain,2).supported);
        chain.assign(63,{{1,1},63});QVERIFY(planSurfaces({1,1},chain,2).supported);
        chain.assign(64,{{1,1},64});QVERIFY(planSurfaces({1,1},chain,2).supported);
        // Fan-out: pass zero is read again at pass three, not recyclable after
        // its first consumer. Both final outputs remain resident through replay.
        chain={{{64,32},3},{{32,64},2},{{32,32},4},{{32,32},4}};
        plan=planSurfaces({32,32},chain,2);QVERIFY(plan.supported);
        for(int i=0;i<4;++i) for(int j=i+1;j<4;++j)
            if(j<=chain[i].lastRead) QVERIFY(plan.slotForPass[i]!=plan.slotForPass[j]);
        // Growing/shrinking mixed domains may share only nonoverlapping lives;
        // each logical image must fit its physical capacity after all growth.
        chain.clear();
        for(int i=0;i<128;++i) chain.push_back({QSize(32+(i%7)*9,32+(i%5)*11),std::min(128,i+1+i%3)});
        plan=planSurfaces({32,32},chain,2);QVERIFY(plan.supported);
        QVERIFY(plan.sizes.size()<chain.size());
        for(int i=0;i<128;++i) {
            const auto capacity=plan.sizes[plan.slotForPass[i]];
            QVERIFY(capacity.width()>=chain[i].size.width() && capacity.height()>=chain[i].size.height());
            for(int j=i+1;j<128 && j<=chain[i].lastRead;++j) QVERIFY(plan.slotForPass[i]!=plan.slotForPass[j]);
        }
        QVERIFY(planSurfaces({32768,32768},std::array<SurfaceLifetime,1>{{{{8192,8192},1}}},2).supported);
        // The native 992x672 size-100 cascade has 14 logical passes, but only
        // two growing scratch allocations plus its exact-size pinned result.
        chain={{{992,672},1},{{992,672},2}};
        int x=0,y=0;
        for(const int radius:{249,125,63,32,6,18}) for(int axis=0;axis<2;++axis) {
            (axis==0?x:y)+=radius;
            chain.push_back({QSize(992+2*std::min(x,493-x),672+2*std::min(y,493-y)),int(chain.size()+1)});
        }
        plan=planSurfaces({992,672},chain,2);QVERIFY(plan.supported);
        QCOMPARE(plan.sizes.size(),size_t(3));
        QCOMPARE(plan.storagePixels*4,uint64_t(35468288));
        QCOMPARE(plan.workPixels,uint64_t(26390600));
    }
    void captureSurfaceFormats() {
        std::vector<SurfaceLifetime> passes;
        for (int i=0;i<32;++i) passes.push_back({{160,96},i+1,i%2});
        const auto plan=planSurfaces({160,96},passes,2);
        QVERIFY(plan.supported);
        QCOMPARE(plan.sizes.size(),size_t(3));
        // Format separation does not disable lifetime reuse or reduce budget
        // charges. No reader may alias its writer, even across format changes.
        QCOMPARE(plan.workPixels,uint64_t(160*96*(1+32*2)));
        for (int i=0;i<32;++i) {
            QCOMPARE(plan.storageClasses[plan.slotForPass[i]],passes[i].storageClass);
            if (i) QVERIFY(plan.slotForPass[i]!=plan.slotForPass[i-1]);
        }
        PlaneEffects effects; effects.matrix=std::array<double,6>{.5,0,0,0,.5,0};
        const auto neutral=effects.relayAffine({160,96},true);
        const auto processed=effects.relayAffine({160,96},false);
        QVERIFY(neutral && processed);
        QCOMPARE(neutral->forward.map(QPointF(68,38)),QPointF(114,67));
        QCOMPARE(processed->forward.map(QPointF(68,38)),QPointF(74,43));
        QCOMPARE(processed->inverse.map(QPointF(74,43)),QPointF(68,38));
    }
    void standaloneBlurKernelBounds() {
        const auto largest=stockBlurSteps(100);
        QCOMPARE(largest.count,6);
        QCOMPARE(largest.sizes,(std::array<float,6>{100,50,25,12.5,1.75,6}));
        QCOMPARE(stockBlurSteps(std::numeric_limits<double>::max()).sizes,largest.sizes);
        for(const double size:{-1.0,0.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
            QCOMPARE(stockBlurSteps(size).count,0);
        for(int i=1;i<=10000;++i) {
            const auto steps=stockBlurSteps(i/100.0);
            QVERIFY(steps.count>=1 && steps.count<=6);
            int support=0;
            for(int j=0;j<steps.count;++j) {
                const auto kernel=stockBlurKernel(steps.sizes[j]);
                double total=kernel.taps[1];
                for(int k=1;k<10;++k) {
                    QVERIFY(kernel.taps[2*k]>=0 && kernel.taps[2*k]<=249);
                    QVERIFY(kernel.taps[2*k+1]>=0);
                    total+=2*kernel.taps[2*k+1];
                }
                QVERIFY(std::abs(total-1)<1e-6);
                QVERIFY(kernel.radius>=0 && kernel.radius<=249);support+=kernel.radius;
            }
            QVERIFY(support<=493);
        }
    }
    void standaloneBlurRange() {
        const auto inf=std::numeric_limits<float>::infinity();
        for(const float size:{-1.f,0.f,.5f,4.f,4.25f,16.f,99.f,100.f,101.f,104.f,128.f,256.f,
                8193.f,std::numeric_limits<float>::max(),inf,-inf,std::numeric_limits<float>::quiet_NaN()}) {
            const auto raw=std::bit_cast<uint32_t>(size);
            QByteArray bytes;for(int i=0;i<4;++i) bytes.append(char(raw>>(8*i)));
            const QJsonObject blur{{"type",1},{"flags",1},{"paramMask",1},{"params",QString::fromLatin1(bytes.toHex())}};
            auto decode=[&](QJsonArray chain) {
                return decodePlaneEffects(model::appearanceOf(QJsonDocument(QJsonObject{{"filters",chain}}).toJson(QJsonDocument::Compact).toStdString()));
            };
            const bool admitted=std::isfinite(size) && size>=0;
            const auto effect=decode({blur});
            QVERIFY(effect.supported);
            QCOMPARE(effect.ignoredFilters,admitted ? size_t(0) : size_t(1));
            if(!admitted) QVERIFY(effect.filters.empty());
            if(admitted) {
                QCOMPARE(effect.filters[0].size,std::min(double(size),100.0));
                QVERIFY(effect.boundedTreeFilters());
                QVERIFY(!effect.requiresSimpleSources());
            }
            // The filter itself remains unsupported; worker decoding omits it.
            // Valid direct commands clamp before conversion, including DBL_MAX.
            PlaneEffects direct;PlaneFilter f;f.size=size;direct.filters={f};
            QCOMPARE(direct.valid({992,672}),admitted);
            if(size>4 && std::isfinite(size)) {
                const QJsonObject outline{{"type",2},{"flags",1},{"paramMask",0x23},{"params","00000040ff00ffff0000"}};
                for(const QJsonArray chain:{QJsonArray{outline,blur},QJsonArray{blur,outline},QJsonArray{blur,blur}}) {
                    const auto fallback=decode(chain);QVERIFY(fallback.supported);
                    QVERIFY(fallback.filters.empty());QCOMPARE(fallback.ignoredFilters,size_t(2));
                }
            }
        }
        PlaneEffects direct;PlaneFilter f;f.size=std::numeric_limits<double>::max();direct.filters={f};
        QVERIFY(direct.valid({992,672}));
    }
    void unsupportedPlaneFilterFallback() {
        const QJsonObject outline{{"type",2},{"flags",1},{"paramMask",0x23},{"params","00000040ff00ffff0000"}};
        const QJsonObject blur{{"type",1},{"flags",1},{"paramMask",1},{"params","0000803f"}};
        const QJsonObject unknown{{"type",99},{"flags",1}},malformed{{"type",1},{"flags",1},{"paramMask",1},{"params","bad hex"}};
        auto decode=[&](QJsonArray filters,QJsonObject fields=QJsonObject{}) {
            fields["filters"]=filters;
            return decodePlaneEffects(model::appearanceOf(QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString()));
        };
        const auto expected=decode({outline,blur});
        const auto mixed=decode({unknown,outline,malformed,blur,unknown});
        QVERIFY(mixed.supported);QCOMPARE(mixed.filters,expected.filters);QCOMPARE(mixed.ignoredFilters,size_t(3));
        QVERIFY(mixed.boundedTreeFilters());QVERIFY(!mixed.requiresSimpleSources());
        QJsonArray many;for(int i=0;i<9;++i) many.append(outline);
        const auto excessive=decode(many);QVERIFY(excessive.supported);QVERIFY(excessive.filters.empty());
        QCOMPARE(excessive.ignoredFilters,size_t(9));
        QJsonArray transform;for(const float value:{1.f,0.f,4.f,0.f,1.f,-3.f})
            transform.append(QJsonObject{{"f32",QString("0x%1").arg(std::bit_cast<uint32_t>(value),8,16,QLatin1Char('0'))}});
        const QJsonObject affine{{"hasTransform",true},{"transform",transform}};
        const auto transformed=decode({outline},affine);
        // A matrix keeps the filters, whatever their kind (master-transform-world filters).
        QVERIFY(transformed.supported);QVERIFY(transformed.matrix);QCOMPARE(transformed.filters,decode({outline}).filters);
        QCOMPARE(transformed.ignoredFilters,size_t(0));QCOMPARE(transformed.matrix,decode({},affine).matrix);
        QVERIFY(!transformed.boundedTreeFilters());
        const QJsonObject wave{{"type",5},{"flags",1},{"paramMask",0x3d},{"params","0000803f0000803f00000000000000000000"}};
        QCOMPARE(decode({wave}).filters.size(),size_t(1));
        const auto waved=decode({outline,wave},affine);
        QVERIFY(waved.supported);QVERIFY(waved.matrix);QCOMPARE(waved.filters.size(),size_t(2));QCOMPARE(waved.ignoredFilters,size_t(0));
        for(const QJsonObject fields:{QJsonObject{{"hasTransform",true},{"transform",QJsonArray{}}},
                QJsonObject{{"renderSource","*unsupported"}},QJsonObject{{"maptext","unsupported"}}})
            QVERIFY(!decode({unknown},fields).supported); // no structural fail-open
        auto color=identityColor;color[19]=.5f;
        QByteArray bytes;for(float value:color) {const auto raw=std::bit_cast<uint32_t>(value);for(int i=0;i<4;++i) bytes.append(char(raw>>(8*i)));}
        const QJsonObject colorVar{{"bit",0x400},{"data",QString::fromLatin1(bytes.toHex())}};
        const QJsonObject material{{"vars",QJsonArray{colorVar}}};
        const auto alphaMatrix=decode({blur},material);
        QVERIFY(alphaMatrix.supported);QVERIFY(alphaMatrix.filters.empty());QCOMPARE(alphaMatrix.ignoredFilters,size_t(1));
    }
    void orderedStockAdmissionAndProvenance() {
        auto blur=QJsonObject{{"type",1},{"flags",1},{"paramMask",1},{"params","0000803f"}};
        auto outline=QJsonObject{{"type",2},{"flags",1},{"paramMask",0x23},{"params","00000040ff00ffff0000"}};
        auto decode=[&](QJsonArray chain) {
            return decodePlaneEffects(model::appearanceOf(QJsonDocument(QJsonObject{{"filters",chain}}).toJson(QJsonDocument::Compact).toStdString()));
        };
        auto forward=decode({outline,blur}),reverse=decode({blur,outline});
        QVERIFY(forward.boundedTreeFilters()); QVERIFY(reverse.boundedTreeFilters());
        QVERIFY(!forward.requiresSimpleSources()); QVERIFY(reverse.requiresSimpleSources());
        auto omitted=[&](QJsonArray chain) {
            const auto result=decode(chain);
            return result.supported && result.filters.empty() && result.ignoredFilters==size_t(chain.size());
        };
        QVERIFY(decode({blur,blur,outline}).boundedTreeFilters());
        QJsonArray eight;for(int i=0;i<4;++i) {eight.append(blur);eight.append(outline);}
        QVERIFY(decode(eight).requiresSimpleSources());eight.append(blur);QVERIFY(omitted(eight));
        // The record byte numbers the filter among those of its type (stock 516.1689, stock-parity-world repeated-filters).
        auto second=blur;second["flags"]=2;
        const auto kept=decode({outline,second});QVERIFY(kept.supported);
        QCOMPARE(kept.filters,decode({outline,blur}).filters);QCOMPARE(kept.ignoredFilters,size_t(0));
        auto wide=outline;wide["params"]="00008040ff00ffff0000";QVERIFY(decode({wide,blur}).boundedTreeFilters());
        for(float size:{2.75f,3.125f,4.25f,5.5f,31.5f,33.25f,64.5f,100.f,256.25f,512.f,1024.f,8192.f}) for(int alpha:{0,1,128,254,255}) for(int flags:{0,1,2,3}) {
            QByteArray bytes;const auto bits=std::bit_cast<uint32_t>(size);
            for(int i=0;i<4;++i) bytes.append(char(bits>>(8*i)));
            wide["params"]=QString::fromLatin1(bytes.toHex())+"c04080"+QString("%1%2").arg(alpha,2,16,QLatin1Char('0')).arg(flags,2,16,QLatin1Char('0'))+"00";
            QVERIFY(decode({wide,blur}).boundedTreeFilters());
            QVERIFY(decode({blur,wide}).requiresSimpleSources());
            QVERIFY(decode({wide,wide,blur}).relayFilters());
        }
        auto large=blur;large["params"]="00008040";
        QVERIFY(decode({large}).supported);QVERIFY(decode({outline,large}).boundedTreeFilters());
        large["params"]="01008040";QVERIFY(decode({large}).supported);QVERIFY(omitted({outline,large}));
        blur["params"]="000080bf";QVERIFY(omitted({blur}));

        test::ReplayFixture f; QVERIFY(f.load("frames-filtered-reverse.txt","master-tree-world"));
        const auto old=f.sprites;
        auto pick=[&](const MapSprites& sprites) {return mapPointerAt({40,168},f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"main.map",true,sprites);};
        const auto hit=pick(old);QVERIFY(hit && hit->screen && hit->filteredPlane);
        auto& world=f.session.worldMut();const auto root=world.hudAppid.at(hit->click.over.ref);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(world.appearances.at(root)))).object();
        const auto leaf=uint32_t(fields["overlays"].toArray()[0].toInt());
        const auto original=fieldsJson(world.appearances.at(leaf));
        auto partial=QJsonDocument::fromJson(QByteArray::fromStdString(original)).object();
        partial["hasColor"]=true;partial["colorR"]=partial["colorG"]=partial["colorB"]=255;partial["colorA"]=128;
        setFields(world.appearances.at(leaf), QJsonDocument(partial).toJson(QJsonDocument::Compact).toStdString());f.refresh();
        QVERIFY(pick(f.sprites)->filteredPlane); // scalar fractional alpha now admitted
        partial["vars"]=QJsonArray{QJsonObject{{"bit",256},{"data","02"}}}; // nonnormal source still refuses
        setFields(world.appearances.at(leaf), QJsonDocument(partial).toJson(QJsonDocument::Compact).toStdString());f.refresh();
        QVERIFY(!pick(f.sprites)->filteredPlane);
        QVERIFY(pick(old)->screen);QVERIFY(pick(old)->filteredPlane);
        QVERIFY(mapPointerCurrent(*hit,world,0));
        QVERIFY(mapPointerStack(*hit).front().filteredPlane);
        auto hover=*hit;hover.kind=MapPointer::Hover;QVERIFY(mapPointerCurrent(hover,world,0));
        auto drag=*hit;drag.kind=MapPointer::Drag;drag.source=std::make_shared<MapPointer>(*hit);QVERIFY(mapPointerCurrent(drag,world,0));
        ++world.hudIdentity.at(hit->click.over.ref).membership;QVERIFY(!mapPointerCurrent(*hit,world,0));
        QVERIFY(!mapPointerCurrent(hover,world,0));QVERIFY(!mapPointerCurrent(drag,world,0));
        QVERIFY(!mapPointerCurrent(mapPointerStack(*hit).front(),world,0));
        setFields(world.appearances.at(leaf), original);f.refresh();QVERIFY(pick(f.sprites)->screen);
        QVERIFY(pick(f.sprites)->filteredPlane); // provenance recovery restores the admitted chain
    }
    void localForwardingLifecycle() {
        test::ReplayFixture f;
        QVERIFY(f.load("frames-forward.txt"));
        auto validForward=[](const MapSprites& sprites) {
            const LocalTargets plan(sprites); const int i=plan.find("*forward");
            return i>=0 && plan.groups[size_t(i)].valid;
        };
        QVERIFY(validForward(f.sprites));
        const auto rendered=f.sprites;
        const auto source=std::find_if(rendered.begin(),rendered.end(),[](const auto& s){return s.renderTarget=="*local";});
        const auto forward=std::find_if(rendered.begin(),rendered.end(),[](const auto& s){return s.renderTarget=="*forward";});
        QVERIFY(source!=rendered.end()); QVERIFY(forward!=rendered.end());
        auto& world=f.session.worldMut();
        const auto sourceApp=world.hudAppid.at(source->atom), forwardApp=world.hudAppid.at(forward->atom);
        auto mutate=[&](uint32_t id,const char* key,const QJsonValue& value) {
            auto& app=world.appearances.at(id);
            auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(app))).object();
            fields[key]=value; setFields(app, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            f.refresh();
        };
        mutate(sourceApp,"renderTarget","*renamed"); QVERIFY(!validForward(f.sprites));
        QVERIFY(validForward(rendered)); // pending rebinding cannot mutate old geometry
        mutate(sourceApp,"renderTarget","*local"); QVERIFY(validForward(f.sprites));
        mutate(forwardApp,"renderSource","*forward"); QVERIFY(!validForward(f.sprites)); // self-cycle
        mutate(forwardApp,"renderSource","*local"); QVERIFY(validForward(f.sprites));
        mutate(forwardApp,"colorA",128); mutate(forwardApp,"hasColor",true);
        QVERIFY(!validForward(f.sprites)); // no new local material/effect admission
        mutate(forwardApp,"hasColor",false); QVERIFY(validForward(f.sprites));
        const auto hud=world.hud.at(source->atom); const auto order=world.hudOrder;
        world.hud.erase(source->atom); world.hudAppid.erase(source->atom);
        std::erase(world.hudOrder,source->atom); f.refresh();
        QVERIFY(!validForward(f.sprites)); QVERIFY(validForward(rendered));
        world.hud[source->atom]=hud; world.hudAppid[source->atom]=sourceApp; world.hudOrder=order;
        ++world.hudIdentity.at(source->atom).membership; f.refresh();
        QVERIFY(validForward(f.sprites));
        // Chained input is still refused even when its pixels are admitted.
        const auto hit=mapPointerAt({40,168},f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"main.map",true,f.sprites);
        QVERIFY(hit); QVERIFY(!hit->screen); QVERIFY(!hit->relayHit);
    }
    void wideOutlineAdmission() {
        PlaneEffects effects;
        PlaneFilter outline; outline.kind=PlaneFilter::Outline;
        effects.filters={outline};
        for (double size : {0.0,.125,.5,1.5,2.75,4.25,8.0,16.0,31.5,32.0,33.25,64.5,100.0,256.25,512.0,1024.0,8192.0}) {
            effects.filters[0].size=size;
            QVERIFY(effects.valid({224,224}));QVERIFY(effects.boundedTreeFilters());
        }
        for (double size : {-.01,8192.0001,8193.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            effects.filters[0].size=size;
            QVERIFY(!effects.valid({224,224}));QVERIFY(!effects.boundedTreeFilters());
        }
        effects.filters[0].size=32;
        const auto admitted=effects;
        for (int flags : {1,2,3}) {effects=admitted;effects.filters[0].flags=flags;QVERIFY(effects.boundedTreeFilters());}
        for (int flags : {4,8,-1}) {effects=admitted;effects.filters[0].flags=flags;QVERIFY(!effects.valid({224,224}));}
        for(float alpha:{0.f,1.f/255,.5f,254.f/255,1.f}) {
            effects=admitted;effects.filters[0].color[3]=alpha;QVERIFY(effects.boundedTreeFilters());QVERIFY(effects.relayFilters());
        }
        for(float alpha:{-.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            effects=admitted;effects.filters[0].color[3]=alpha;QVERIFY(!effects.valid({224,224}));
        }
        effects=admitted;effects.filters.resize(9,outline);QVERIFY(!effects.valid({224,224}));
        effects=admitted;effects.matrix=std::array<double,6>{1,0,0,0,1,0};
        QVERIFY(effects.valid({224,224}));QVERIFY(!effects.boundedTreeFilters()); // drawn, but not a tree's filters
        for (auto kind : {PlaneFilter::Blur,PlaneFilter::Shadow}) {
            effects=admitted;effects.filters[0].kind=kind;QVERIFY(effects.valid({224,224}));
        }
    }
    void stockMasterTreeCallbacks() {
        QStringList phases;QString inventoryError;
        QVERIFY2(test::MasterTreeFixture::phases(phases,inventoryError),qPrintable(inventoryError));
        for(const auto& phase:phases) {
            test::MasterTreeFixture f; QVERIFY2(f.load(phase),qPrintable(f.error));
            for(int i=0;i<f.callbacks.size();++i) {
                const auto p=f.clickPoint(i);
                const auto hit=mapPointerAt(p,f.frame.source.size(),{7,7},f.frame.source,
                    f.frame.geometry,"map",true,f.sprites);
                QVERIFY(hit); const auto error=f.checkClick(*hit,i);
                QVERIFY2(error.isEmpty(),qPrintable(phase+": "+error));
                QVERIFY(mapPointerCurrent(*hit,f.session.world(),0));
                if(hit->screen) { // a HUD hit names this membership of the screen, not a later one
                    auto& world=f.session.worldMut();const auto id=hit->click.over.ref;
                    ++world.hudIdentity.at(id).membership;QVERIFY(!mapPointerCurrent(*hit,world,0));--world.hudIdentity.at(id).membership;
                }
            }
        }
    }
    void positionedMasterCallbacks() {
        QJsonArray rows;QString inventoryError;
        QVERIFY2(test::MasterPlacementFixture::rows(rows,inventoryError),qPrintable(inventoryError));
        int clicks=0;
        for(const auto& value:rows) {
            const auto row=value.toObject();test::MasterPlacementFixture f;QVERIFY2(f.load(row),qPrintable(f.error));
            const auto points=row["points"].toArray(),callbacks=row["callbacks"].toArray();
            QCOMPARE(points.size(),callbacks.size());
            for(int i=0;i<points.size();++i) {
                const auto xy=points[i].toArray();
                const auto hit=mapPointerAt(QPointF(xy[0].toInt(),xy[1].toInt()),f.frame.source.size(),{7,7},f.frame.source,
                    f.frame.geometry,"main.map",true,f.sprites);
                QVERIFY(hit);const auto error=f.checkClick(*hit,callbacks[i].toObject());
                QVERIFY2(error.isEmpty(),qPrintable(row["kind"].toString()+"/"+row["placement"].toString()+": "+error));
                QVERIFY(mapPointerCurrent(*hit,f.session.world(),0));++clicks;
            }
        }
        QCOMPARE(clicks,135);
    }
    void positionedMasterMotionAndRefusal() {
        QJsonArray rows;QString error;
        QVERIFY2(test::MasterPlacementFixture::rows(rows,error),qPrintable(error));
        const auto row=std::find_if(rows.begin(),rows.end(),[](const auto& v) {
            const auto r=v.toObject();return r["kind"]=="overlay" && r["placement"]=="center";
        });QVERIFY(row!=rows.end());
        test::MasterPlacementFixture f;QVERIFY2(f.load(row->toObject()),qPrintable(f.error));
        auto pick=[&](QPoint point,const MapSprites& sprites) {
            return *mapPointerAt(point,f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"main.map",true,sprites);
        };
        const auto old=f.sprites;const auto oldHit=pick({135,71},old);QVERIFY(oldHit.screen);
        f.setLocation("2:7,2:5");QVERIFY(!pick({135,71},f.sprites).screen);
        auto moved=pick({78,130},f.sprites);QVERIFY(moved.screen);
        QCOMPARE(moved.click.iconX,8);QCOMPARE(moved.click.iconY,25);
        QCOMPARE(pick({135,71},old).click.over.ref,oldHit.click.over.ref);
        QVERIFY(mapPointerCurrent(oldHit,f.session.world(),0));
        auto refused=[&] {
            bool master=false;
            for(const auto& s:f.sprites) {
                if(s.kind==MapSprite::Screen) return false;
                if(s.kind==MapSprite::Master) {master=true;if(!s.planeEffects || s.planeEffects->supported)return false;}
            }
            return master;
        };
        // Without a screen_loc the master and its overlay are not drawn at all (stock 516.1689, master-screen-loc-world).
        f.setLocation("");
        QVERIFY(std::none_of(f.sprites.begin(),f.sprites.end(),[](const auto& s) {
            return s.kind==MapSprite::Master || s.kind==MapSprite::Screen; }));
        // Text that is not coordinates does the same ("bogus" there).
        f.setLocation("invalid");
        QVERIFY(std::none_of(f.sprites.begin(),f.sprites.end(),[](const auto& s) {
            return s.kind==MapSprite::Master || s.kind==MapSprite::Screen; }));
        // A range acts once and draws the overlay at each position ("1,1 to 3,3" there).
        auto counts=[&] {
            std::pair<int,int> n{0,0};
            for(const auto& s:f.sprites) {
                if(s.kind==MapSprite::Master) {++n.first;if(!s.planeEffects || !s.planeEffects->supported) n.first=-100;}
                if(s.kind==MapSprite::Screen) ++n.second;
            }
            return n;
        };
        f.setLocation("1,1");const auto single=counts();QCOMPARE(single.first,1);QVERIFY(single.second>0);
        f.setLocation("1,1 TO 2,1");QCOMPARE(counts(),std::pair(1,2*single.second));
        // The second end's pixel offset is ignored ("1,1 to 3:8,3" there).
        f.setLocation("1,1 TO 2:3,1");QCOMPARE(counts(),std::pair(1,2*single.second));
        // A range with a percent end is placed like an object's: one overlay for "1,1 to 50%,50%", two ends on one tile.
        f.setLocation("1,1 TO 50%,50%");QCOMPARE(counts(),single);
        // A form this does not place is refused (different pixel offsets on a RIGHT end were not tried).
        f.setLocation("1,1 TO RIGHT:8,1");QVERIFY(refused());
        // Off the view the master acts; its overlay is outside this scene, which the fixture does not enlarge ("8,1" there).
        f.setLocation("8,1");QVERIFY(!refused());QCOMPARE(counts(),std::pair(1,0));
        QCOMPARE(f.entities.hudMargins(),QMargins(0,0,1,0));
        auto& w=f.session.worldMut();w.bounds.maxX=std::max(w.bounds.maxX,9);w.bounds.maxY=std::max(w.bounds.maxY,9); // a world the view fits in
        w.bounds.viewW=9;w.bounds.viewH=9;f.refresh();
        QCOMPARE(counts(),single); // Same cached template is drawn in the larger view.
        w.bounds.viewW=7;w.bounds.viewH=7;f.refresh();QCOMPARE(counts(),std::pair(1,0));
        w.bounds.viewW=9;w.bounds.viewH=9;f.refresh();QCOMPARE(counts(),single);
        f.setLocation("CENTER,CENTER");
        auto leaf=std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen && !s.image.isNull();});
        QVERIFY(leaf!=f.sprites.end());QCOMPARE(leaf->position-f.frame.source.topLeft(),QPoint(160,96));
        w.bounds.viewW=7;w.bounds.viewH=7;f.refresh();
        leaf=std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen && !s.image.isNull();});
        QVERIFY(leaf!=f.sprites.end());QCOMPARE(leaf->position-f.frame.source.topLeft(),QPoint(128,64));
        ++w.hudIdentity.at(oldHit.click.over.ref).membership;
        QVERIFY(!mapPointerCurrent(oldHit,w,0));QVERIFY(!mapPointerCurrent(moved,w,0));
        const auto escape=std::find_if(rows.begin(),rows.end(),[](const auto& v) {
            const auto r=v.toObject();return r["kind"]=="overlay-group-leafplane" && r["placement"]=="center";
        });QVERIFY(escape!=rows.end());
        test::MasterPlacementFixture unmastered;QVERIFY(unmastered.load(escape->toObject()));
        auto& wide=unmastered.session.worldMut();wide.bounds.viewW=9;wide.bounds.viewH=9;unmastered.refresh();
        const auto escaped=std::find_if(unmastered.sprites.begin(),unmastered.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen && !s.image.isNull();});
        QVERIFY(escaped!=unmastered.sprites.end());
        QCOMPARE(escaped->iconBottom,QPoint(unmastered.frame.source.left(),unmastered.frame.source.bottom()+1));
    }
    void masterTreeRefusalAndLifetimes_data() {
        QTest::addColumn<QString>("phase");
        QTest::newRow("ungrouped") << QString("overlay-nested-tint");
        QTest::newRow("grouped-escape") << QString("g-overlay-nested-group-apart-overlap-alpha");
        QTest::newRow("outlined-escape") << QString("o-overlay-nested-group-apart-outline-childoutline-overlap-alpha");
        QTest::newRow("size-two-escape") << QString("q-overlay-nested-group-apart-outline-childoutline-overlap-alpha-size2");
        QTest::newRow("wide-escape") << QString("w-overlay-nested-group-apart-outline-childoutline-overlap-alpha-radius32");
        QTest::newRow("fractional-escape") << QString("w-overlay-nested-group-apart-outline-childoutline-leafplane-tint-radius4.25-childradius32");
    }
    void masterTreeRefusalAndLifetimes() {
        QFETCH(QString,phase);
        test::MasterTreeFixture f;QVERIFY2(f.load(phase),qPrintable(f.error));
        auto& w=f.session.worldMut();QCOMPARE(w.hudAppid.size(),size_t(1));
        const auto rootId=w.hudAppid.begin()->second;
        const auto original=fieldsJson(w.appearances.at(rootId));
        const auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(original)).object();
        const auto old=f.sprites;
        auto pick=[&](const MapSprites& sprites,int i) {
            return mapPointerAt(test::MasterTreeFixture::point(i),f.frame.source.size(),{7,7},f.frame.source,
                f.frame.geometry,"map",true,sprites);
        };
        const auto hit=pick(old,2);QVERIFY(hit && hit->screen);QVERIFY(mapPointerCurrent(*hit,w,0));
        const auto stack=mapPointerStack(*hit);QVERIFY(!stack.empty());
        if (hit->filteredPlane) {
            QVERIFY(stack.front().screen);QVERIFY(stack.front().filteredPlane);
            auto hover=*hit;hover.kind=MapPointer::Hover;QVERIFY(mapPointerCurrent(hover,w,0));
            auto drag=*hit;drag.kind=MapPointer::Drag;drag.source=std::make_shared<MapPointer>(*hit);
            QVERIFY(mapPointerCurrent(drag,w,0));
        } else QCOMPARE(stack.front().click.over.ref,hit->click.over.ref);
        const auto ref=hit->click.over.ref;
        ++w.hudIdentity.at(ref).membership;QVERIFY(!mapPointerCurrent(*hit,w,0));--w.hudIdentity.at(ref).membership;
        ++w.hudIdentity.at(ref).key;QVERIFY(!mapPointerCurrent(*hit,w,0));--w.hudIdentity.at(ref).key;
        const auto hud=w.hud.at(ref);w.hud.erase(ref);QVERIFY(!mapPointerCurrent(*hit,w,0));w.hud[ref]=hud;
        QVERIFY(!mapPointerCurrent(*hit,w,1));
        auto refused=[&] {
            f.refresh();
            int masters=0;
            for(const auto& s:f.sprites) {
                if(s.kind==MapSprite::Screen) return false;
                if(s.kind==MapSprite::Master) {++masters;if(!s.planeEffects || s.planeEffects->supported)return false;}
            }
            return masters>0;
        };
        for(const QString key:{"reset","apart","filter","outline-size","outline-fraction","outline-negative","outline-nan","outline-infinity","outline-flags","outline-unknown-flags","outline-alpha","outline-record","outline-mask","outline-hex","chain","transform","target","text","placement","offset","cycle","work","siblings"}) {
            auto changed=fields;
            if(key=="reset" || key=="apart") changed["appearanceFlags"]=double(uint32_t(fields["appearanceFlags"].toDouble()) | (key=="reset" ? 0x00400000u : 0x08000000u));
            if(key=="filter") changed["filters"]=QJsonArray{QJsonObject{{"type",99},{"flags",1}}};
            if(key.startsWith("outline-") || key=="chain") {
                // Extended sizes/alpha and known outline flags admit; unknown bits,
                // malformed parameters and nine-operation chains still fall back.
                QJsonObject filter{{"type",2},{"flags",1},{"paramMask",35},{"params",QJsonObject{{"bstr",
                    key=="outline-size" ? "00000442ff00ffff0000" :
                    key=="outline-fraction" ? "00000242ff00ffff0000" :
                    key=="outline-negative" ? "000080bfff00ffff0000" :
                    key=="outline-nan" ? "0000c07fff00ffff0000" :
                    key=="outline-infinity" ? "0000807fff00ffff0000" :
                    key=="outline-flags" ? "0000803fff00ffff0100" :
                    key=="outline-unknown-flags" ? "0000803fff00ffff0400" :
                    key=="outline-alpha" ? "0000803fff00ff800000" : "0000803fff00ffff0000"}}}};
                if(key=="outline-record") filter["flags"]=2;
                if(key=="outline-mask") filter["paramMask"]=0x40;
                if(key=="outline-hex") filter["params"]="invalid hex";
                changed["filters"]=key=="chain" ? QJsonArray{filter,filter,filter,filter,filter,filter,filter,filter,filter} : QJsonArray{filter};
            }
            if(key=="transform") {changed["hasTransform"]=true;changed["transform"]=QJsonArray{};}
            if(key=="target") changed["renderTarget"]="*refused";
            if(key=="text") changed["maptext"]="refused";
            if(key=="placement") changed["screenLoc"]="1,1 TO RIGHT:8,1"; // a range this does not place
            if(key=="offset") changed["pixelX"]=7;
            auto children=fields["overlays"].toArray();
            if(key=="cycle") children.append(int(rootId));
            if(key=="work") for(int i=0;i<4096;++i) children.append(children[0]);
            if(key=="siblings") children.append(children[1]);
            changed["overlays"]=children;
            setFields(w.appearances.at(rootId), QJsonDocument(changed).toJson(QJsonDocument::Compact).toStdString());
            const bool filterFallback=key=="filter" || key.startsWith("outline-") || key=="chain";
            const bool admitted=filterFallback || key=="reset"; // stock material continuation admits RGB resets
            QCOMPARE(refused(),!admitted);
            const bool extended=key=="outline-size" || key=="outline-fraction" || key=="outline-alpha" || key=="outline-flags" ||
                key=="outline-record";
            if(filterFallback) QVERIFY(std::any_of(f.sprites.begin(),f.sprites.end(),[&](const auto& s){
                return s.kind==MapSprite::Master && s.planeEffects && s.planeEffects->supported &&
                    (extended ? !s.planeEffects->ignoredFilters && !s.planeEffects->filters.empty() : bool(s.planeEffects->ignoredFilters));
            }));
            // Fallback/refused publications cannot mutate previously rendered input.
            QCOMPARE(pick(old,2)->click.over.ref,hit->click.over.ref);
            QVERIFY(f.checkClick(*pick(old,2),2).isEmpty());
            QCOMPARE(pick(f.sprites,2)->screen,admitted);
        }
        setFields(w.appearances.at(rootId), original);f.refresh();QVERIFY(f.checkClick(*pick(f.sprites,2),2).isEmpty());
        // Missing assets retain selected-plane refusal markers, then retry on arrival.
        EntityMap late;
        const auto missing=late.prepare(w,f.frame,[](uint32_t,QByteArray&){return false;},0,nullptr,true);
        for(const auto& s:missing) if(s.kind==MapSprite::Master) QVERIFY(!s.planeEffects->supported);
        const auto arrived=late.prepare(w,f.frame,[&](uint32_t id,QByteArray& out){return f.read(id,out);},0,nullptr,true);
        QVERIFY(f.checkClick(*pick(arrived,2),2).isEmpty());
        // The whole bounded chain refuses on a missing child and recovers without
        // resurrecting a prior overlay or dropping the known master markers.
        const auto childId=uint32_t(fields["overlays"].toArray()[0].toInt());
        const auto child=w.appearances.at(childId);w.appearances.erase(childId);QVERIFY(refused());
        w.appearances[childId]=child;f.refresh();QVERIFY(f.checkClick(*pick(f.sprites,2),2).isEmpty());
        // Identical appearance bytes on a world occurrence are not HUD admission.
        auto red=std::find_if(w.atoms.begin(),w.atoms.end(),[&](const auto& item) {
            const auto ap=w.appearances.find(item.second.appid);
            return ap!=w.appearances.end() && model::parseJson(fieldsJson(ap->second))["name"].s=="red";
        });
        QVERIFY(red!=w.atoms.end());red->second.appid=rootId;w.hudOrder.clear();f.refresh();
        int refusedWorldMasters=0;
        for(const auto& s:f.sprites) if(s.kind==MapSprite::Master) {
            QVERIFY(!s.planeEffects->supported);++refusedWorldMasters;
        }
        QVERIFY(refusedWorldMasters>0);
    }
    void outlinedTreeInputBoundaries_data() {
        QTest::addColumn<QString>("phase");
        QTest::newRow("size-one") << QString("o-overlay-group-apart-outline");
        QTest::newRow("size-two") << QString("q-overlay-group-apart-outline-size2");
        QTest::newRow("wide") << QString("w-overlay-group-apart-outline-radius32");
        QTest::newRow("fractional") << QString("w-overlay-group-apart-outline-radius4.25");
    }
    void outlinedTreeInputBoundaries() {
        QFETCH(QString,phase);
        test::MasterTreeFixture f;QVERIFY2(f.load(phase),qPrintable(f.error));
        const auto old=f.sprites;
        auto pick=[&](const MapSprites& sprites,bool defaultMap=true) {
            return mapPointerAt(f.clickPoint(2),f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"map",defaultMap,sprites);
        };
        QVERIFY(pick(old)->filteredPlane);QVERIFY(pick(old)->screen);
        QVERIFY(!pick(old,false)->screen); // no secondary-MAP extension
        // A filtered plane's own ink is clickable under other filter kinds and a matrix too (stock 516.1689,
        // master-transform-world clicks).
        // So is a client image's (stock 516.1689, plane-click-world outline1).
        for(const QString key:{"wave","turned","affine","image"}) {
            auto changed=old;
            for(auto& sprite:changed) if(sprite.kind==MapSprite::Screen && key=="affine")
                sprite.affine=StaticAffine::make({1,0,0,0,1,0},{32,32}); // a part that carries a matrix of its own
            for(auto& sprite:changed) if(sprite.kind==MapSprite::Screen && key=="image") sprite.imageIdentity=1;
            for(auto& sprite:changed) if(sprite.kind==MapSprite::Master) {
                auto effects=std::make_shared<PlaneEffects>(*sprite.planeEffects);
                if(key=="wave") {PlaneFilter wave;wave.kind=PlaneFilter::Wave;wave.x=16;wave.size=3;effects->filters={wave};}
                if(key=="turned") effects->matrix=std::array<double,6>{1,0,0,0,1,0};
                sprite.planeEffects=effects;
            }
            QVERIFY2(pick(changed)->screen,qPrintable(key));
        }
        for(const QString key:{"chain","opacity2","scaled","text","relay","blend"}) {
            auto changed=old;
            for(auto& sprite:changed) {
                if(sprite.kind==MapSprite::Master && key=="chain") {
                    auto effects=std::make_shared<PlaneEffects>(*sprite.planeEffects);
                    effects->filters.resize(9,effects->filters.front());
                    sprite.planeEffects=effects;
                }
                if(sprite.kind!=MapSprite::Screen) continue;
                if(key=="opacity2") sprite.mouseOpacity=2;
                if(key=="scaled") sprite.size={64,64};
                if(key=="text") sprite.maptext=true;
                if(key=="relay") sprite.renderSource="unverified";
                if(key=="blend") sprite.blend=3;
            }
            QVERIFY2(!pick(changed)->screen,qPrintable(key));
            QVERIFY(pick(old)->screen); // no mutation of rendered evidence
        }
    }
    void masterGroupingStructuralBoundaries() {
        test::MasterTreeFixture f;QVERIFY2(f.load("g-overlay-group-apart"),qPrintable(f.error));
        auto& w=f.session.worldMut();const auto rootId=w.hudAppid.begin()->second;
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(w.appearances.at(rootId)))).object();
        const auto childId=uint32_t(fields["overlays"].toArray()[0].toInt());
        auto& child=w.appearances.at(childId);
        const auto original=fieldsJson(child);
        const auto escaped=f.sprites;
        auto pick=[&](const MapSprites& sprites) {
            return mapPointerAt(f.clickPoint(2),f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"map",true,sprites);
        };
        QVERIFY(pick(escaped)->screen);
        auto childFields=QJsonDocument::fromJson(QByteArray::fromStdString(original)).object();
        childFields["appearanceFlags"]=4; // Captured ordinary leaf: remove KEEP_APART.
        setFields(child, QJsonDocument(childFields).toJson(QJsonDocument::Compact).toStdString());f.refresh();
        QVERIFY(!pick(f.sprites)->screen);
        for(const auto& entry:mapPointerStack(*pick(f.sprites))) QVERIFY(!entry.screen);
        QVERIFY(pick(escaped)->screen); // immutable old occurrence
        for(const auto& s:f.sprites) if(s.kind==MapSprite::Master) QVERIFY(s.planeEffects->supported);
        // Even a suppressed local leaf is validated: unsupported effects cannot
        // silently become admitted merely because its ink would be discarded.
        for(const QString key:{"group","both-group-flags","reset","relative-plane","bounds","descendant"}) {
            auto changed=childFields;
            if(key=="group") changed["appearanceFlags"]=double(0x04000004u);
            if(key=="both-group-flags") changed["appearanceFlags"]=double(0x0c000004u);
            if(key=="reset") changed["appearanceFlags"]=double(0x00400004u);
            if(key=="relative-plane") changed["plane"]=-32766;
            if(key=="bounds") changed["pixelX"]=8193;
            if(key=="descendant") changed["overlays"]=QJsonArray{int(childId)};
            setFields(child, QJsonDocument(changed).toJson(QJsonDocument::Compact).toStdString());f.refresh();
            for(const auto& s:f.sprites) {
                QVERIFY(s.kind!=MapSprite::Screen);
                if(s.kind==MapSprite::Master) QCOMPARE(s.planeEffects->supported,key=="reset");
            }
        }
        setFields(child, original);f.refresh();QVERIFY(f.checkClick(*pick(f.sprites),2).isEmpty());
        // The server removes assigned master offsets from the captured wire.
        // These are admitted zero-offset controls, NOT evidence to ignore a
        // nonzero wire offset (the synthetic refusal above retains that gate).
        for(const QString phase:{"g-overlay-group-offset","g-overlay-group-apart-offset"}) {
            test::MasterTreeFixture offset;QVERIFY2(offset.load(phase),qPrintable(offset.error));
            const auto& world=offset.session.world();
            const auto ap=model::parseJson(fieldsJson(world.appearances.at(world.hudAppid.begin()->second)));
            QCOMPARE(ap["pixelX"].i(),int64_t(0));QCOMPARE(ap["pixelY"].i(),int64_t(0));
            for(const auto& s:offset.sprites) if(s.kind==MapSprite::Master) QVERIFY(s.planeEffects->supported);
        }
    }
    void masterCrossRootEscapeRefused_data() {
        QTest::addColumn<QString>("phase");
        QTest::newRow("unmastered-escape") << QString("g-overlay-group-leafplane");
        QTest::newRow("nested-master-replaced") << QString("g-overlay-nested-group-leafplane-tint");
    }
    void masterCrossRootEscapeRefused() {
        QFETCH(QString,phase);
        test::MasterTreeFixture f;QVERIFY2(f.load(phase),qPrintable(f.error));
        auto& w=f.session.worldMut();const auto rootRef=w.hudAppid.begin()->first;
        const auto rootId=w.hudAppid.begin()->second;
        const auto old=f.sprites;
        auto pick=[&](const MapSprites& sprites) {
            return mapPointerAt(f.clickPoint(2),f.frame.source.size(),{7,7},f.frame.source,f.frame.geometry,"map",true,sprites);
        };
        QVERIFY(f.checkClick(*pick(old),2).isEmpty());
        constexpr uint32_t other=90000;
        w.appearances[other]=w.appearances.at(rootId);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(w.appearances.at(rootId)))).object();
        fields["plane"]=2;fields["overlays"]=QJsonArray{};
        setFields(w.appearances[other], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        w.hudOrder.push_back(other);w.hudAppid[other]=other;w.hud[other]={};
        w.hudIdentity[other]=w.hudIdentity.at(rootRef);f.refresh();
        int refusedMasters=0;
        for(const auto& s:f.sprites) if(s.atom==rootRef && (s.kind==MapSprite::Screen || s.kind==MapSprite::Master)) {
            QCOMPARE(s.kind,MapSprite::Master);QVERIFY(!s.planeEffects->supported);++refusedMasters;
        }
        QVERIFY(refusedMasters>0);
        QVERIFY(!pick(f.sprites)->screen);QVERIFY(f.checkClick(*pick(old),2).isEmpty());
        w.hudOrder.pop_back();w.hudAppid.erase(other);w.hud.erase(other);w.hudIdentity.erase(other);
        f.refresh();QVERIFY(f.checkClick(*pick(f.sprites),2).isEmpty());
    }
    void stockLocalRelayCallbacks() {
        Fixture f; QVERIFY(f.load("local-relay-world"));
        QCOMPARE(f.frame.source.size(),QSize(224,224));
        const auto expectations = rows(QString(FUGUE_TEST_DATA_DIR)+"/local-relay-world/stock-mouse.tsv");
        QCOMPARE(expectations.size(),24);
        int menus = 0;
        for (const auto& row : expectations) {
            QVERIFY(row.size() == 7);
            const auto hit = f.at(row[1].toInt(),row[2].toInt()); QVERIFY(hit);
            QCOMPARE(QString::fromStdString(f.name(*hit)),QString::fromUtf8(row[3]));
            auto params = QString::fromStdString(proto::enc::hoverParams(hit->click));
            QCOMPARE(params,QString::fromUtf8(row[5]));
            QVERIFY2(mapPointerCurrent(*hit,f.session.world(),f.frame.geometry.session),row[0].constData());
            if (row[6] != "-") {
                QStringList names;
                for (const auto& entry : mapPointerStack(*hit)) names.push_back(QString::fromStdString(f.name(entry)));
                QCOMPARE(names.join('|'),QString::fromUtf8(row[6])); ++menus;
            }
        }
        QCOMPARE(menus,10);
        // Fresh 516.1688 stock follow-up: a world source owns both location
        // and vis coordinates (not the consumer turf at y=10).
        const auto worldHit=*f.at(104,183);
        QCOMPARE(QString::fromStdString(f.name(worldHit)),QString("m_src"));
        QVERIFY(worldHit.click.hasVis);QCOMPARE(worldHit.click.visX,9);QCOMPARE(worldHit.click.visY,-8);
        QCOMPARE(int(worldHit.click.loc.x),7);QCOMPARE(int(worldHit.click.loc.y),10);
        QVERIFY(mapPointerCurrent(worldHit,f.session.world(),0));
        // A queued source hit cannot outlive either the image occurrence or HUD membership.
        const auto hit = *f.at(8,151); QVERIFY(hit.relayHit); QVERIFY(hit.screen);
        const auto id = hit.relayHit->consumer.imageId;
        auto& w = f.session.worldMut();
        const auto image = w.imageInstances.at(id);
        const auto consumerFields = fieldsJson(w.appearances[image.appid]);
        setFields(w.appearances[image.appid], R"({"renderSource":"*rebound"})");
        QVERIFY(!mapPointerCurrent(hit,w,0)); setFields(w.appearances[image.appid], consumerFields);
        const auto worldSource = worldHit.click.over.ref;
        const auto sourceAtom = w.atoms.at(worldSource);
        w.atoms.erase(worldSource); QVERIFY(!mapPointerCurrent(worldHit,w,0));
        w.atoms[worldSource] = sourceAtom; ++w.atoms[worldSource].renderIdentity;
        QVERIFY(!mapPointerCurrent(worldHit,w,0)); w.atoms[worldSource] = sourceAtom;
        w.imageInstances.erase(id); QVERIFY(!mapPointerCurrent(hit,w,0));
        w.imageInstances[id] = image; QVERIFY(mapPointerCurrent(hit,w,0));
        ++w.imageInstances[id].identity; QVERIFY(!mapPointerCurrent(hit,w,0));
        w.imageInstances[id] = image;
        const auto producer = hit.click.over.ref;
        ++w.hudIdentity[producer].membership; QVERIFY(!mapPointerCurrent(hit,w,0));
        --w.hudIdentity[producer].membership;
        const auto appid = w.hudAppid.at(producer);
        const auto fields = fieldsJson(w.appearances[appid]);
        setFields(w.appearances[appid], R"({"renderTarget":"*rebound"})");
        QVERIFY(!mapPointerCurrent(hit,w,0)); setFields(w.appearances[appid], fields);
        QVERIFY(mapPointerCurrent(hit,w,0));
        w.bounds.maxX += 1; QVERIFY(!mapPointerCurrent(hit,w,0));
        // Saved last-rendered geometry is immutable even after model mutation.
        QCOMPARE(f.at(8,151)->click.over.ref,producer);
    }
    void localCaptureBoundsAndDependencies() {
        MapSprite source;source.image=QImage(2,2,QImage::Format_RGBA8888);source.image.fill(Qt::red);
        source.renderTarget="*root";source.localGroup=1;source.position={500,700};source.captureOrigin=source.position;
        QVERIFY(LocalTargets({source}).groups[0].valid);
        auto oversized=source;oversized.size={4096,2048};
        QVERIFY(!LocalTargets({oversized}).groups[0].valid); // 4M local-pixel bound, no allocation
        oversized.size={8193,1};QVERIFY(!LocalTargets({oversized}).groups[0].valid);
        oversized=source;oversized.captureOrigin={-8192,-8192};QVERIFY(!LocalTargets({oversized}).groups[0].valid);
        auto duplicate=source;duplicate.localGroup=2;QCOMPARE(LocalTargets({source,duplicate}).find("*root"),0); // stock relays one of them (stock-parity-world shared-render-target)
        auto alpha=source;alpha.tint[3]=.5f;QVERIFY(LocalTargets({alpha}).groups[0].valid); // captured with its alpha (overlay-relays)
        auto child=source;child.localGroup=0;QVERIFY(!LocalTargets({child}).groups[0].valid); // no invented child origin
        MapSprites chain;
        for(int i=0;i<18;++i) {
            auto s=source;s.localGroup=uint64_t(i+1);s.renderTarget=QString("*%1").arg(i);
            if(i<17) {s.image={};s.renderSource=QString("*%1").arg(i+1);}
            chain.push_back(s);
        }
        QVERIFY(!LocalTargets(chain).groups[0].valid); // bounded recursive geometry
        chain={source};auto cycle=source;cycle.renderSource="*root";
        QVERIFY(!LocalTargets({cycle}).groups[0].valid);
    }
    void stockGroupedLocalRelayInput() {
        Fixture f;QVERIFY(f.load("local-relay-world","frames-group.txt"));
        for(const QPoint p : {QPoint(120,159),QPoint(56,159)}) {
            const auto hit=*f.at(p.x(),p.y());
            QCOMPARE(QString::fromStdString(f.name(hit)),QString("obscured"));
            QCOMPARE(hit.click.iconX,-7);QCOMPARE(hit.click.iconY,32);
            QVERIFY(hit.click.hasVis);QCOMPARE(hit.click.visX,p.x()+1);QCOMPARE(hit.click.visY,160);
            QVERIFY(mapPointerCurrent(hit,f.session.world(),0));
        }
        const auto red=*f.at(136,151);
        QCOMPARE(QString::fromStdString(f.name(red)),QString("obscured"));
        QCOMPARE(red.click.iconX,9);QCOMPARE(red.click.iconY,24);
        QCOMPARE(red.click.visX,137);QCOMPARE(red.click.visY,152);
        const auto& im=f.session.world().imageInstances.at(78);
        QCOMPARE(im.turfCell,(std::array<uint16_t,3>{10,9,1}));
        QCOMPARE(im.turfWidth,uint16_t(15));QCOMPARE(im.turfHeight,uint16_t(15));
        QVERIFY(!f.session.world().imageTurfCurrent(78,im.identity,{10,9,0}));
    }
    void stockShadowDecoding() {
        for(int identity:{1,2,17,255}) {
            const auto text=QString(R"({"filters":[{"type":3,"flags":%1,"paramMask":31,"params":"0000804004080faa00000000000000c000000000"}]})").arg(identity);
            const auto e=decodePlaneEffects(model::appearanceOf(text.toStdString()));
            QVERIFY(e.supported);QCOMPARE(e.ignoredFilters,size_t(0));QCOMPARE(e.filters.size(),size_t(1));
            const auto& f=e.filters[0];QCOMPARE(f.kind,PlaneFilter::Shadow);
            QCOMPARE(f.size,4.0);QCOMPARE(f.y,2.0);QCOMPARE(f.color[3],170.f/255);
            QVERIFY(e.boundedTreeFilters());QVERIFY(e.relayFilters());QVERIFY(e.sourceCoverage({224,224}));
            for(double size:{0.0,.0001,.5,4.0,6.25,32.0,100.0,1024.0})
                for(double offset:{-8192.0,-1.0,0.0,.5,4.25,33.0,8192.0}) {
                    auto wide=e;wide.filters[0].size=size;wide.filters[0].grow=offset;
                    wide.filters[0].x=-32.5;wide.filters[0].y=.125;
                    QVERIFY(wide.valid({224,224}));
                }
        }
        PlaneEffects bounds;PlaneFilter shadow;shadow.kind=PlaneFilter::Shadow;
        bounds.filters={shadow};
        for(double invalid:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            auto e=bounds;e.filters[0].size=invalid;QVERIFY(!e.valid({224,224}));
        }
        for(double invalid:{8192.01,-8192.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            auto e=bounds;e.filters[0].x=invalid;QVERIFY(!e.valid({224,224}));
            e=bounds;e.filters[0].grow=invalid;QVERIFY(!e.valid({224,224}));
        }
        bounds.filters.resize(8,shadow);QVERIFY(bounds.valid({224,224}));
        bounds.filters.push_back(shadow);QVERIFY(!bounds.valid({224,224}));
        auto empty=decodeSurfaceEffects(model::appearanceOf(R"({"filters":[{"type":3,"flags":1,"params":""}]})"));
        QCOMPARE(empty.filters.size(),size_t(1));QCOMPARE(empty.filters[0].size,0.0);
        QCOMPARE(empty.filters[0].x,0.0);QCOMPARE(empty.filters[0].color[3],0.f);
        for(const auto* record:{R"({"type":3,"flags":1,"paramMask":1,"params":"000080bf"})",
            R"({"type":3,"flags":1,"paramMask":1,"params":"0000807f"})",
            R"({"type":3,"flags":1,"paramMask":32,"params":"0000"})",
            R"({"type":3,"flags":1,"paramMask":31,"params":"00"})"}) {
            const auto e=decodePlaneEffects(model::appearanceOf(std::string("{\"filters\":[")+record+"]}"));
            QVERIFY(e.supported);QVERIFY(e.filters.empty());QCOMPARE(e.ignoredFilters,size_t(1));
        }
    }
    void orderedPlaneFilterDecoding() {
        const auto decoded=decodePlaneEffects(model::appearanceOf(R"({"filters":[
            {"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000403f"}},
            {"type":2,"flags":1,"paramMask":35,"params":{"bstr":"0000a03f804020800200"}},
            {"type":3,"flags":1,"paramMask":31,"params":{"bstr":"0000003f102030ff000000c0000040400000803f"}}
        ]})"));
        QVERIFY(decoded.supported); QCOMPARE(decoded.filters.size(),size_t(3));
        QCOMPARE(decoded.filters[0].kind,PlaneFilter::Blur); QCOMPARE(decoded.filters[0].size,.75);
        QCOMPARE(decoded.filters[1].kind,PlaneFilter::Outline); QCOMPARE(decoded.filters[1].size,1.25);
        QCOMPARE(decoded.filters[1].flags,2); QCOMPARE(decoded.filters[1].color[3],128.f/255);
        QCOMPARE(decoded.filters[2].kind,PlaneFilter::Shadow); QCOMPARE(decoded.filters[2].x,-2);
        QCOMPARE(decoded.filters[2].y,-3); QCOMPARE(decoded.filters[2].grow,1.0);
        for(const auto* record:{
            R"({"type":1,"flags":1})", R"({"type":4,"flags":1})", R"({"type":2,"flags":1})",
            R"({"type":1,"flags":1,"paramMask":2,"params":{"bstr":"00000000"}})",
            R"({"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000807f"}})",
            R"({"type":1,"flags":1,"paramMask":1,"params":{"bstr":"000000"}})",
            R"({"type":2,"flags":1,"paramMask":32,"params":{"bstr":"0400"}})",
            R"({"type":3,"flags":1,"paramMask":36,"params":{"bstr":"0000003f0000"}})"}) {
            const auto fallback=decodePlaneEffects(model::appearanceOf(std::string("{\"filters\":[")+record+"]}"));
            QVERIFY2(fallback.supported,record);QVERIFY(fallback.filters.empty());QCOMPARE(fallback.ignoredFilters,size_t(1));
        }
        QVERIFY(decodePlaneEffects(model::appearanceOf(R"({"filters":[{"type":1,"flags":1,"paramMask":1,"params":{"bstr":"0000a040"}}]})")).supported);
        auto wideChain=decoded;wideChain.filters[0].size=5;QVERIFY(!wideChain.valid({32,32}));
        auto sharpChain=decoded; sharpChain.filters[1].flags=1; QVERIFY(sharpChain.valid({32,32}));
        auto tooMany=decoded; tooMany.filters.resize(9); QVERIFY(!tooMany.valid({32,32}));
        auto invalid=decoded; invalid.matrix=std::array<double,6>{0,0,0,0,0,0}; QVERIFY(!invalid.valid({32,32}));
    }
    void planeTransformPickingAndSelection() {
        MapGeometry geometry; geometry.region={0,0,3,3}; geometry.world={3,3}; geometry.tile=32;
        MapSprite object; object.atom=42; object.key=7; object.haveKey=true; object.targetIdentity=9;
        object.position={10,12}; object.iconBottom={10,20}; object.cell={1,3}; object.plane=1;
        object.image=QImage(8,8,QImage::Format_RGBA8888); object.image.fill(Qt::red);
        object.image.setPixelColor(1,1,Qt::transparent);
        MapSprite master; master.kind=MapSprite::Master; master.plane=1;
        auto effects=std::make_shared<PlaneEffects>(); effects->matrix=std::array<double,6>{1,0,20,0,1,-10};
        master.planeEffects=effects;
        auto at=[&](QPoint pixel,const MapSprites& sprites) {
            return *mapPointerAt(pixel,{96,96},{3,3},{0,0,96,96},geometry,"map",true,sprites);
        };
        const MapSprites rendered{object,master};
        auto hit=at({32,24},rendered); QCOMPARE(hit.click.over.ref,42u); QCOMPARE(hit.click.iconX,3); QCOMPARE(hit.click.iconY,6);
        QCOMPARE(mapPointerStack(hit).size(),1);
        QCOMPARE(at({31,23},rendered).click.over.kind,proto::enc::RefTurf); // source alpha hole
        QCOMPARE(at({12,14},rendered).click.over.kind,proto::enc::RefTurf); // no untransformed phantom
        model::World w; w.bounds={true,3,3}; w.atoms[42].hasPos=true; w.atoms[42].haveKey=true;
        w.atoms[42].key=7; w.atoms[42].renderIdentity=9;
        QVERIFY(mapPointerCurrent(hit,w,0)); w.atoms[42].renderIdentity=10; QVERIFY(!mapPointerCurrent(hit,w,0));
        auto plain=master; plain.planeEffects.reset();
        QCOMPARE(at({12,14},{object,master,plain}).click.over.ref,42u); // last master wins
        QCOMPARE(at({32,24},{object,master,plain}).click.over.kind,proto::enc::RefTurf);
        plain.mouseOpacity=0; QCOMPARE(at({12,14},{object,plain}).click.over.kind,proto::enc::RefTurf);
        auto filtered=std::make_shared<PlaneEffects>(); filtered->filters.push_back({});
        plain.mouseOpacity=1; plain.planeEffects=filtered;
        QVERIFY(mapPointerStack(at({12,14},{object,plain})).empty()); // no unproved filtered hit mask
        filtered->supported=false;
        QVERIFY(mapPointerStack(at({12,14},{object,plain})).empty());
        auto rotated=std::make_shared<PlaneEffects>();rotated->matrix=std::array<double,6>{0,-1,0,1,0,0};
        plain.planeEffects=rotated;
        auto offsetObject=object;offsetObject.position+=QPoint(32,64);offsetObject.iconBottom+=QPoint(32,64);
        const auto rotation=mapPointerAt({14,83},{96,96},{3,3},{32,64,96,96},geometry,"map",true,{offsetObject,plain});
        QVERIFY(rotation);QCOMPARE(rotation->click.over.ref,42u);QCOMPARE(rotation->click.iconX,4);QCOMPARE(rotation->click.iconY,6);
        QCOMPARE(mapPointerStack(*rotation).size(),1);
        auto attached=object;attached.kind=MapSprite::Image;attached.screenInteractive=true;attached.imageIdentity=5;
        // A client image on a turned plane is listed where the plane draws it (stock 516.1689, plane-click-world turn90).
        QCOMPARE(mapPointerStack(at({32,24},{attached,master})).size(),1);
        // Pending selection changes cannot alter the retained rendered transform.
        QCOMPARE(at({32,24},rendered).click.over.ref,42u);
        QCOMPARE(mapPointerStack(hit).size(),1);
    }
    void stockPixels_data() {
        QTest::addColumn<QString>("scene"); QTest::addColumn<int>("count");
        QTest::newRow("bands") << "bands" << 192;
        QTest::newRow("pairs") << "pairs" << 192;
        QTest::newRow("background") << "background" << 192;
        QTest::newRow("cells") << "cells" << 6;
        QTest::newRow("cellsame") << "cellsame" << 12;
    }
    void stockPixels() {
        QFETCH(QString,scene); QFETCH(int,count);
        Fixture f; QVERIFY(f.load("bandorder-world/"+scene));
        const auto image=f.raster(); QCOMPARE(image.size(),QSize(480,480));
        const auto expected=rows(QString(FUGUE_TEST_DATA_DIR)+"/bandorder-world/"+scene+"/dreamseeker-pixels.tsv");
        QCOMPARE(expected.size(),count);
        for(const auto& r:expected) {
            QCOMPARE(r.size(),4);
            const auto got=image.pixelColor(r[1].toInt(),479-r[2].toInt()).name().mid(1).toUtf8();
            QVERIFY2(got==r[3],(r[0]+": expected "+r[3]+" got "+got).constData());
        }
    }
    void stockMenus_data() {
        QTest::addColumn<QString>("scene"); QTest::addColumn<int>("count");
        QTest::newRow("overlays") << "bandorder-world/overlays" << 9;
        QTest::newRow("thresholds") << "bandorder-world/thresholds" << 28;
        QTest::newRow("thresholds2") << "bandorder-world/thresholds2" << 28;
        QTest::newRow("hudstack") << "hudstack-world" << 71;
    }
    void stockMenus() {
        QFETCH(QString,scene); QFETCH(int,count); Fixture f; QVERIFY(f.load(scene));
        const auto expected=rows(QString(FUGUE_TEST_DATA_DIR)+'/'+scene+"/dreamseeker-menus.tsv");
        QCOMPARE(expected.size(),count);
        int gradedVerbs=0;
        for(const auto& r:expected) {
            QVERIFY(r.size()>=4); const auto p=f.at(r[1].toInt(),r[2].toInt()); QVERIFY(p);
            QByteArray got;
            int index = 0;
            const auto verbs = r.size()==5 ? r[4].split('|') : QList<QByteArray>();
            for(const auto& hit:mapPointerStack(*p)) {
                const auto name=QByteArray::fromStdString(f.name(hit)); if(name.isEmpty()) continue;
                if(!got.isEmpty()) got+='|'; got+=name;
                QVERIFY(mapPointerCurrent(hit,f.session.world(),0));
                if(index < verbs.size() && verbs[index]!="-") {
                    ++gradedVerbs;
                    const auto& ref=hit.click.over; const auto& loc=hit.click.loc;
                    const auto target=model::targetFromHit(f.session.world(),ref.kind,ref.ref,hit.screen ? 0 : int(loc.x)+1,hit.screen ? 0 : int(loc.y)+1,hit.screen);
                    QByteArray names;
                    for(const auto& v:model::atomMenu(f.session.world(),target.appid,target.target)) {
                        if(!names.isEmpty()) names+=','; names+=QByteArray::fromStdString(v.verb.name);
                    }
                    QCOMPARE(names,verbs[index]);
                }
                ++index;
            }
            QVERIFY2(got==r[3],(r[0]+": expected "+r[3]+" got "+got).constData());
        }
        QCOMPARE(gradedVerbs,scene=="hudstack-world" ? 9 : 0);
    }
    void stockHudHits_data() {
        QTest::addColumn<QString>("scene"); QTest::addColumn<QString>("frames");
        for(const auto& scene:{"hudhits-world","hudoverlay-world"}) for(const auto& frames:{"frames-pm.txt","frames-nopm.txt"})
            QTest::newRow((QByteArray(scene)+frames).constData()) << QString(scene) << QString(frames);
    }
    void stockHudHits() {
        QFETCH(QString,scene); QFETCH(QString,frames); Fixture f; QVERIFY(f.load(scene,frames));
        const auto expected=rows(QString(FUGUE_TEST_DATA_DIR)+'/'+scene+"/dreamseeker-answers.tsv");
        QCOMPARE(expected.size(),14);
        for(const auto& r:expected) {
            QCOMPARE(r.size(),7); const auto p=f.at(r[1].toInt(),r[2].toInt()); QVERIFY(p);
            const auto& c=p->click;
            const auto name=c.over.kind==proto::enc::RefTurf ? QByteArray("turf ")+QByteArray::number(c.over.x+1)+','+QByteArray::number(c.over.y+1) : QByteArray::fromStdString(f.name(*p));
            QVERIFY2(name==r[3],(r[0]+": expected "+r[3]+" got "+name).constData());
            QCOMPARE(c.iconX,r[4].toInt()); QCOMPARE(c.iconY,r[5].toInt());
            const auto params=QByteArray::fromStdString(proto::enc::clickParams(c));
            QCOMPARE(params.mid(params.indexOf("screen-loc=")+11),r[6]);
        }
    }
    void thresholdControl() {
        // Stock background rows cross a float32 half-ULP; no double-only shortcut.
        QVERIFY(drawKey(20000,false,32*6+1+33).band<0);
        QCOMPARE(drawKey(20000,false,32*7+1+33).band,16);
        QCOMPARE(drawKey(5000,false,66).band,0);
        QCOMPARE(drawKey(5000,true,-1).band,17);
    }
};
FUGUE_TEST_GUILESS_MAIN(Composition)
#include "composition_test.moc"
