#include "shell/mapvisibility.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace fugue::shell {
QRect MapVisibility::displayedCells(QRect region, int tile, QRect source) {
    if (region.isEmpty() || source.isEmpty() || tile <= 0) return {};
    auto floor=[tile](int v) { return int(std::floor(double(v) / tile)); };
    const int left=region.x()+floor(source.left());
    const int right=region.x()+floor(source.right());
    const int bottom=region.y()+region.height()-1-floor(source.top());
    const int top=region.y()+region.height()-1-floor(source.bottom());
    if (left>right || top>bottom) return {};
    return QRect(QPoint(left,top),QPoint(right,bottom));
}
const char* visibilityName(VisibilityOutcome v) {
    switch (v) {
#define V(x) case VisibilityOutcome::x: return #x
        V(Visible); V(Sight); V(Hidden); V(Unknown); V(Unsupported); V(Budget); V(OffView);
#undef V
    }
    return "Unknown";
}
VisibilityOutcome VisibilityFrame::cell(int x, int y) const {
    if (status != VisibilityOutcome::Visible) return status;
    if (!cells.contains(x,y)) return VisibilityOutcome::OffView;
    return VisibilityOutcome(outcomes[size_t(y-cells.y()) * cells.width() + x-cells.x()]);
}
VisibilityOutcome VisibilityFrame::illumination(QPoint anchor) const {
    if (status!=VisibilityOutcome::Visible) return status;
    if (!lightCells.contains(anchor)) return VisibilityOutcome::Unknown;
    return VisibilityOutcome(lighting[size_t(anchor.y()-lightCells.y())*lightCells.width()+anchor.x()-lightCells.x()]);
}
VisibilityOutcome VisibilityFrame::test(QRect bounds, Category category, QPoint anchor) const {
    if (status != VisibilityOutcome::Visible) return status;
    if (hasBlackness() && !blacknessReady) return VisibilityOutcome::Unsupported;
    // Integer floor division, including negative offsets at the world boundary.
    auto floor = [&](int v) { return int(std::floor(double(v) / tile)); };
    if (bounds.isEmpty()) return VisibilityOutcome::OffView;
    QRect covered(QPoint(floor(bounds.left()), floor(-bounds.bottom()-1)),
                  QPoint(floor(bounds.right()), floor(-bounds.top()-1)));
    covered = covered.intersected(cells);
    if (covered.isEmpty()) return VisibilityOutcome::OffView;
    if (sight & uint32_t(category)) return VisibilityOutcome::Sight;
    const auto light=illumination(anchor);
    if (light!=VisibilityOutcome::Visible) return light;
    const int stride=cells.width()+1;
    const int x0=covered.left()-cells.left(),y0=covered.top()-cells.top();
    const int x1=x0+covered.width(),y1=y0+covered.height();
    auto count=[&](size_t k) { const auto& s=sums[k];
        return s[size_t(y1)*stride+x1]+s[size_t(y0)*stride+x0]-s[size_t(y0)*stride+x1]-s[size_t(y1)*stride+x0]; };
    if (count(0)) return VisibilityOutcome::Visible;
    if (count(2)) return VisibilityOutcome::Budget;
    return count(1) ? VisibilityOutcome::Unknown : VisibilityOutcome::Hidden;
}
namespace {
// The model's cell tables are ordered by (x,y,z), while the bounded sampler
// visits (y,x). Keep one forward cursor per column for this preparation only.
// Sparse columns / many other Z levels fall back to a tree seek after four
// steps: neither the cumulative world cache nor an unrelated level is scanned.
// No model iterators survive prepare(), so arrivals, erasure and relinking need
// no revision protocol and cannot leave dangling cached references.
template<class Table> class ColumnLookup {
public:
    ColumnLookup(const Table& table, int width) : table_(table), columns_(size_t(width)) {}
    const typename Table::mapped_type* find(int column, const typename Table::key_type& key) {
        auto& cursor=columns_[size_t(column)];
        if (!cursor.ready) { cursor.it=table_.lower_bound(key); cursor.ready=true; }
        else {
            int steps=0;
            while (cursor.it!=table_.end() && cursor.it->first<key) {
                if (steps++==4) { cursor.it=table_.lower_bound(key); break; }
                ++cursor.it;
            }
        }
        return cursor.it!=table_.end() && cursor.it->first==key ? &cursor.it->second : nullptr;
    }
private:
    struct Cursor { typename Table::const_iterator it; bool ready=false; };
    const Table& table_;
    std::vector<Cursor> columns_;
};
// Two depth-constrained eight-neighbor passes, followed by simultaneous opaque
// boundary completion. Verified against stock fixtures, not generic raycasting.
std::vector<int> propagate(const VisibilityFrame& f, bool missingOpaque) {
    const int width=f.cells.width(), height=f.cells.height(), count=width*height;
    std::vector<int> square(size_t(count),0), diamond(size_t(count),0), order(size_t(count),0);
    std::iota(order.begin(),order.end(),0);
    auto xy=[&](int i) { return QPoint(i%width+f.cells.x(),i/width+f.cells.y()); };
    auto depth=[&](int i, bool manhattan) {
        const auto p=xy(i)-f.eye;
        return manhattan ? std::abs(p.x())+std::abs(p.y()) : std::max(std::abs(p.x()),std::abs(p.y()));
    };
    auto opaque=[&](int i) { return f.opacity[size_t(i)]==VisibilityFrame::Opaque ||
        (missingOpaque && f.opacity[size_t(i)]>=VisibilityFrame::Missing); };
    auto index=[&](int x,int y) { return x>=0 && y>=0 && x<width && y<height ? y*width+x : -1; };
    for (bool manhattan : {false,true}) {
        std::stable_sort(order.begin(),order.end(),[&](int a,int b) { return depth(a,manhattan)<depth(b,manhattan); });
        auto& values=manhattan ? diamond : square;
        for (int i : order) {
            const int d=depth(i,manhattan);
            if (!d) continue;
            bool reached=false;
            for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
                if (!dx && !dy) continue;
                const int n=index(i%width+dx,i/width+dy);
                if (n>=0 && values[size_t(n)]==d-1) reached=true;
            }
            if (reached && (opaque(i) || !manhattan || square[size_t(i)])) values[size_t(i)]=opaque(i) ? -1 : d;
        }
    }
    const int eye=index(f.eye.x()-f.cells.x(),f.eye.y()-f.cells.y());
    if (eye>=0) diamond[size_t(eye)]=1;
    std::vector<int> boundaries;
    for (int i=0; i<count; ++i) {
        if (!opaque(i) || diamond[size_t(i)]) continue;
        const int x=i%width,y=i/width;
        auto lit=[&](int dx,int dy) { const int n=index(x+dx,y+dy); return n>=0 && diamond[size_t(n)]!=0; };
        auto wall=[&](int dx,int dy) { const int n=index(x+dx,y+dy); return n>=0 && opaque(n); };
        bool visible=(lit(-1,0)&&lit(1,0)) || (lit(0,-1)&&lit(0,1));
        for (int dx : {-1,1}) for (int dy : {-1,1})
            visible |= lit(dx,dy) && !wall(dx,dy) && lit(dx,0) && wall(dx,0) && lit(0,dy) && wall(0,dy);
        if (visible) boundaries.push_back(i);
    }
    for (int i : boundaries) diamond[size_t(i)]=-1;
    return diamond;
}
// Max-energy eight-neighbor flood restricted to the eye's geometric field.
// O(cells + edges), independent of emitter count. Walls receive light but stop it;
// hidden source/back-face paths cannot light a wall visible from the other side.
std::vector<uint8_t> illuminate(const VisibilityFrame& f, const std::vector<uint8_t>& seeds,
                                const std::vector<int>& visible, bool missingOpaque, bool through) {
    auto light=seeds;
    std::array<std::vector<int>,256> buckets;
    for (size_t i=0;i<light.size();++i) if (light[i]) buckets[light[i]].push_back(int(i));
    const int width=f.cells.width(),height=f.cells.height();
    for (int level=255;level>1;--level) for (int i:buckets[size_t(level)]) {
        if (light[size_t(i)]!=level) continue;
        const auto opacity=f.opacity[size_t(i)];
        const bool blocks=!through && (opacity==VisibilityFrame::Opaque || (missingOpaque && opacity>=VisibilityFrame::Missing));
        const int spread=blocks ? seeds[size_t(i)] : level;
        if (spread<=1) continue;
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            if (!dx && !dy) continue;
            const int x=i%width+dx,y=i/width+dy;
            if (x<0 || y<0 || x>=width || y>=height) continue;
            const size_t n=size_t(y)*width+x;
            if (!visible[n] || light[n]>=spread-1) continue;
            light[n]=uint8_t(spread-1); buckets[size_t(spread-1)].push_back(int(n));
        }
    }
    return light;
}
}
std::shared_ptr<const VisibilityFrame> MapVisibility::prepare(const model::World& w, QRect displayed) {
    auto next=std::make_shared<VisibilityFrame>();
    next->eye={w.eyeX-1,w.eyeY-1}; next->z=w.viewZ; next->sight=w.sight; next->tile=model::iconSizeOf(w);
    next->seeInDark=w.seeInDark;
    const int width=w.bounds.viewW,height=w.bounds.viewH;
    next->cells={w.eyeX-1-width/2,w.eyeY-1-height/2,width,height};
    if (!displayed.isEmpty()) next->cells=next->cells.united(displayed);
    Inputs input;
    if (!w.haveEye || !w.haveSight || !w.haveSeeInDark || !w.bounds.have || !w.resync.haveOrigin ||
        width<=0 || height<=0 || w.bounds.winW<=0 || w.bounds.winH<=0 || w.bounds.lightW<=0 || w.bounds.lightH<=0 || next->tile<=0)
        next->status=VisibilityOutcome::Unknown;
    else if (next->cells.width()>VisibilityFrame::maxSide || next->cells.height()>VisibilityFrame::maxSide)
        next->status=VisibilityOutcome::Budget;
    // SEE_INFRA (0x40) and SEE_PIXELS (0x100) draw like plain sight (stock 516.1688, tests/data/sight-flags-world:
    // no infra-luminous atom showed in the dark, pixel offsets changed nothing); BLIND stays unsupported (all black).
    else if (w.sight & ~0x77cu) next->status=VisibilityOutcome::Unsupported;
    else {
        next->status=VisibilityOutcome::Visible;
        const QRect buffered(int16_t(w.resync.originX),int16_t(w.resync.originY),w.bounds.winW,w.bounds.winH);
        input.window=QRect(next->eye.x()-w.bounds.lightW/2,next->eye.y()-w.bounds.lightH/2,
                           w.bounds.lightW,w.bounds.lightH).intersected(buffered);
        // A radius-6 emitter reaches five tiles away. Only the captured source
        // window counts, including its off-screen margin, not the larger cache.
        input.cells=next->cells.united(next->cells.adjusted(-5,-5,5,5).intersected(input.window));
        const auto count=size_t(input.cells.width())*input.cells.height();
        input.opacity.reserve(count); input.ambient.reserve(count);
        input.emission.reserve(count); input.uncertain.reserve(count);
        ColumnLookup tiles(w.tiles,input.cells.width()), areas(w.areaCells,input.cells.width());
        ColumnLookup objects(w.cellObjs,input.cells.width()), mobs(w.cellMobs,input.cells.width());
        // Many roots and area cells share an appearance. Resolve its small sight
        // scalars once per attempt, including absent appearances. These caches are
        // bounded by the sampled cells/visits, and discarded before model mutation.
        struct Sample { uint8_t opacity=VisibilityFrame::Missing, emission=0, uncertain=VisibilityFrame::Missing; };
        std::unordered_map<uint32_t,Sample> samples;
        std::unordered_map<uint32_t,uint8_t> ambientAreas;
        auto appearance=[&](uint32_t id) -> const Sample& {
            auto [it,inserted]=samples.try_emplace(id);
            if (inserted) {
                const auto a=w.appearances.find(id);
                if (a!=w.appearances.end()) {
                    it->second.opacity=a->second.opaque() ? VisibilityFrame::Opaque : VisibilityFrame::Clear;
                    if (const int luminosity=a->second.luminosity(); luminosity>=0) { it->second.emission=uint8_t(luminosity); it->second.uncertain=0; }
                }
            }
            return it->second;
        };
        for (int y=input.cells.top(); y<=input.cells.bottom(); ++y) for (int x=input.cells.left(); x<=input.cells.right(); ++x) {
            uint8_t state=VisibilityFrame::Clear,ambient=0,emission=0,uncertain=0;
            const bool inWorld=x>=0 && y>=0 && x<w.bounds.maxX && y<w.bounds.maxY;
            const bool live=input.window.contains(x,y);
            auto sample=[&](uint32_t id) {
                const auto& a=appearance(id);
                if (a.opacity==VisibilityFrame::Missing) {
                    if (state!=VisibilityFrame::Opaque) state=VisibilityFrame::Missing;
                } else if (a.opacity==VisibilityFrame::Opaque) state=VisibilityFrame::Opaque;
                if (a.uncertain) uncertain=VisibilityFrame::Missing;
                else emission=std::max(emission,a.emission);
            };
            if (!inWorld) state=VisibilityFrame::Opaque;
            else if (next->visits>=maxVisits) state=ambient=uncertain=VisibilityFrame::Deferred;
            else {
                ++next->visits;
                const std::array<uint16_t,3> key{uint16_t(x),uint16_t(y),w.viewZ};
                const int column=x-input.cells.left();
                const auto* t=tiles.find(column,key);
                if (t) sample(*t);
                else state=uncertain=VisibilityFrame::Missing;
                ambient=VisibilityFrame::Missing;
                const auto* area=areas.find(column,key);
                if (area) {
                    auto [it,inserted]=ambientAreas.try_emplace(*area,VisibilityFrame::Missing);
                    if (inserted) {
                        const auto bind=w.turfBinds.find(*area);
                        if (bind!=w.turfBinds.end()) {
                            const auto& a=appearance(bind->second);
                            if (!a.uncertain) it->second=a.emission!=0;
                        }
                    }
                    ambient=it->second;
                }
                for (bool isMob : {false,true}) {
                    auto& cells=isMob ? mobs : objects;
                    const auto* found=cells.find(column,{x+1,y+1,int(w.viewZ)});
                    if (!found) continue;
                    for (uint32_t id : *found) {
                        if (next->visits>=maxVisits) {
                            if (state!=VisibilityFrame::Opaque) state=VisibilityFrame::Deferred;
                            uncertain=VisibilityFrame::Deferred; break;
                        }
                        ++next->visits;
                        const uint32_t keyId=id | (isMob ? model::kRegistryA : 0u);
                        if (w.resync.freed.count(keyId)) continue;
                        const auto a=w.atoms.find(keyId);
                        if (a!=w.atoms.end() && a->second.hasPos && a->second.x==x+1 && a->second.y==y+1 && a->second.z==w.viewZ)
                            sample(a->second.appid);
                        else {
                            if (state!=VisibilityFrame::Opaque) state=VisibilityFrame::Missing;
                            uncertain=VisibilityFrame::Missing;
                        }
                    }
                }
            }
            if (!live) emission=uncertain=0;
            next->unknownCells+=state>=VisibilityFrame::Missing || ambient>=VisibilityFrame::Missing || uncertain>=VisibilityFrame::Missing;
            next->deferredCells+=state==VisibilityFrame::Deferred || ambient==VisibilityFrame::Deferred || uncertain==VisibilityFrame::Deferred;
            input.opacity.push_back(state); input.ambient.push_back(ambient);
            input.emission.push_back(emission); input.uncertain.push_back(uncertain);
        }
    }
    reused_=frame_ && frame_->cells==next->cells && frame_->eye==next->eye && frame_->z==next->z &&
        frame_->tile==next->tile && frame_->sight==next->sight && frame_->seeInDark==next->seeInDark &&
        frame_->status==next->status && inputs_==input;
    if (reused_) return frame_;
    if (next->status==VisibilityOutcome::Visible) {
        VisibilityFrame sampled;
        sampled.cells=input.cells; sampled.eye=next->eye; sampled.opacity=input.opacity;
        const bool through=(w.sight & 0x200u)!=0;
        const auto certain=through ? std::vector<int>(input.opacity.size(),1) : propagate(sampled,true);
        const auto possible=through || !next->unknownCells ? certain : propagate(sampled,false);
        const int sw=input.cells.width(),sh=input.cells.height();
        std::vector<uint8_t> seeds(input.opacity.size(),0),possibleSeeds(seeds);
        std::vector<VisibilityOutcome> geometry(input.opacity.size());
        for (int y=0;y<sh;++y) for (int x=0;x<sw;++x) {
            const size_t i=size_t(y)*sw+x;
            const auto state=input.opacity[i];
            geometry[i]=through ? VisibilityOutcome::Visible : state>=VisibilityFrame::Missing ?
                (state==VisibilityFrame::Deferred ? VisibilityOutcome::Budget : VisibilityOutcome::Unknown) :
                certain[i] ? VisibilityOutcome::Visible : possible[i] ?
                (next->deferredCells ? VisibilityOutcome::Budget : VisibilityOutcome::Unknown) : VisibilityOutcome::Hidden;
            if (possible[i]) {
                if (geometry[i]==VisibilityOutcome::Visible) seeds[i]=input.emission[i];
                possibleSeeds[i]=input.uncertain[i] ? 6 : input.emission[i];
            }
        }
        if (input.cells.contains(next->eye)) {
            const size_t eye=size_t(next->eye.y()-input.cells.y())*sw+next->eye.x()-input.cells.x();
            seeds[eye]=std::max(seeds[eye],w.seeInDark);
            possibleSeeds[eye]=std::max(possibleSeeds[eye],w.seeInDark);
        }
        const auto lit=illuminate(sampled,seeds,certain,true,through);
        const auto possiblyLit=next->unknownCells ? illuminate(sampled,possibleSeeds,possible,false,through) : lit;
        next->lightCells=input.cells;
        next->lighting.reserve(input.opacity.size());
        for (int y=0;y<sh;++y) for (int x=0;x<sw;++x) {
            const size_t i=size_t(y)*sw+x;
            const auto ambient=input.ambient[i];
            const auto result=ambient==1 || lit[i] ? VisibilityOutcome::Visible :
                ambient==VisibilityFrame::Deferred || (possiblyLit[i] && next->deferredCells) ? VisibilityOutcome::Budget :
                ambient==VisibilityFrame::Missing || possiblyLit[i] ? VisibilityOutcome::Unknown : VisibilityOutcome::Hidden;
            next->lighting.push_back(uint8_t(result));
        }
        const int outStride=next->cells.width()+1;
        for (auto& sum:next->sums) sum.resize(size_t(outStride)*(next->cells.height()+1),0);
        for (int y=1;y<=next->cells.height();++y) for (int x=1;x<=next->cells.width();++x) {
            const size_t i=size_t(y-1+next->cells.y()-input.cells.y())*sw+x-1+next->cells.x()-input.cells.x();
            const auto g=geometry[i],l=VisibilityOutcome(next->lighting[i]);
            next->opacity.push_back(input.opacity[i]);
            next->outcomes.push_back(uint8_t(g==VisibilityOutcome::Hidden || l==VisibilityOutcome::Hidden ? VisibilityOutcome::Hidden :
                g==VisibilityOutcome::Budget || l==VisibilityOutcome::Budget ? VisibilityOutcome::Budget :
                g==VisibilityOutcome::Unknown || l==VisibilityOutcome::Unknown ? VisibilityOutcome::Unknown : VisibilityOutcome::Visible));
            auto sum=[&](auto& s,bool value) {
                s[size_t(y)*outStride+x]=uint32_t(value)+s[size_t(y-1)*outStride+x]+s[size_t(y)*outStride+x-1]-s[size_t(y-1)*outStride+x-1];
            };
            for (size_t k=0;k<3;++k) sum(next->sums[k],g==(k==0 ? VisibilityOutcome::Visible : k==1 ? VisibilityOutcome::Unknown : VisibilityOutcome::Budget));
        }
        if (next->hasBlackness()) {
            // Stock covers both unlit and LOS-hidden cells, independently of
            // category/self admission. All three category bypasses disable it.
            // Unknown coverage is explicitly unsupported, not a guessed mask.
            next->blacknessReady=std::all_of(next->outcomes.begin(),next->outcomes.end(),[](uint8_t v) {
                return v==uint8_t(VisibilityOutcome::Visible) || v==uint8_t(VisibilityOutcome::Hidden);
            });
            if (next->blacknessReady) for (int y=next->cells.top();y<=next->cells.bottom();++y) {
                int start=next->cells.left();
                while (start<=next->cells.right()) {
                    if (next->cell(start,y)!=VisibilityOutcome::Hidden) { ++start; continue; }
                    int end=start+1;
                    while (end<=next->cells.right() && next->cell(end,y)==VisibilityOutcome::Hidden) ++end;
                    next->blackness.emplace_back(start,y,end-start,1); start=end;
                }
            }
        }
    }
    inputs_=std::move(input);
    frame_=std::move(next);
    return frame_;
}
}
