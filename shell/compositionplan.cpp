#include "shell/compositionplan.h"
#include "shell/blurkernel.h"
#include <QCache>
#include <QMutex>

namespace fugue::shell {
namespace {
// The box of an image's pixels that are not fully transparent; one of alpha 1 counts.
QRect inkBox(const QImage& image) {
    if (image.format() != QImage::Format_RGBA8888) return image.rect();
    static QMutex mutex;
    static QCache<qint64,QRect> cache(8192);
    QMutexLocker lock(&mutex);
    if (const auto* known = cache.object(image.cacheKey())) return *known;
    int left = image.width(), right = -1, top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) if (row[4*x+3]) {
            left = std::min(left,x); right = std::max(right,x); top = std::min(top,y); bottom = y;
        }
    }
    const QRect box = right < 0 ? QRect() : QRect(QPoint(left,top),QPoint(right,bottom));
    cache.insert(image.cacheKey(),new QRect(box));
    return box;
}
// What a part adds to the box of a KEEP_TOGETHER surface: its ink, turned with it; a surface adds all of its own, and
// maptext its box, not what an outline or shadow draws past that (stock 516.1689).
QRect inkRect(const CompositionPlan::Item& item) {
    const auto& s = *item.sprite;
    const bool surface = item.input >= 0;
    if (!surface && s.maptext) return s.rect().adjusted(s.textMargin,s.textMargin,-s.textMargin,-s.textMargin);
    if (!surface && (s.image.isNull() || !s.renderSource.isEmpty() ||
                     (!s.size.isEmpty() && s.size != s.image.size()))) return s.rect();
    if (surface && !s.affine) return s.rect();
    const QRect ink = surface ? QRect(QPoint(),s.size) : inkBox(s.image);
    if (ink.isEmpty() || !s.affine) return ink.translated(s.position);
    // Under a matrix, a part's or a nested group's box is the exact one out to whole pixels, and one pixel more on the
    // right or at the bottom when that leaves an odd size: an 18x16 box turned by 25 degrees counts as 24 px high, a
    // row further down than it reaches (stock 516.1689).
    const QRectF turned = s.affine->forward.mapRect(QRectF(ink));
    const double e = 1.0/1024; // a quarter turn lands on whole pixels
    const int left = int(std::floor(turned.left()+e)), top = int(std::floor(turned.top()+e));
    const int width = int(std::ceil(turned.right()-e))-left, height = int(std::ceil(turned.bottom()-e))-top;
    return QRect(left,top,width+(width&1),height+(height&1)).translated(s.position);
}
std::map<int,qsizetype> selectMasters(const MapSprites& sprites) {
    std::map<int,qsizetype> selected;
    for (qsizetype i = 0; i < sprites.size(); ++i)
        if (sprites[i].kind == MapSprite::Master) selected[sprites[i].plane] = i;
    return selected;
}
bool filteredInputPart(const MapSprite& s) {
    return !s.appearanceGroup && !s.affine && !s.rootAffine && !s.imageIdentity && !s.visHit && !s.maptext &&
        s.kind != MapSprite::Image && s.kind != MapSprite::Vis &&
        (s.size.isEmpty() || s.size == s.image.size()) && s.mouseOpacity == 1 &&
        s.image.size() == QSize(32,32) && s.blend <= 1 && s.renderSource.isEmpty() && s.renderTarget.isEmpty();
}
}
namespace {
// The alpha of one part of a render target at a scene pixel: its own pixel there, through its own matrix, times its
// own alpha.
double targetPartAlpha(const MapSprite& s, QPoint pixel) {
    if (s.image.isNull() || s.maptext || !s.rect().contains(pixel)) return 0;
    const QPoint local = pixel - s.position;
    QPoint sample(local.x() * s.image.width() / s.rect().width(), local.y() * s.image.height() / s.rect().height());
    if (s.affine) {
        const QPointF p = s.affine->inverse.map(QPointF(local) + QPointF(.5,.5));
        if (p.x() < 0 || p.y() < 0 || p.x() >= s.image.width() || p.y() >= s.image.height()) return 0;
        sample = {int(std::floor(p.x())), int(std::floor(p.y()))};
    }
    return s.image.pixelColor(sample).alphaF() * std::clamp(double(s.tint[3]), 0.0, 1.0);
}
std::vector<CompositionPlan::InputMask> inputMasks(const PlaneEffects& effects, QRect content) {
    std::vector<CompositionPlan::InputMask> out;
    for (const auto& f : effects.filters)
        if (f.kind == PlaneFilter::AlphaMask && f.image)
            out.push_back({*f.image, content.topLeft() + QPoint(int(std::lround(f.x)), int(std::lround(f.y))) +
                               QPoint((content.width() - f.image->width()) / 2, (content.height() - f.image->height()) / 2),
                           (f.flags & 1) != 0});
    return out;
}
// An alpha mask on an atom's own filters whose mask is an atom's render target hides from clicks what it masks out, as
// a mask from an icon does, and the right-click menu still lists the atom: the target centred on the atom's icon and
// moved by the filter's x, y; MASK_INVERSE hides what it keeps (stock 516.1689, plane-click-world selfmask,
// selfmask-inverse). The target's alpha is its parts' own pixels, their own matrix included; filters on the target's
// source are not applied, as for a plane's mask (rtalpha-rtoutline).
void targetMasks(std::vector<CompositionPlan::InputMask>& out, const PlaneEffects& effects, QRect content,
                 const LocalTargets& local, const MapSprites& sprites) {
    for (const auto& f : effects.filters) {
        if (f.kind != PlaneFilter::AlphaMask || f.icon != UINT32_MAX || f.source.isEmpty()) continue;
        const int index = local.find(f.source);
        QImage alpha;
        QPoint origin;
        if (index >= 0 && local.groups[size_t(index)].valid) {
            const auto& g = local.groups[size_t(index)];
            if (g.bounds.isEmpty() || g.bounds.width() > 1024 || g.bounds.height() > 1024) continue;
            alpha = QImage(g.bounds.size(), QImage::Format_ARGB32);
            alpha.fill(Qt::transparent);
            for (int y = 0; y < g.bounds.height(); ++y)
                for (int x = 0; x < g.bounds.width(); ++x) {
                    const QPoint pixel = g.bounds.topLeft() + QPoint(x, y);
                    double a = 0;
                    for (auto i : g.parts) a += targetPartAlpha(sprites[i], pixel) * (1 - a);
                    alpha.setPixelColor(x, y, QColor(0, 0, 0, int(std::lround(a * 255))));
                }
            origin = content.topLeft() + QPoint((content.width() - alpha.width()) / 2, (content.height() - alpha.height()) / 2);
        } else {   // a target that is not there is an empty mask
            alpha = QImage(1, 1, QImage::Format_ARGB32); alpha.fill(Qt::transparent);
            origin = content.topLeft();
        }
        out.push_back({alpha, origin + QPoint(int(std::lround(f.x)), int(std::lround(f.y))), (f.flags & 1) != 0});
    }
}
}
CompositionPlan::CompositionPlan(const MapSprites& scene, QRect viewport, CompositionBackend capabilities)
    : sprites(scene), source(viewport), backend(capabilities), local(sprites),
      masters(selectMasters(sprites)), planeSources(sprites,source,masters) {
    if (source.isEmpty()) { rejection.reason = PlanRejection::EmptySource; return; }
    try { plan(); }
    catch (const PlanRejection& error) { rejection = error; }
    if (supported) planInput();
}
std::shared_ptr<const CompositionPlan> CompositionPlan::build(const MapSprites& sprites, QRect source, CompositionBackend backend) {
    return std::shared_ptr<const CompositionPlan>(new CompositionPlan(sprites,source,backend));
}
void CompositionPlan::plan() {
    auto generated = [&](MapSprite sprite) { generated_.push_back(std::move(sprite)); return &generated_.back(); };
    std::map<int, std::vector<const MapSprite*>> planes;
    for (const auto& sprite : sprites) {
        if (sprite.kind != MapSprite::Master) planes[sprite.plane].push_back(&sprite);
    }
    for (const auto& [plane, master] : masters) {
        planes.try_emplace(plane);
        if (!sprites[master].renderTarget.isEmpty()) ++planeTargetCounts[sprites[master].renderTarget];
    }
    std::map<QString, bool> processedPlaneTargets;
    auto addPass = [&](std::vector<Item> items, QRect bounds = {}, bool byteCapture = false) {
        if (passes.size() >= size_t(std::numeric_limits<int>::max()))
            throw PlanRejection{PlanRejection::IndexRange,passes.size()+1,std::numeric_limits<int>::max()};
        const int index = int(passes.size());
        passes.push_back({-1, std::move(items), 0, bounds.isEmpty() ? source : bounds, byteCapture}); return index;
    };
    std::vector<int> localPasses(local.groups.size(), -2);
    // A hidden target's own filter, matrix or KEEP_TOGETHER material runs on a surface of its parts, inside what is
    // captured (stock 516.1689); those captures are
    // made below, once groups can be composed.
    auto surfaced = [&](const LocalTargets::Group& group) {
        return std::any_of(group.parts.begin(),group.parts.end(),[&](auto part) { return LocalTargets::ownSurface(sprites[part]); });
    };
    for (int i = 0; i < int(local.groups.size()); ++i) {
        const auto& group = local.groups[i];
        if (group.valid && !surfaced(group)) {  // also one a relay does not show: it still draws itself
            std::vector<Item> items;
            for (auto part : group.parts) items.push_back({&sprites[part],-1,{},false,part});
            localPasses[i] = addPass(std::move(items), group.bounds);
        }
    }
    // Byte capture after each contributor blend. Direct RGBA8 blending on D3D
    // rounds the incoming premultiplied fragment before blending, which loses
    // precision relative to stock. Blend on the shared float surface, then
    // materialize the byte boundary; no readback or CPU composition is involved.
    auto capturePlane = [&](std::vector<Item> items, QRect bounds = {}) {
        if (bounds.isEmpty()) bounds = source;
        int result = -1;
        MapSprite quad; quad.position = bounds.topLeft(); quad.size = bounds.size();
        for (auto item : items) {
            // Stock materializes contributor STRAIGHT RGBA before blending.
            // Use the byte attachment's conversion, not a CPU/shader imitation
            // of its tie arithmetic. Neutral resident rasters are already bytes.
            if (item.sprite->color != identityColor || item.sprite->tint != std::array<float,4>{1,1,1,1}) {
                const int blend = item.sprite->blend;
                auto material = *item.sprite; material.blend = 0;
                item.sprite = generated(std::move(material));
                // Resident and already-resolved axis-aligned geometry is known
                // before named-source resolution. Never use alpha/ink bounds:
                // a matrix can generate alpha in transparent texels. Empty
                // intersections retain the
                // reference path (addPass treats empty bounds as the viewport).
                QRect materialBounds = bounds;
                if (backend.floatSurfaces && backend.boundedMaterials && item.input >= -1 && !item.sprite->affine &&
                    item.sprite->renderSource.isEmpty() && item.sprite->renderTarget.isEmpty()) {
                    const QRect clipped = item.sprite->rect().intersected(bounds);
                    if (!clipped.isEmpty()) materialBounds = clipped;
                }
                const int straight = addPass({item},materialBounds,true);
                passes[straight].straightOutput = true;
                auto output = quad; output.position = materialBounds.topLeft(); output.size = materialBounds.size();
                output.blend = blend;
                item = {generated(std::move(output)),straight,{},true};
            }
            std::vector<Item> layer;
            if (result >= 0) layer.push_back({generated(quad),result});
            layer.push_back(item);
            const int blended = addPass(std::move(layer),bounds);
            Item capture{generated(quad),blended};
            capture.effect = {7,0,0,0,0,0,0,0,1.0f/bounds.width(),1.0f/bounds.height(),2,0};
            result = addPass({capture},bounds,true);
        }
        return result >= 0 ? result : addPass({}, bounds, true);
    };
    // Shared surface-operation graph. Master and relay admission are separate;
    // both use these passes, bounds, precision and global work/storage limits.
    int emptyMask = -1;
    // content: the unpadded surface stock filters (an atom's parts); initial: Fugue's padded capture around it.
    auto applyEffects = [&](int result, const PlaneEffects* effects, QRect initial = {}, QRect content = {}) {
        if (initial.isEmpty()) initial = source;
        if (content.isEmpty()) content = initial;
        MapSprite quad; quad.kind = MapSprite::Screen; quad.position = initial.topLeft(); quad.size = initial.size();
        QRect effectBounds = initial;
        if (effects && effects->boundedTreeFilters() && effects->filters.size() > 1) {
            // Retain the chain's float materialization boundary. Stock grows
            // each operation below, not one pre-padded domain for the chain.
            result = addPass({{generated(quad),result}},effectBounds);
        }
        auto filterPass = [&](int input, int operation, double size, const PlaneFilter& filter) {
            const QRect inputBounds = effectBounds;
            // Stock outline: ceil(step) on every side, every pass. Small blur
            // expands both axes on its horizontal pass, then retains that domain.
            const int padding = operation == 5 || operation == 18 ? int(std::ceil(size)) :
                operation == 3 ? std::min(stockBlurKernel(float(size)).radius,
                    filter.kind == PlaneFilter::Shadow || filter.kind == PlaneFilter::Bloom ? 100 :
                    std::numeric_limits<int>::max()) : 0;
            effectBounds.adjust(-padding,-padding,padding,padding);
            quad.position = effectBounds.topLeft(); quad.size = effectBounds.size();
            Item item{generated(quad), input};
            item.effect = {float(operation), float(size), float(padding), float(filter.flags),
                filter.color[0], filter.color[1], filter.color[2], filter.color[3],
                1.0f/inputBounds.width(), 1.0f/inputBounds.height(), 1.0f, 0.0f};
            return addPass({item},effectBounds);
        };
        auto blur = [&](int input, const PlaneFilter& filter) {
            if (filter.size <= 0 || (filter.kind == PlaneFilter::Shadow && filter.size < .001)) return input;
            if (filter.kind == PlaneFilter::Shadow || filter.kind == PlaneFilter::Bloom) {
                // Shadow and bloom intermediates are A8R8G8B8 on every axis/recursive
                // step. The standalone large-blur float profile is not reused.
                const auto steps = stockBlurSteps(filter.size);
                for (int i=0;i<steps.count;++i)
                    input = filterPass(filterPass(input,3,steps.sizes[i],filter),4,steps.sizes[i],filter);
                return input;
            }
            if (filter.size > 4 && effects->filters.size() == 1) {
                const QRect blurSource = effectBounds;
                const auto steps = stockBlurSteps(filter.size);
                std::array<int,6> radii{};
                int total = 0;
                for (int i = 0; i < steps.count; ++i) {
                    radii[i] = stockBlurKernel(steps.sizes[i]).radius;
                    total += radii[i];
                }
                int doneX = 0, doneY = 0;
                for (int i = 0; i < steps.count; ++i) for (int axis = 0; axis < 2; ++axis) {
                    (axis == 0 ? doneX : doneY) += radii[i];
                    // Exact finite-support domain of the remaining cascade.
                    const int px = std::min(doneX,total-doneX), py = std::min(doneY,total-doneY);
                    const QRect bounds = blurSource.adjusted(-px,-py,px,py);
                    const QRect from = passes[input].bounds;
                    auto output = quad;output.position = bounds.topLeft();output.size = bounds.size();output.smooth = true;
                    Item item{generated(std::move(output)),input};
                    item.effect = {float(9+axis),steps.sizes[i],float(bounds.x()-from.x()),float(bounds.y()-from.y()),
                        float(bounds.width()),float(bounds.height()),0,0,1.0f/from.width(),1.0f/from.height(),
                        i == steps.count-1 && axis == 1 ? 1.0f : 0.0f,0};
                    input = addPass({item},bounds);
                }
                effectBounds = passes[input].bounds;
                quad.position = effectBounds.topLeft(); quad.size = effectBounds.size();
                return input;
            }
            const double size = std::min(filter.size, 100.0);
            return filterPass(filterPass(input, 3, size, filter), 4, size, filter);
        };
        auto outline = [&](const PlaneFilter& filter) {
            // Stock square steps double; round steps use the 1.75 ladder.
            // Each ladder needs <=16 passes at the generic8192 size bound.
            auto ladder = [&](double left, const PlaneFilter& operation) {
                const bool square = operation.flags & 2;
                double step = 1;
                // Even size zero colors partial source alpha in stock.
                if (left <= 1) result = filterPass(result, 5, left, operation);
                else for (int pass = 0; pass < 16; ++pass) {
                    if (!square && 2*step >= left && step > 2) step = left*.5;
                    result = filterPass(result, 5, step, operation);
                    left -= step; step *= square ? 2 : 1.75;
                    if (left <= step) { result = filterPass(result, 5, left, operation); break; }
                }
            };
            // Sharp round outlines first execute a square/sharp prefix.
            if (filter.flags == 1 && filter.size > 1.5) {
                auto square = filter; square.flags |= 2;
                const double prefix = std::max(1.0,std::floor(filter.size*.5));
                ladder(prefix,square); ladder(filter.size-prefix,filter);
            } else ladder(filter.size,filter);
        };
        if (effects && !effects->filters.empty()) {
            const bool shadow = std::any_of(effects->filters.begin(),effects->filters.end(),[](const auto& f) {
                return f.kind == PlaneFilter::Shadow || f.kind == PlaneFilter::Wave ||
                    f.kind == PlaneFilter::Ripple || f.kind == PlaneFilter::Bloom;
            });
            // Entry copy: premultiplied bytes (13) before a shadow, wave, ripple or bloom, straight bytes rounded half
            // up (7) otherwise. No retained stock capture tells the two apart: the stock-compared tests pass with either.
            result = filterPass(result,shadow ? 13 : 7,0,PlaneFilter{});
        }
        // Stock's own surface: the content grown by every operation so far (Fugue's capture is padded around it).
        auto stockRect = [&] {
            return content.adjusted(effectBounds.left()-initial.left(), effectBounds.top()-initial.top(),
                                    effectBounds.right()-initial.right(), effectBounds.bottom()-initial.bottom());
        };
        // Stock 516.1688 wave and ripple: consecutive records of one type share a
        // pass (at most 10; wave groups by flags without WAVE_SIDEWAYS, ripple by all flags); records with no effect
        // are consumed without counting. The pass grows the surface by the summed amplitude (ceil, at most 32 px;
        // none with WAVE_BOUNDED, which clamps sampling to the surface edge instead of a transparent border).
        // Wave phase scales by the grown size over the original (stock's waveDir uses the grown size).
        auto displacement = [&](size_t first) {
            const auto& chain = effects->filters;
            const auto kind = chain[first].kind;
            const QRect stock = stockRect();
            const double W = std::max(1, stock.width()), H = std::max(1, stock.height());
            std::vector<const PlaneFilter*> accepted;
            int key = 0;
            size_t next = first;
            for (; next < chain.size() && chain[next].kind == kind && accepted.size() < 10; ++next) {
                const auto& f = chain[next];
                if (kind == PlaneFilter::Wave ? std::abs(float(f.size)) < .001 || f.x*f.x + f.y*f.y < .000001 :
                    std::abs(float(f.size)) <= .001 || std::abs(float(f.repeat)) <= .001 || f.falloff < 0) continue;
                const int k = kind == PlaneFilter::Wave ? (f.flags & ~1) : f.flags;
                if (!accepted.empty() && k != key) break;
                key = k; accepted.push_back(&f);
            }
            if (accepted.empty()) return next;
            const bool bounded = key & 2;
            auto params = std::make_shared<std::array<float,64>>();
            (*params)[0] = float(accepted.size()); (*params)[1] = bounded ? 1.f : 0.f;
            int padX = 0, padY = 0;
            if (kind == PlaneFilter::Wave) {
                std::vector<std::array<float,2>> amplitude;
                double sumX = 0, sumY = 0;
                for (const auto* f : accepted) {
                    const double x = float(f->x), y = float(f->y), length2 = x*x + y*y;
                    const double s = std::sqrt(1/length2) * float(f->size);
                    amplitude.push_back((f->flags & 1) ? std::array<float,2>{float(-y*s), float(x*s)} :
                                                         std::array<float,2>{float(x*s), float(y*s)});
                    sumX += std::abs(amplitude.back()[0]); sumY += std::abs(amplitude.back()[1]);
                }
                if (!bounded) { padX = std::min(32, int(std::ceil(sumX))); padY = std::min(32, int(std::ceil(sumY))); }
                for (size_t i = 0; i < accepted.size(); ++i) {
                    const auto* f = accepted[i];
                    const double x = float(f->x), y = float(f->y), length2 = x*x + y*y;
                    float* p = params->data() + 4 + 6*i;
                    p[0] = float(1/length2*x) * float(uint16_t(W + 2*padX));
                    p[1] = float(1/length2*y) * float(uint16_t(H + 2*padY));
                    p[2] = amplitude[i][0] * float(1/W); p[3] = amplitude[i][1] * float(1/H);
                    p[4] = float(std::fmod(float(f->offset), 1.0));
                }
            } else {
                double sum = 0;
                for (size_t i = 0; i < accepted.size(); ++i) {
                    const auto* f = accepted[i];
                    sum += float(f->size);
                    float* p = params->data() + 4 + 6*i;
                    p[0] = float(W*.5 + f->x); p[1] = float(H*.5 + f->y);
                    p[2] = std::abs(float(f->repeat)); p[3] = float(f->falloff) * 2.5f;
                    p[4] = float(f->radius) / float(f->repeat); p[5] = float(f->size);
                }
                // A negative summed size would shrink stock's surface; not modelled (kept unpadded).
                if (!bounded) padX = padY = std::clamp(int(std::ceil(sum)), 0, 32);
            }
            const QRect from = effectBounds;
            effectBounds.adjust(-padX,-padY,padX,padY);
            quad.position = effectBounds.topLeft(); quad.size = effectBounds.size();
            Item item{generated(quad), result};
            item.effect = {kind == PlaneFilter::Wave ? 15.f : 16.f, 0,
                float(effectBounds.x()-from.x()), float(effectBounds.y()-from.y()),
                float(stock.x()-from.x()), float(stock.y()-from.y()), float(W), float(H),
                1.f/from.width(), 1.f/from.height(), float(effectBounds.width()), float(effectBounds.height())};
            item.params = std::move(params);
            result = addPass({item},effectBounds);
            return next;
        };
        // Stock 516.1688 bloom: keep pixels with a straight channel above the threshold, spread them
        // (outline ladder, spread pass) and blur them on byte surfaces, subtract the kept pixels (RGB only),
        // then add the glow times the bloom alpha into the surface itself: the glow never grows the surface.
        auto bloom = [&](const PlaneFilter& filter) {
            if (filter.color[3] == 0 || !(filter.size >= .001 || filter.offset >= .001)) return;
            const int original = result;
            const QRect sourceBounds = effectBounds;
            const auto sourceQuad = quad;
            Item kept{generated(quad), result};
            kept.effect = {17,0,0,0,filter.color[0],filter.color[1],filter.color[2],1,
                1.f/effectBounds.width(),1.f/effectBounds.height(),0,1};
            const int threshold = result = addPass({kept},effectBounds);
            if (filter.offset != 0) {
                auto spread = filter; spread.flags = 0;
                double left = filter.offset, step = 1;
                if (left > 1) for (int pass = 0; pass < 16; ++pass) {
                    if (2*step >= left && step > 2) step = left*.5;
                    result = filterPass(result, 18, step, spread);
                    left -= step; step *= 1.75;
                    if (left <= step) break;
                }
                result = filterPass(result, 18, left, spread);
            }
            if (filter.size > .001) result = blur(result, filter);
            Item glow{generated(quad), result}, cut{generated(sourceQuad), threshold};
            glow.effect[0] = cut.effect[0] = 14;
            cut.stockSubtract = true;
            result = filterPass(addPass({glow, cut}, effectBounds), 13, 0, PlaneFilter{});
            auto glowQuad = quad; glowQuad.blend = 2;
            effectBounds = sourceBounds; quad = sourceQuad;
            // Clip to stock's surface, not Fugue's padded capture (bloom-outline in filter-types-world).
            const QRect stock = stockRect();
            Item base{generated(sourceQuad), original}, added{generated(std::move(glowQuad)), result};
            base.effect[0] = 14;
            added.effect = {20,0,0,0,filter.color[3],filter.color[3],filter.color[3],filter.color[3],0,0,0,0};
            auto stockQuad = sourceQuad; stockQuad.position = stock.topLeft(); stockQuad.size = stock.size();
            Item clipped{generated(std::move(stockQuad)), addPass({base, added}, stock)};
            clipped.effect[0] = 13;
            result = addPass({clipped}, effectBounds);
        };
        if (effects) for (size_t index = 0; index < effects->filters.size(); ++index) {
            const auto& filter = effects->filters[index];
            if (filter.kind == PlaneFilter::Wave || filter.kind == PlaneFilter::Ripple) {
                index = displacement(index) - 1;
                continue;
            }
            if (filter.kind == PlaneFilter::Bloom) { bloom(filter); continue; }
            if (filter.kind == PlaneFilter::Blur) result = blur(result, filter);
            else if (filter.kind == PlaneFilter::Outline) outline(filter);
            else if (filter.kind == PlaneFilter::Shadow) {
                // Stock shadow: keep original geometry separate from the
                // displaced/grown/blurred domain; never stretch a padded input.
                const int original = result;
                const auto originalQuad = quad;
                const QRect originalBounds = effectBounds;
                auto shadowPass = [&](int operation, QPointF displacement, QRect bounds) {
                    const QRect from = effectBounds;
                    effectBounds = bounds;
                    quad.position = bounds.topLeft(); quad.size = bounds.size();
                    Item item{generated(quad),result};
                    item.effect = {float(operation),0,
                        float(bounds.x()-from.x()-displacement.x()),float(bounds.y()-from.y()-displacement.y()),
                        filter.color[0],filter.color[1],filter.color[2],filter.color[3],
                        1.f/from.width(),1.f/from.height(),float(bounds.width()),float(bounds.height())};
                    result = addPass({item},bounds);
                };
                const QPointF displacement(filter.x,filter.y);
                const QRect shifted = QRectF(originalBounds).translated(displacement).toAlignedRect();
                shadowPass(filter.grow > 0 ? 12 : 11, displacement, originalBounds.united(shifted));
                if (filter.grow > 0) {
                    auto growth = filter; growth.kind = PlaneFilter::Outline;
                    growth.size = filter.grow; growth.flags = 0; growth.color = {1,1,1,1};
                    outline(growth);
                    shadowPass(11,{},effectBounds);
                }
                result = blur(result,filter);
                Item shadowItem{generated(quad),result}, originalItem{generated(originalQuad),original};
                shadowItem.effect[0] = originalItem.effect[0] = 14;
                result = addPass({shadowItem,originalItem},effectBounds);
                // Stock A8R8G8B8 premultiplied attachment, not straight-RGB rounding.
                result = filterPass(result,13,0,filter);
            } else if (filter.kind == PlaneFilter::Color) {
                auto material = quad; material.color = filter.material;
                result = addPass({{generated(std::move(material)),result}},effectBounds,true);
            } else if (filter.kind == PlaneFilter::AlphaMask) {
                // The mask is its source plane's surface, or an icon or an atom's render target centred on this
                // surface, moved by x, y; where it does not reach it is transparent.
                // MASK_INVERSE (1) masks by 1 - alpha. MASK_SWAP (2) draws the mask's pixels, masked by this surface's
                // alpha. Any place in a chain (stock 516.1689, stock-parity-world plane-mask-filters).
                if (emptyMask < 0) emptyMask = addPass({});
                auto named = quad; named.renderSource = filter.source;
                Item copy{generated(std::move(named))};
                copy.maskOffset = QPoint(int(std::lround(filter.x)), int(std::lround(filter.y)));
                copy.maskCentre = content;
                if (filter.icon != UINT32_MAX) {  // not loaded yet: an empty mask
                    auto icon = quad; icon.renderSource.clear();
                    if (filter.image) {
                        icon.image = *filter.image; icon.size = icon.image.size();
                        icon.position = content.topLeft() + copy.maskOffset +
                            QPoint((content.width() - icon.size.width()) / 2, (content.height() - icon.size.height()) / 2);
                    }
                    copy = filter.image ? Item{generated(std::move(icon))} : Item{generated(std::move(icon)), emptyMask};
                } else copy.maskSource = true;
                const int moved = addPass({copy},effectBounds,true);
                const bool inverse = filter.flags & 1, swap = filter.flags & 2;
                auto alpha = quad; alpha.color = {};
                alpha.color[15] = inverse ? -1.f : 1.f; alpha.color[19] = inverse ? 1.f : 0.f;
                Item maskItem{generated(std::move(alpha)), swap ? result : moved}; maskItem.alphaMask = true;
                result = addPass({{generated(quad), swap ? moved : result},maskItem},effectBounds,true);
            }
        }
        return std::pair{result,quad};
    };
    // Anonymous captures share the same GPU graph/byte boundaries as named
    // planes. Group identity is occurrence + immutable template node, never an
    // asset name or atom slot. Nesting cannot merge independently placed copies.
    // capture: the parts of one hidden render target, composed into what it captures wherever it stands.
    auto groupedItems = [&](const std::vector<const MapSprite*>& members, bool capture = false) {
        using Key = std::pair<uint64_t,const AppearanceGroup*>;
        std::map<Key,std::vector<const MapSprite*>> contents;
        for (const auto* s:members) {
            int depth=0;
            for (auto g=s->appearanceGroup;g;g=g->parent) {
                const auto& t=g->transform;
                const std::array<double,6> matrix{t.m11(),t.m12(),t.m21(),t.m22(),t.dx(),t.dy()};
                if (++depth>32 || !finiteColor(g->color) || !std::isfinite(g->layer) ||
                    !std::all_of(g->tint.begin(),g->tint.end(),[](float v){return std::isfinite(v);}) ||
                    !t.isAffine() || !t.isInvertible() ||
                    !std::all_of(matrix.begin(),matrix.end(),[](double v){return std::isfinite(v) && std::abs(v)<1e9;}))
                    throw PlanRejection{PlanRejection::InvalidPass};
                const Key key{s->groupOccurrence,g.get()};
                contents[key].push_back(s);
            }
        }
        auto walk = [&](auto&& self, const std::vector<const MapSprite*>& list, const AppearanceGroup* parent, int depth) -> std::vector<Item> {
            std::vector<Item> out;
            if (depth>32) throw PlanRejection{PlanRejection::InvalidPass};
            std::set<Key> emitted;
            for (const auto* s:list) {
                auto g=s->appearanceGroup;
                // A hidden render target inside a group (a vis_contents member of a KEEP_TOGETHER holder) is captured, not
                // drawn (stock 516.1689, stock-parity-world overlay-relays ov-vis-src-kt).
                if (s->renderTarget.startsWith('*') && !capture) continue;
                if (g.get()==parent) { out.push_back({s,-1,{},false,s-sprites.constData()}); continue; }
                while (g && g->parent.get()!=parent) g=g->parent;
                if (!g) continue;
                const Key key{s->groupOccurrence,g.get()};
                if (!emitted.insert(key).second) continue;
                auto items=self(self,contents.at(key),g.get(),depth+1);
                // The group's matrix about the group's origin, as the matrix of a rectangle standing at `relative`.
                auto turnedBy=[&](QPointF relative,QSize size) {
                    const auto& t=g->transform;
                    const QPointF shift=t.map(relative)-relative;
                    StaticAffine affine;
                    affine.forward=QTransform(t.m11(),t.m12(),t.m21(),t.m22(),shift.x(),shift.y());
                    affine.inverse=affine.forward.inverted();
                    const QRectF moved=affine.forward.mapRect(QRectF(QPointF(),size));
                    if (std::max({std::abs(moved.left()),std::abs(moved.right()),std::abs(moved.top()),std::abs(moved.bottom())})>=1e9)
                        throw PlanRejection{PlanRejection::InvalidPass};
                    affine.bounds=moved.toAlignedRect();
                    return affine;
                };
                // A KEEP_TOGETHER surface is as large as what is drawn in it: the box of its parts' ink, an alpha of 1 and
                // an overlay of alpha 0 included, maptext by its whole box. A matrix samples it smoothed up to that box,
                // where the edge texel repeats, and draws nothing beyond: an edge that lies on the box is hard, one inside
                // it soft (stock 516.1689, stock-parity-world surface-bounds: a single pixel scaled by 4 is a 4 px
                // block). A filtered surface keeps its parts' whole icons and a soft edge (kt-outline-anim-turn). The
                // surface of an icon with its own maptext ends hard too, but holds the whole icon.
                const bool tight=!(g->effects && !g->effects->filters.empty());
                // With nothing in it but one icon the group has no surface: that icon is drawn like any other, its
                // edge smoothed towards transparent black (ta-kt-alone-turn, overlay-apart-in-kt-turn, tb-kt-alone-scale).
                if (tight && items.size()==1 && items[0].input==-1) {
                    const auto& part=*items[0].sprite;
                    if (!part.maptext && !part.image.isNull() && part.renderSource.isEmpty() && !part.affine &&
                        (part.size.isEmpty() || part.size==part.image.size()) && part.blend<=1 &&
                        part.tint==std::array<float,4>{1,1,1,1} && part.color==identityColor) {
                        MapSprite plain=part;
                        plain.tint=g->tint;plain.color=g->color;plain.blend=g->blend;plain.layer=g->layer;
                        plain.appearanceGroup.reset();
                        if (!g->transform.isIdentity()) {
                            plain.affine=turnedBy(plain.position-s->groupOrigin,plain.image.size());
                            plain.smooth=!g->nearest;
                        }
                        out.push_back({generated(std::move(plain)),-1,{},false,items[0].original});
                        continue;
                    }
                }
                QRect bounds;
                for (const auto& item:items) bounds=bounds.united(tight && !g->filterOnly ? inkRect(item) : item.sprite->rect());
                bool transformed=false;
                for (auto a=g;a;a=a->parent) transformed|=!a->transform.isIdentity();
                // An atom's filters run on float surfaces, on the group's untransformed contents; its matrix then
                // moves the filtered surface (stock 516.1688, transform-group-world). Filter ink beyond the view still
                // lands in it, so pad before clipping to the view.
                const PlaneEffects* effects=g->effects && !g->effects->filters.empty() && backend.floatSurfaces ?
                    g->effects.get() : nullptr;
                const QRect content=bounds;
                if (g->effects) {
                    auto masks=inputMasks(*g->effects,content);
                    targetMasks(masks,*g->effects,content,local,sprites);
                    if (!masks.empty()) groupMasks[{key.first,key.second}]=std::move(masks);
                }
                if (effects) bounds=bounds.adjusted(-filterReach(*effects),-filterReach(*effects),
                                                    filterReach(*effects),filterReach(*effects));
                if (!transformed && !capture) bounds=bounds.intersected(source.adjusted(-256,-256,256,256));
                if (!transformed && !effects && !capture) bounds=bounds.intersected(source);
                if (bounds.isEmpty()) continue;
                if (bounds.width()>8192 || bounds.height()>8192) throw PlanRejection{PlanRejection::InvalidPass};
                int input=capturePlane(std::move(items),bounds);
                if (effects) {
                    const auto [filtered,filteredQuad]=applyEffects(input,effects,bounds,content);
                    input=filtered; bounds=QRect(filteredQuad.position,filteredQuad.size);
                }
                MapSprite quad; quad.position=bounds.topLeft();quad.size=bounds.size();
                quad.tint=g->tint;quad.color=g->color;quad.blend=g->blend;quad.layer=g->layer;
                if (!g->transform.isIdentity()) {
                    if (quad.tint!=std::array<float,4>{1,1,1,1} || quad.color!=identityColor) {
                        auto material=quad;material.blend=0;
                        input=addPass({{generated(std::move(material)),input}},bounds,true);
                        quad.tint={1,1,1,1};quad.color=identityColor;
                    }
                    quad.affine=turnedBy(bounds.topLeft()-s->groupOrigin,bounds.size());
                    // PIXEL_SCALE on the group samples its surface unsmoothed (ta-kt-turn-pixelscale): the texel the pixel's
                    // centre falls in, on a boundary the one before it except in a triangle of every seventh column, and
                    // nothing past the surface's edge (stock 516.1689, master-transform-world fx-grid-kt-pxs-s50, -s60).
                    // The graphics card's nearest sampler over this hard-edged quad gives exactly that on Direct3D 11,
                    // OpenGL and Vulkan, the triangle included, so the shader's own pick (texel.w) is not used here.
                    quad.smooth=!g->nearest;
                }
                const bool turned=quad.affine.has_value();
                out.push_back({generated(std::move(quad)),input});
                out.back().hardEdge=tight && turned;
            }
            // Layer ties keep the order the parts were added in, BLEND_ADD ones included (stock 516.1689,
            // stock-parity-world kt-add-first, kt-add-last).
            if (parent) std::stable_sort(out.begin(),out.end(),[](const Item& a,const Item& b) {
                const float al=a.sprite->appearanceGroup ? a.sprite->groupLayer : a.sprite->layer;
                const float bl=b.sprite->appearanceGroup ? b.sprite->groupLayer : b.sprite->layer;
                return al<bl;
            });
            return out;
        };
        return walk(walk,members,nullptr,0);
    };
    for (int i = 0; i < int(local.groups.size()); ++i) {
        const auto& group = local.groups[i];
        if (group.valid && surfaced(group)) {
            std::vector<const MapSprite*> members;
            for (auto part : group.parts) members.push_back(&sprites[part]);
            localPasses[i] = addPass(groupedItems(members,true), group.bounds);
        }
        if (local.find(group.name) == i || local.find(group.name) < 0) targets[group.name] = local.find(group.name) == i ? localPasses[i] : -2;
    }
    // Build a dataflow graph before resolving sources: targets can be on later planes.
    for (const auto& [plane, members] : planes) {
        // Bound planning storage independently of reachable GPU surface admission.
        std::vector<Item> items;
        for (auto grouped : groupedItems(members)) {
            const auto* sprite = grouped.sprite;
            // (a surface, or the one icon of a group drawn in its place)
            if (grouped.input >= 0 || (grouped.original >= 0 && sprite != &sprites[grouped.original])) { items.push_back(grouped); continue; }
            if (!sprite->renderTarget.isEmpty()) {
                const auto index = sprite - sprites.constData();
                const int group = index >= 0 && index < sprites.size() ? local.owner[size_t(index)] : -1;
                if (group >= 0 && !sprite->renderTarget.startsWith('*') &&
                    &sprites[local.groups[group].parts.back()] == sprite && localPasses[group] >= 0) {
                    MapSprite quad; quad.position = local.groups[group].bounds.topLeft();
                    quad.size = local.groups[group].bounds.size();
                    items.push_back({generated(std::move(quad)),localPasses[group]});
                }
                continue;
            }
            if (!sprite->renderSource.isEmpty() && sprite->kind != MapSprite::Screen &&
                !local.names.count(sprite->renderSource)) continue;
            items.push_back({sprite,-1,{},false,sprite-sprites.constData()});
        }
        auto found = masters.find(plane);
        if (found == masters.end()) { final.insert(final.end(), items.begin(), items.end()); continue; }
        const auto& master = sprites[found->second];
        auto& disposition = masterDisposition[plane];
        disposition = MasterDisposition::Skipped;
        const auto* effects = master.planeEffects.get();
        if (effects) ignoredFilters += effects->ignoredFilters;
        const bool materialFilters = effects && std::any_of(effects->filters.begin(),effects->filters.end(),[](const auto& f) {
            return f.kind == PlaneFilter::AlphaMask || f.kind == PlaneFilter::Color;
        });
        // The master's own colour and alpha apply after its filters (stock-parity-world mask-color, mask-matrix,
        // mask-alpha, colorfilter-color).
        if ((effects && (!effects->valid(source.size()) || (!effects->filters.empty() &&
            (!backend.floatSurfaces || (!materialFilters && (master.color[3] != 0 || master.color[7] != 0 ||
                                                             master.color[11] != 0 || master.color[19] != 0))))))) {
            if (!master.renderTarget.isEmpty()) targets[master.renderTarget] = -2;
            disposition = MasterDisposition::Refused;
            continue;
        }
        if (master.tint[3] <= 0 && master.renderTarget.isEmpty()) continue;
        const bool identity = master.color == identityColor && master.tint == std::array<float,4>{1,1,1,1};
        const bool effected = effects && (!effects->filters.empty() || effects->matrix);
        if (master.blend <= 1 && identity && !effected && master.renderTarget.isEmpty()) {
            disposition = MasterDisposition::Forward;
            final.insert(final.end(), items.begin(), items.end()); continue;
        }
        if (items.empty() && master.renderTarget.isEmpty() && master.color[19] == 0) continue;
        // Plane targets are byte-premultiplied captures, including blending
        // between contributors. Do not quantize float filter intermediates or
        // resident straight-alpha images to imitate that boundary.
        disposition = MasterDisposition::Captured;
        const bool targetCapture = !master.renderTarget.isEmpty();
        const int captured = targetCapture ? capturePlane(std::move(items)) : addPass(std::move(items));
        // The filters run on the untransformed plane; the matrix turns their result below.
        PlaneEffects surface;
        if (effects) { surface = *effects; surface.matrix.reset(); }
        auto [result,quad] = applyEffects(captured,effects ? &surface : nullptr);
        auto composite = quad; composite.tint = master.tint; composite.color = master.color;
        bool materialPending = !identity;
        // Scalar masters byte-round the premultiplied modulator, not sampled RGB.
        // Decoded matrices and contributor material retain their separate paths.
        if (materialPending && master.color == identityColor) {
            Item material{generated(composite),result}; material.effect[0] = 1;
            result = addPass({material},passes[result].bounds,true);
            composite.tint = {1,1,1,1}; materialPending = false;
        }
        // Materialize color BEFORE resampling a transformed surface. Combining
        // nonlinear clamp/color with affine sampling reverses the contract.
        if (effects && effects->matrix) {
            if (materialPending) result = addPass({{generated(composite), result}},passes[result].bounds,targetCapture);
            composite.tint = {1,1,1,1}; composite.color = identityColor;
            composite.affine = StaticAffine::make(*effects->matrix, source.size());
            // Stock 516.1689 samples the turned plane bilinearly, or nearest under the master's PIXEL_SCALE
            // (master-transform-world fx-scalex2, fx-pxs-scalex2). Nearest takes the texel the pixel's centre falls in,
            // and on a texel boundary the one before it (fx-grid-pxs-*; see the renderer).
            composite.smooth = !master.nearest;
            composite.nearest = master.nearest;
            // The pivot is the view's centre however far the filters grew the surface, and what they drew past
            // the view turns with the rest (stock 516.1689, master-transform-world turn90-shadow6,
            // half-edge-outline4).
            const QPointF grown = composite.position - source.topLeft();
            if (composite.affine && !grown.isNull()) {
                auto& affine = *composite.affine;
                affine.forward = QTransform::fromTranslate(grown.x(),grown.y()) * affine.forward *
                                 QTransform::fromTranslate(-grown.x(),-grown.y());
                affine.inverse = affine.forward.inverted();
                affine.bounds = affine.forward.mapRect(QRectF(QPointF(),composite.size)).toAlignedRect();
            }
            // Stock 516.1689 samples an unfiltered plane as though it ended at the part of it the view shows: the view
            // taken back through the matrix, its bounding box rounded out to whole plane pixels. An enlarged plane's
            // outermost row and column of the view are so not mixed with what lies just outside it (master-transform-
            // world fx-grid-s200, -s200t90, -sx200y300; -s150, -s300, -s200m3 round outward, not to nearest; turned:
            // -s300t1, -s400t4, and a limit turned with the view is ruled out by -s200t30). The same holds with a
            // render target, visible or hidden (fx-grid-rtv-s200, -rth-s200), with the master's own colour or alpha
            // (-tint-, -dim-) and with filters that only recolour pixels where they are, a colour matrix or an alpha
            // mask (-swap-, -cover-). A filter that draws past the pixels it reads (an outline) leaves the whole surface
            // sampled (fx-grid-outline1-s200). Where that box lies past the plane, the plane's own edge still fades to
            // clear over a texel (fx-grid-s50t30), so the box is not cut to the plane.
            const bool inPlace = !effects || std::all_of(effects->filters.begin(),effects->filters.end(),[](const auto& f) {
                return f.kind == PlaneFilter::Color || f.kind == PlaneFilter::AlphaMask; });
            if (composite.affine && inPlace) {
                const QRectF view(QPointF(source.topLeft()-composite.position),QSizeF(source.size()));
                const QRectF shown = composite.affine->inverse.mapRect(view);
                const double margin = 1e-6;   // a bound on a whole pixel is not pushed outward by rounding error
                composite.affine->sampled = QRect(
                    QPoint(int(std::floor(shown.left()+margin)),int(std::floor(shown.top()+margin))),
                    QPoint(int(std::ceil(shown.right()-margin))-1,int(std::ceil(shown.bottom()-margin))-1));
            }
        }
        if (!master.renderTarget.isEmpty()) {
            if (materialPending || (effects && effects->matrix)) result = addPass({{generated(composite), result}},{},true);
            targets[master.renderTarget] = result;
            // Even a decoded zero-size filter materializes the source basis.
            // Raster alpha and suppressed consumer children do not select it.
            processedPlaneTargets[master.renderTarget] = !identity || effected;
            if (!master.renderTarget.startsWith('*')) {
                quad.blend = master.blend;
                final.push_back({generated(std::move(quad)), result});
            }
        } else {
            composite.blend = master.blend;
            final.push_back({generated(std::move(composite)), result});
        }
    }
    // Effects, not material or consumer identity, key shared source operations.
    std::map<std::pair<int,std::vector<PlaneFilter>>,int> relayEffects;
    auto resolve = [&](Item& item) {
        if (item.maskSource) {
            const auto name = item.sprite->renderSource;
            const auto target = targets.find(name);
            // One producer only. A plane's surface keeps its scene-space bounds, never stretched into the viewport; an
            // atom's render target is its own icon centred on the masked surface, wherever the atom is (stock 516.1689,
            // stock-parity-world plane-alpha-mask-*).
            const bool atom = local.names.count(name) && target != targets.end() && target->second >= 0;
            item.input = atom || (planeTargetCounts[name] == 1 && target != targets.end() && target->second >= 0)
                             ? target->second : emptyMask;
            const QRect bounds = passes[item.input].bounds;
            auto quad = *item.sprite;
            const QRect centre = item.maskCentre;
            quad.position = (atom ? centre.topLeft() + QPoint((centre.width() - bounds.width()) / 2,
                                                              (centre.height() - bounds.height()) / 2)
                                  : bounds.topLeft()) + item.maskOffset;
            quad.size = bounds.size();
            quad.renderSource.clear();
            item.sprite = generated(std::move(quad));
            return;
        }
        const auto& dependency = item.sprite->planeSourceDependency;
        if (!dependency.isEmpty() && !local.names.count(dependency)) {
            const auto target = targets.find(dependency);
            const auto* effects = item.sprite->planeEffects.get();
            if (planeTargetCounts[dependency] != 1 || target == targets.end() || target->second < 0 ||
                (effects && (!effects->relayFilters() ||
                 (!effects->filters.empty() && !backend.floatSurfaces) ||
                 (effects->matrix && !effects->relayAffine(source.size(),true))))) {
                item.input = -2; return; // refuse the entire extended appearance, including its children
            }
        }
        if (item.input >= 0 || item.sprite->renderSource.isEmpty()) return;
        const auto found = targets.find(item.sprite->renderSource);
        item.input = found == targets.end() ? -2 : found->second;
        auto quad = *item.sprite;
        if (local.names.count(quad.renderSource)) {
            const int index = local.find(quad.renderSource);
            // The consumer's own colour, alpha and blend apply to the captured surface (stock 516.1689,
            // stock-parity-world relay-hud-color, relay-obj-color).
            item.input = index < 0 || quad.affine || quad.rootAffine || quad.maptext || quad.blend > 5 ? -2 : localPasses[index];
            if (item.input >= 0) {
                // The relay's own filters run on the captured surface where the relay stands, then its matrix turns the
                // result about the relay's own icon box, then its colour (stock 516.1689, stock-parity-world
                // *-relay-turn, *-relay-outline). Filters this cannot run are left out rather than the relay.
                // The pivot is the centre of the producer's own icon, not of the relay's tile or of all that was
                // captured (render-target-relays relay-big-turn, relay-ov-turn).
                const auto& producer = local.groups[index];
                const QRect box = producer.icon.isNull() ? QRect(quad.position,quad.size.isEmpty() ? producer.bounds.size() : quad.size)
                                                         : producer.icon.translated(quad.position-producer.origin);
                QRect bounds = LocalTargets::placed(quad,producer);
                const auto* effects = quad.planeEffects.get();
                const bool filtered = effects && effects->supported && !effects->filters.empty() && backend.floatSurfaces &&
                    std::none_of(effects->filters.begin(),effects->filters.end(),[](const auto& f) {
                        return f.kind == PlaneFilter::AlphaMask; });
                if (filtered) {
                    MapSprite capture; capture.position = bounds.topLeft(); capture.size = bounds.size();
                    auto surface = *effects; surface.matrix.reset();
                    const auto [result,filteredQuad] =
                        applyEffects(addPass({{generated(std::move(capture)),item.input}},bounds),&surface,bounds,bounds);
                    item.input = result; bounds = QRect(filteredQuad.position,filteredQuad.size);
                }
                quad.position = bounds.topLeft(); quad.size = bounds.size();
                const auto turned = effects && effects->matrix ? StaticAffine::make(*effects->matrix,box.size()) : std::nullopt;
                if (effects && effects->matrix && !turned) item.input = -2;
                else if (turned) {
                    if (quad.tint != std::array<float,4>{1,1,1,1} || quad.color != identityColor) {
                        auto material = quad; material.blend = 0;
                        item.input = addPass({{generated(std::move(material)),item.input}},bounds,true);
                        quad.tint = {1,1,1,1}; quad.color = identityColor;
                    }
                    const QPointF shift = bounds.topLeft() - box.topLeft();
                    StaticAffine affine;
                    affine.forward = QTransform::fromTranslate(shift.x(),shift.y()) * turned->forward *
                                     QTransform::fromTranslate(-shift.x(),-shift.y());
                    affine.inverse = affine.forward.inverted();
                    affine.bounds = affine.forward.mapRect(QRectF(QPointF(),bounds.size())).toAlignedRect();
                    quad.affine = affine; // quad.smooth: bilinear unless the relay has PIXEL_SCALE
                }
            }
        } else {
            // The plane's picture moves with the relay's screen_loc (stock 516.1689, plane-relay-world loc23, locpx).
            const QPoint offset = relayOffset(quad, source);
            quad.position = source.topLeft(); quad.size = source.size();
            const auto* effects = quad.planeEffects.get();
            if (planeTargetCounts[quad.renderSource] != 1 || quad.blend > 4 ||
                (effects && !effects->relayFilters())) item.input = -2;
            if (effects && !effects->filters.empty() && item.input >= 0) {
                if (dependency.isEmpty() || !effects->relayFilters() || !backend.floatSurfaces) item.input = -2;
                else {
                    const auto key = std::make_pair(item.input,effects->filters);
                    const auto cached = relayEffects.find(key);
                    if (cached != relayEffects.end()) item.input = cached->second;
                    else {
                        const int input = item.input;
                        // A master's chain can retain padded intermediates. Resolve
                        // its scene-space bounds, then clip to the consumer's capture
                        // domain; never stretch that padding across the viewport.
                        const QRect bounds = passes[input].bounds;
                        if (bounds != source) {
                            MapSprite capture; capture.position = bounds.topLeft(); capture.size = bounds.size();
                            item.input = addPass({{generated(std::move(capture)),input}});
                        }
                        auto surface = *effects; surface.matrix.reset();
                        item.input = applyEffects(item.input,&surface).first;
                        relayEffects[key] = item.input;
                    }
                }
            }
            if (item.input >= 0) {
                // Source-master chains also retain padded bounds, even when the
                // consumer has no filter. Preserve scene-space texel placement
                // rather than stretching a padded producer into the viewport.
                quad.position = passes[item.input].bounds.topLeft();
                quad.size = passes[item.input].bounds.size();
            }
            if (item.input >= 0 && effects && effects->matrix) {
                // Material is evaluated in capture space, before resampling.
                // Clip padded chain intermediates to the viewport capture first.
                if (passes[item.input].bounds != source) {
                    auto capture = quad; capture.tint = {1,1,1,1}; capture.color = identityColor; capture.blend = 0;
                    item.input = addPass({{generated(std::move(capture)),item.input}});
                    quad.position = source.topLeft(); quad.size = source.size();
                }
                if (quad.tint != std::array<float,4>{1,1,1,1} || quad.color != identityColor) {
                    auto material = quad; material.blend = 0;
                    item.input = addPass({{generated(std::move(material)),item.input}});
                    quad.tint = {1,1,1,1}; quad.color = identityColor;
                }
                quad.affine = effects->relayAffine(source.size(),!processedPlaneTargets[quad.renderSource]);
                // Stock 516.1689 samples the whole relayed plane, not only the part the view shows, and repeats its
                // edge texels past it (plane-relay-world grid-rm_2_0_0_0_2_0, grid-rm_2_0_-112_0_2_112). With PIXEL_SCALE
                // on the relay it takes the nearest texel, as a master does (grid-rnear-*; see the renderer).
                if (quad.affine) quad.affine->sampled = QRect(QPoint(),quad.size);
                quad.smooth = !quad.nearest;
            }
            quad.position += offset;
        }
        item.sprite = generated(std::move(quad));
    };
    // Resolution can append effect passes; never retain references into a vector
    // while adding to it. Generated operation items have no unresolved names.
    const size_t unresolvedPasses = passes.size();
    for (size_t p = 0; p < unresolvedPasses; ++p) for (size_t i = 0; i < passes[p].items.size(); ++i) {
        auto item = passes[p].items[i]; resolve(item); passes[p].items[i] = item;
    }
    for (auto& item : final) resolve(item);
    // Only displayed dependency closures need physical storage. An explicit DFS
    // stack preserves cycle-cut and sibling order without a depth-dependent C++ stack.
    requestedSurfaces = passes.size() + 1;
    struct Visit { int index; size_t next = 0; };
    std::vector<Visit> stack;
    auto visit = [&](int root) {
        if (root < 0 || passes[root].state == 2) return;
        passes[root].state = 1; stack.push_back({root});
        while (!stack.empty()) {
            auto& cursor = stack.back();
            auto& pass = passes[cursor.index];
            if (cursor.next == pass.items.size()) {
                pass.state = 2; schedule.push_back(cursor.index); stack.pop_back(); continue;
            }
            auto& item = pass.items[cursor.next++];
            if (item.input < 0) continue;
            auto& input = passes[item.input];
            if (input.state == 1) {
                item.cyclicInput = item.input; item.input = item.maskSource ? emptyMask : -2;
            } else if (input.state == 0) {
                input.state = 1; stack.push_back({item.input});
            }
        }
    };
    // Start at displayed outputs, preserving which relay back-edge is cut.
    if (emptyMask >= 0) visit(emptyMask); // deterministic zero source even for a cut mask back-edge
    for (const auto& item : final) visit(item.input);
    livePasses = schedule.size();
    std::vector<int> order(passes.size(),-1);
    std::vector<SurfaceLifetime> lifetimes;
    for (int i = 0; i < int(schedule.size()); ++i) {
        order[schedule[i]] = i;
        const auto& pass = passes[schedule[i]];
        lifetimes.push_back({pass.bounds.size(),i,pass.byteCapture ? 1 : 0});
    }
    for (int i = 0; i < int(schedule.size()); ++i)
        for (const auto& item : passes[schedule[i]].items) if (item.input >= 0)
            lifetimes[order[item.input]].lastRead = std::max(lifetimes[order[item.input]].lastRead,i);
    for (const auto& item : final) if (item.input >= 0) lifetimes[order[item.input]].lastRead = int(schedule.size());
    // Continue conservatively charging byte captures at float weight.
    storage = planSurfaces(source.size(),lifetimes,!backend.floatSurfaces ? 1 : backend.wideFloatSurfaces ? 4 : 2);
    if (!storage.supported) { rejection = storage.rejection; return; }
    metrics = {true,uint64_t(passes.size()),livePasses,storage.sizes.size()+1,storage.workPixels,storage.storagePixels};
    for (int i = 0; i < int(schedule.size()); ++i) passes[schedule[i]].surface = storage.slotForPass[i];
    supported = true;
}
bool CompositionPlan::maskedOut(const MapSprite& sprite, QPoint groupPixel, QPoint planePixel) const {
    auto hides = [](const std::vector<InputMask>& masks, QPoint pixel) {
        for (const auto& mask : masks) {
            const QPoint at = pixel - mask.topLeft;
            const int alpha = mask.image.rect().contains(at) ? mask.image.pixelColor(at).alpha() : 0;
            if (mask.inverse ? alpha == 255 : alpha == 0) return true;
        }
        return false;
    };
    if (const auto it = planeMasks.find(sprite.plane); it != planeMasks.end() && hides(it->second, planePixel)) return true;
    for (auto g = sprite.appearanceGroup.get(); g; g = g->parent.get())
        if (const auto it = groupMasks.find({sprite.groupOccurrence, g}); it != groupMasks.end() && hides(it->second, groupPixel))
            return true;
    return false;
}
void CompositionPlan::planInput() {
    // Coverage/coordinates deliberately do not come from composited alpha or
    // visual reachability: scalar-zero masters can still own original ink.
    std::set<QString> consumed, masked;
    std::set<int> fallbackRefused;
    for (const auto& s : sprites) {
        if (s.kind == MapSprite::Screen && !s.renderSource.isEmpty()) consumed.insert(s.renderSource);
        if (s.kind != MapSprite::Master &&
            (s.kind != MapSprite::Entity || !filteredInputPart(s) || s.footprint || s.localGroup || s.passMouse ||
             s.tint != std::array<float,4>{1,1,1,1} || s.color != identityColor)) fallbackRefused.insert(s.plane);
    }
    // A hidden plane that is only an alpha mask's source takes no clicks either (stock 516.1689, plane-click-world
    // ptalpha).
    for (const auto& [plane,index] : masters)
        if (const auto* effects = sprites[index].planeEffects.get())
            for (const auto& f : effects->filters)
                if (f.kind == PlaneFilter::AlphaMask && !f.source.isEmpty()) masked.insert(f.source);
    for (const auto& [plane,index] : masters) {
        const auto& s = sprites[index];
        const auto* effects = s.planeEffects.get();
        if (effects && effects->matrix && effects->valid(source.size()))
            transforms.emplace(plane,*StaticAffine::make(*effects->matrix,source.size()));
        const bool unclickable = masterDisposition.at(plane) == MasterDisposition::Refused ||
            s.mouseTransparent || s.mouseOpacity == 0 || (effects && !effects->valid(source.size()));
        // A hidden plane that nothing draws stays clickable where it stands (stock 516.1689, plane-relay-world norelay).
        const bool target = s.renderTarget.startsWith('*');
        const bool suppressed = unclickable || (target && (consumed.count(s.renderTarget) || masked.count(s.renderTarget)));
        if (!unclickable && target && consumed.count(s.renderTarget) && !masked.count(s.renderTarget)) captured.insert(plane);
        // Colour and alpha-mask filters leave the plane's ink clickable; a mask hides what it masks out.
        const bool inert = effects && std::all_of(effects->filters.begin(), effects->filters.end(), [](const PlaneFilter& f) {
            return f.kind == PlaneFilter::Color || f.kind == PlaneFilter::AlphaMask; });
        const bool filtered = effects && !effects->filters.empty() && !inert;
        // A filter of any kind leaves the plane's own ink clickable where it stood and adds none, under a matrix
        // too, which the pointer is mapped back through (stock 516.1689, master-transform-world clicks).
        if (filtered) outlines.insert(plane);
        if (suppressed || filtered) hidden.insert(plane);
        if (suppressed) hiddenTree.insert(plane);
        if (effects && !suppressed)
            if (auto masks = inputMasks(*effects, source); !masks.empty()) planeMasks[plane] = std::move(masks);
        if (s.masterFallback && s.mouseOpacity == 2 && !hiddenTree.count(plane) && !fallbackRefused.count(plane) &&
            effects && effects->valid(source.size()) && effects->filters.empty() && !effects->matrix) fallback = index;
    }
    bindings.resize(size_t(sprites.size()));
    auto planeBinding = [&](const QString& name) {
        const auto it = planeSources.names.find(name);
        return it == planeSources.names.end() ? -1 : it->second;
    };
    auto operation = [&](const QString& name) {
        const auto it = targets.find(name);
        return it == targets.end() ? -2 : it->second;
    };
    for (qsizetype i = 0; i < sprites.size(); ++i) {
        const auto& s = sprites[i]; auto& b = bindings[size_t(i)];
        // Groups (KEEP_TOGETHER or the atom's filters) keep their parts' own ink and root-relative coordinates: root
        // and overlay ink hit as the root, filter ink is not hit; a turned group maps the pointer back through its
        // matrix first (stock 516.1688, tests/data/click-world kt-root, kt-outline, outline, kt-turned,
        // filter-turned). Transformed vis members inverse-map like other parts (vis-turned, scaled-member).
        if (s.groupOccurrence!=0) {
            QTransform total; bool turned=false;
            for (auto g=s.appearanceGroup;g;g=g->parent)
                if (!g->transform.isIdentity()) { total=total*g->transform; turned=true; }
            if (turned) {
                bool invertible=false;
                const auto inverse=total.inverted(&invertible);
                if (invertible) { b.groupInverse=inverse; b.groupOrigin=s.groupOrigin; }
                else b.refused=true;
            }
        }
        if (s.planeEffects && s.planeEffects->matrix)
            b.mouseAffine = s.planeEffects->relayAffine(source.size(),false);
        b.localSource = local.find(s.renderSource);
        b.localName = local.names.count(s.renderSource) != 0;
        b.planeSource = planeBinding(s.renderSource);
        b.planeDependency = planeBinding(s.planeSourceDependency);
        b.localDependency = local.names.count(s.planeSourceDependency) != 0;
        if (!s.renderSource.isEmpty()) {
            b.sourceOperation = operation(s.renderSource);
            b.refused = b.sourceOperation < 0;
        }
        if (!s.planeSourceDependency.isEmpty()) {
            b.dependencyOperation = operation(s.planeSourceDependency);
            b.refused |= b.dependencyOperation < 0;
        }
        if (!s.renderTarget.isEmpty()) {
            const int group = local.find(s.renderTarget);
            b.localVisible = group >= 0 && local.groups[group].valid;
        }
    }
    auto resolved = [&](const Item& item) {
        if (item.original < 0) return;
        auto& binding = bindings[size_t(item.original)];
        binding.refused |= item.input == -2;
        if (!sprites[item.original].renderSource.isEmpty()) binding.sourceOperation = item.input;
    };
    for (const auto& pass : passes) for (const auto& item : pass.items) resolved(item);
    for (const auto& item : final) resolved(item);
}
}
