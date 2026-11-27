// Scalar, straight-RGBA reference for bounded plane filters. Test-only pixels;
// production composition always uses QRhi surfaces.
#pragma once
#include "shell/planeeffects.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <vector>
namespace fugue::shell::test {
inline int byte(double v) { return std::clamp(int(std::round(v*255)),0,255); }
inline QImage blurReference(const QImage& src, double sigma) {
    if (sigma <= 0) return src;
    const int radius = int(std::ceil(3*sigma));
    std::vector<double> kernel; double total = 0;
    for (int i=-radius;i<=radius;++i) { const double v=std::exp(-double(i*i)/(2*sigma*sigma)); kernel.push_back(v); total+=v; }
    for (auto& v:kernel) v/=total;
    auto axis=[&](const QImage& image,bool horizontal) {
        QImage out(image.size(),QImage::Format_RGBA8888);
        for (int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            std::array<double,4> sum{};
            for(int i=-radius;i<=radius;++i) {
                const auto c=image.pixelColor(std::clamp(x+(horizontal?i:0),0,image.width()-1),
                    std::clamp(y+(horizontal?0:i),0,image.height()-1));
                const double a=c.alpha()/255.0, weight=kernel[size_t(i+radius)];
                sum[0]+=c.red()/255.0*a*weight; sum[1]+=c.green()/255.0*a*weight;
                sum[2]+=c.blue()/255.0*a*weight; sum[3]+=a*weight;
            }
            out.setPixelColor(x,y,sum[3]>0 ? QColor(byte(sum[0]/sum[3]),byte(sum[1]/sum[3]),byte(sum[2]/sum[3]),byte(sum[3])) : QColor(0,0,0,0));
        }
        return out;
    };
    return axis(axis(src,true),false);
}
inline QImage outlineReference(const QImage& src,const PlaneFilter& f) {
    const int w=src.width(),h=src.height();
    std::vector<double> alpha(size_t(w*h));
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) alpha[size_t(y*w+x)]=src.pixelColor(x,y).alpha()/255.0;
    std::vector<double> steps;
    if(f.size<=1) steps.push_back(f.size);
    else {
        double left=f.size,step=1;
        for(;;) {
            if(2*step>=left && step>2) step=left*.5;
            steps.push_back(step);left-=step;step*=1.75;
            if(left<=step) {steps.push_back(left);break;}
        }
    }
    for(double step:steps) {
        if(step<=0) continue;
        auto at=[&](int x,int y){return x<0 || y<0 || x>=w || y>=h ? 0.0 : alpha[size_t(y*w+x)];};
        auto tap=[&](double x,double y) {
            const int ix=int(std::floor(x)),iy=int(std::floor(y)); const double dx=x-ix,dy=y-iy;
            return (1-dx)*(1-dy)*at(ix,iy)+dx*(1-dy)*at(ix+1,iy)+(1-dx)*dy*at(ix,iy+1)+dx*dy*at(ix+1,iy+1);
        };
        auto next=alpha; const double d=(f.flags&2)?step:step*.70710677;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const double c=at(x,y);
            double a=c+tap(x-step,y)+tap(x+step,y)+tap(x,y-step)+tap(x,y+step)+
                tap(x-d,y-d)+tap(x+d,y-d)+tap(x-d,y+d)+tap(x+d,y+d);
            a=(f.flags&1)?(a>=.75?1:0):std::min(1.0,a);
            next[size_t(y*w+x)]=c+(1-c)*f.color[3]*a;
        }
        alpha=std::move(next);
    }
    auto out=src;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const auto c=src.pixelColor(x,y); const double sa=c.alpha()/255.0,a=alpha[size_t(y*w+x)],ring=a-sa;
        if(ring<=0) continue;
        out.setPixelColor(x,y,QColor(byte((c.red()/255.0*sa+f.color[0]*ring)/a),
            byte((c.green()/255.0*sa+f.color[1]*ring)/a),byte((c.blue()/255.0*sa+f.color[2]*ring)/a),byte(a)));
    }
    return out;
}
inline QImage transformReference(const QImage& src, const std::array<double,6>& matrix) {
    QImage out(src.size(),QImage::Format_RGBA8888); out.fill(Qt::transparent);
    const auto affine=StaticAffine::make(matrix,src.size()); if(!affine) return out;
    for(int y=0;y<src.height();++y) for(int x=0;x<src.width();++x) {
        const auto p=affine->inverse.map(QPointF(x+.5,y+.5));
        if(p.x()<=-.5 || p.y()<=-.5 || p.x()>=src.width()+.5 || p.y()>=src.height()+.5) continue;
        const double px=p.x()-.5,py=p.y()-.5; const int ix=int(std::floor(px)),iy=int(std::floor(py));
        const double dx=px-ix,dy=py-iy; std::array<double,4> sum{};
        for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
            const auto c=src.rect().contains(ix+i,iy+j) ? src.pixelColor(ix+i,iy+j) : QColor(0,0,0,0);
            const double weight=(i?dx:1-dx)*(j?dy:1-dy),a=c.alpha()/255.0;
            sum[0]+=c.red()/255.0*a*weight;sum[1]+=c.green()/255.0*a*weight;sum[2]+=c.blue()/255.0*a*weight;sum[3]+=a*weight;
        }
        if(sum[3]>0) out.setPixelColor(x,y,QColor(byte(sum[0]/sum[3]),byte(sum[1]/sum[3]),byte(sum[2]/sum[3]),byte(sum[3])));
    }
    return out;
}
inline QColor overReference(QColor s,QColor d) {
    const double sa=s.alpha()/255.0,da=d.alpha()/255.0,a=sa+da*(1-sa);
    if(a<=0) return {0,0,0,0};
    return {byte((s.red()/255.0*sa+d.red()/255.0*da*(1-sa))/a),
        byte((s.green()/255.0*sa+d.green()/255.0*da*(1-sa))/a),
        byte((s.blue()/255.0*sa+d.blue()/255.0*da*(1-sa))/a),byte(a)};
}
inline QImage filterReference(const QImage& src,const PlaneFilter& f) {
    if(f.kind==PlaneFilter::Blur) return blurReference(src,f.size);
    if(f.kind==PlaneFilter::Outline) return outlineReference(src,f);
    auto silhouette=src;
    for(int y=0;y<src.height();++y) for(int x=0;x<src.width();++x) {
        int alpha=0; const int r=int(std::ceil(f.grow));
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx)
            if(dx*dx+dy*dy<=f.grow*f.grow && src.rect().contains(x+dx,y+dy)) alpha=std::max(alpha,src.pixelColor(x+dx,y+dy).alpha());
        silhouette.setPixelColor(x,y,QColor(byte(f.color[0]),byte(f.color[1]),byte(f.color[2]),byte(alpha/255.0*f.color[3])));
    }
    silhouette=blurReference(silhouette,f.size);
    auto out=src;
    for(int y=0;y<src.height();++y) for(int x=0;x<src.width();++x)
        out.setPixelColor(x,y,overReference(src.pixelColor(x,y),silhouette.rect().contains(x-int(f.x),y-int(f.y))?silhouette.pixelColor(x-int(f.x),y-int(f.y)):QColor(0,0,0,0)));
    return out;
}
}
