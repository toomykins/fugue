#include "shell/entitymap.h"
#include "shell/clientoffset.h"
#include "shell/ttmetrics.h"
#include <QPainter>
#include <deque>
#include "shell/drawkey.h"
#include "shell/animation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <functional>

namespace fugue::shell {
namespace {
// An animated filter's numeric parameters and colours move with the eased factor when both ends carry the same filter
// list (kinds and flags); any other change is not a tween (see the sampler's lerp and withFilters below). Out is
// a + (b - c) * f per parameter: a tween is (from, to, from), a parallel step adds its change (ap, step, base).
std::shared_ptr<const PlaneEffects> tweenFilters(const std::shared_ptr<const PlaneEffects>& a,
                                                 const std::shared_ptr<const PlaneEffects>& b,
                                                 const std::shared_ptr<const PlaneEffects>& c, double f) {
    if (!a || !b || !c || a->filters.size() != b->filters.size() || a->filters.size() != c->filters.size()) return nullptr;
    auto out = std::make_shared<PlaneEffects>(*a);
    for (size_t i = 0; i < a->filters.size(); ++i) {
        const auto &x = a->filters[i], &y = b->filters[i], &z = c->filters[i];
        if (x.kind != y.kind || x.kind != z.kind || x.flags != y.flags || x.flags != z.flags ||
            x.source != y.source || x.material != y.material || y.source != z.source || y.material != z.material)
            return nullptr;
        auto& o = out->filters[i];
        auto move = [f](double from, double to, double base) { return from + (to - base) * f; };
        o.size = move(x.size, y.size, z.size); o.grow = move(x.grow, y.grow, z.grow);
        o.x = move(x.x, y.x, z.x); o.y = move(x.y, y.y, z.y);
        o.offset = move(x.offset, y.offset, z.offset); o.radius = move(x.radius, y.radius, z.radius);
        o.repeat = move(x.repeat, y.repeat, z.repeat); o.falloff = move(x.falloff, y.falloff, z.falloff);
        for (int k = 0; k < 4; ++k) o.color[k] = std::clamp(float(move(x.color[k], y.color[k], z.color[k])), 0.f, 1.f);
    }
    // Eased overshoot can leave the decoded envelope; keep the step's own end instead of an invalid list.
    if (!out->valid({32,32})) return nullptr;
    return out;
}
// Filters changed on an animating atom after its animate() calls (the settled appearance's list differs in structure
// from the chain's last end) are drawn at once, with their own parameters and no fade, while the animation plays on,
// on a plane master and on an atom alike (stock 516.1689, tests/data/master-transform-world ax-outline1-lateadd,
// -lateclear, ax-pline-lateadd). A parallel step whose list differs from its base takes it the same way, which is
// inferred, not measured. The rest of the surface operation (a master's matrix) stays with `base`.
std::shared_ptr<const PlaneEffects> withFilters(const std::shared_ptr<const PlaneEffects>& base,
                                                std::shared_ptr<const PlaneEffects> tweened,
                                                const std::shared_ptr<const PlaneEffects>& end) {
    if (tweened) return tweened;
    if (!base || !end) return base;
    auto out = std::make_shared<PlaneEffects>(*base);
    out->filters = end->filters;
    out->ignoredFilters = end->ignoredFilters;
    return out;
}
// The model's cell tables are ordered by (x,y,z), while the turf pass visits
// (y,x). Keep one forward cursor per column for this preparation only. Sparse
// columns / many other Z levels fall back to a tree seek after four steps:
// neither the cumulative world cache nor an unrelated level is scanned. No
// model iterators survive prepare(), so arrivals, erasure and relinking need
// no revision protocol and cannot leave dangling cached references.
template<class Table> class TurfColumnLookup {
public:
    TurfColumnLookup(const Table& table, int width) : table_(table), columns_(size_t(width)) {}
    const typename Table::mapped_type* find(int column, const typename Table::key_type& key) {
        auto& cursor = columns_[size_t(column)];
        if (!cursor.ready) { cursor.it = table_.lower_bound(key); cursor.ready = true; }
        else {
            int steps = 0;
            while (cursor.it != table_.end() && cursor.it->first < key) {
                if (steps++ == 4) { cursor.it = table_.lower_bound(key); break; }
                ++cursor.it;
            }
        }
        return cursor.it != table_.end() && cursor.it->first == key ? &cursor.it->second : nullptr;
    }
private:
    struct Cursor { typename Table::const_iterator it; bool ready = false; };
    const Table& table_;
    std::vector<Cursor> columns_;
};
}
const EntityMap::Appearance& EntityMap::AppearanceLookup::operator()(uint32_t id) {
    auto& cached = cache[id];
    if (cached.checked == epoch) return cached;
    cached.checked = epoch;
    const auto found = source.find(id);
    if (found == source.end()) {
        if (cached.present || !cached.revision) {
            cached.present = cached.valid = false;
            cached.revision = ++revision;
        }
        return cached;
    }
    const auto& ap = found->second;
    bool decode = !cached.present;
    if (decode || cached.source != ap.fields) {
        // The icon, its state and the layer are read from the cache entry, not decoded: a change of those alone
        // moves the revision without a decode.
        cached.source.iconResID = ap.fields.iconResID;
        cached.source.iconState = ap.fields.iconState;
        cached.source.layerBits = ap.fields.layerBits;
        decode = decode || cached.source != ap.fields;
        cached.revision = ++revision;
    }
    cached.present = true;
    cached.icon = ap.fields.iconResID; cached.state = ap.fields.iconState; cached.layerBits = ap.fields.layerBits;
    if (decode) {
        if (stats) ++stats->appearanceDecodes;
        cached.source = ap.fields;
        auto decoded = decodeEntityAppearance(ap.fields, tile, std::move(static_cast<EntityAppearanceFields&>(cached)));
        cached.valid = decoded.valid;
        static_cast<EntityAppearanceFields&>(cached) = std::move(decoded.fields);
    }
    return cached;
}

bool EntityMap::beginAttempt(const model::World& w, const TurfMap::Frame& terrain,
                             const TurfMap::Read& read, int64_t nowNs) {
    const auto& g = terrain.geometry;
    if (trace_) {
        auto& d = trace_->map; d = {}; d.viewZ = g.z; d.seeInvisible = w.seeInvisible;
        d.haveEye=w.haveEye; d.haveSight=w.haveSight;
        d.haveSeeInDark=w.haveSeeInDark; d.seeInDark=w.seeInDark;
        d.haveNarrowSight=w.haveNarrowSight; d.haveWideSight=w.haveWideSight;
        d.narrowSight=w.narrowSight; d.wideSight=w.wideSight;
        d.haveSelf=w.haveSelf; d.selfRef=w.selfRef;
        d.source=terrain.source; d.terrainRegion=g.region;
        int sx=0,sy=0; model::eyeScrollOffset(w,nowNs,sx,sy); d.scroll={sx,sy};
        if (g.visibility) {
            d.fieldStatus=visibilityName(g.visibility->status);
            d.visibilityEye=g.visibility->eye; d.visibilityCells=g.visibility->cells;
            d.sight=g.visibility->sight; d.unknownCells=g.visibility->unknownCells;
            d.deferredCells=g.visibility->deferredCells;
            d.visibilityReused=tracedVisibility_.lock()==g.visibility;
            tracedVisibility_=g.visibility;
        }
        const auto it = w.atoms.find(trace_->key);
        d.placement = TraceReason::AbsentModel;
        if (it != w.atoms.end()) {
            const auto& a = it->second;
            d.present = true; d.freed = w.resync.freed.count(trace_->key) != 0;
            d.positioned = a.hasPos; d.x = a.x; d.y = a.y; d.z = a.z;
            d.appearance = a.appid; d.lifetime = a.renderIdentity;
            if (a.hasPos && a.z==g.z && g.visibility) d.anchorIllumination=visibilityName(g.visibility->illumination({a.x-1,a.y-1}));
            if (a.hasPos && a.z==g.z && g.visibility && !g.visibility->opacity.empty() && g.visibility->cells.contains(a.x-1,a.y-1)) {
                const auto& f=*g.visibility;
                d.anchorOpacity=f.opacity[size_t(a.y-1-f.cells.y())*f.cells.width()+a.x-1-f.cells.x()];
            }
            d.haveWireKey = a.haveKey; d.wireKey = a.key;
            d.placement = d.freed ? TraceReason::Freed : TraceReason::NotVisited;
        }
    }
    if (terrain.image.isNull() || terrain.source.isEmpty() || g.tile <= 0) {
        if (trace_ && trace_->map.present && !trace_->map.freed) trace_->map.placement = TraceReason::NoTerrain;
        order_.clear(); instances_.clear(); visible_.clear(); output_.clear();
        return false;
    }
    ++epoch_;
    if (tile_ != g.tile || seeInvisible_ != w.seeInvisible || turn_ != g.dir || fonts_ != resourceFontGeneration()) {
        templates_.clear(); tile_ = g.tile; seeInvisible_ = w.seeInvisible; turn_ = g.dir; fonts_ = resourceFontGeneration();
    }
    icons_.begin(w.resources, read);
    maptext_.begin();
    return true;
}

bool EntityMap::AnimationSampler::unsettled(const model::Atom::Chain& chain) {
    if (chain.rejected) return true;
    if (chain.steps.empty()) return false;
    if (!animation::admit(chain, animationWork)) return true;
    bool value = false;
    uint32_t lastTo = 0;
    return animation::composeChain(
        chain, nowNs, [](uint32_t) { return false; }, [](bool, bool, double, double) { return false; },
        [](bool&, bool, bool, bool, double, double) {}, value, lastTo);
}

// Sample values, never shared asset playback clocks. The temporary root is
// expanded through the ordinary asset path; QImages retain their resident keys.
bool EntityMap::AnimationSampler::sample(const model::Atom::Chain& chain, uint32_t settled, Appearance& out, bool& composing, bool hudMaster,
                                       bool attachment) {
    if (!animation::admit(chain, animationWork)) return false;
    const auto liveMaster = hudMaster || attachment ? appearance(settled) : Appearance{};
    // An animated atom may be a render target or a relay as long as the animation does not change which (stock
    // 516.1689, stock-parity-world animated-render-target: tgstation's eyelids blink by animating a hidden target's alpha).
    const QString liveSource = appearance(settled).renderSource, liveTarget = appearance(settled).renderTarget;
    bool valid = true;
    auto get = [&](uint32_t id) {
        Appearance ap = appearance(id);
        valid &= ap.present && ap.valid;
        if (hudMaster) {
            // Material, matrix and filter animation on a stable HUD master tree: the plane is drawn with the filters
            // at their tweened parameters, then turned by the tweened matrix, as a settled master with those values is
            // (stock 516.1689, tests/data/master-transform-world ax-*). Endpoints must not silently introduce
            // placement/group/ownership transitions.
            valid &= ap.master && ap.colorValid && ap.color == liveMaster.color && !ap.visFlags && ap.planeEffects &&
                ap.planeEffects->supported &&
                ap.renderSource.isEmpty() && ap.renderTarget.isEmpty() &&
                ap.screenLoc == liveMaster.screenLoc && ap.hasPlane == liveMaster.hasPlane && ap.plane == liveMaster.plane &&
                ap.offset == liveMaster.offset && ap.layerBits == liveMaster.layerBits && ap.blend == liveMaster.blend &&
                ap.icon == liveMaster.icon && ap.state == liveMaster.state && ap.dir == liveMaster.dir &&
                ap.keepTogether == liveMaster.keepTogether && ap.keepApart == liveMaster.keepApart &&
                ap.resetColor == liveMaster.resetColor && ap.resetAlpha == liveMaster.resetAlpha &&
                ap.resetTransform == liveMaster.resetTransform && ap.invisibility == liveMaster.invisibility &&
                ap.mouseOpacity == liveMaster.mouseOpacity && ap.underlays == liveMaster.underlays && ap.overlays == liveMaster.overlays &&
                ap.maptext.isEmpty();
        // vis_flags are read from the sampled member like a settled one (stock 516.1688, vis-world set e anim-kid-*).
        } else if (!(attachment ? ap.matrixValid : ap.transformValid) ||
                   (attachment && (ap.color != liveMaster.color || ap.keepTogether != liveMaster.keepTogether ||
                    ap.keepApart != liveMaster.keepApart || ap.resetTransform != liveMaster.resetTransform ||
                    ap.screenLoc != liveMaster.screenLoc || ap.hasPlane != liveMaster.hasPlane || ap.plane != liveMaster.plane)) ||
                   ap.master || (!attachment && ap.keepTogether) || (ap.keepApart && (ap.hasTransform || !ap.underlays.empty() || !ap.overlays.empty())) ||
                   ap.renderSource != liveSource || ap.renderTarget != liveTarget)
            valid = false;
        return ap;
    };
    // ANIMATION_LINEAR_TRANSFORM (0x2) applies to the step that carries it (stock 516.1688, transform-tween-world).
    auto lerp = [](const Appearance& from, const Appearance& to, double raw, double f, uint32_t flags) {
        Appearance ap = raw >= .5 ? to : from;
        auto mix = [f](int a, int b) { return int(std::lround(a + (b - a) * f)); };
        // Stock 516.1688 truncates the eased change toward zero, as for client offsets (tests/data/parallel-world).
        auto offset = [f](int a, int b) { return a + int(float(b - a) * float(f)); };
        ap.offset = {offset(from.offset.x(), to.offset.x()), offset(from.offset.y(), to.offset.y())};
        for (int i = 0; i < 4; ++i) // clamped: BACK/ELASTIC easing overshoots
            ap.tint[i] = float(std::clamp(mix(int(std::lround(from.tint[i] * 255)), int(std::lround(to.tint[i] * 255))), 0, 255)) / 255;
        ap.matrix = animation::interpolate(from.matrix, to.matrix, f, (flags & 0x2u) != 0);
        // Ends whose lists differ in structure switch at the midpoint with the rest of the appearance. DreamDaemon 516.1689
        // never sends such a step for a DM animate(): both ends carry the atom's filters when animate() is called, and
        // filters is not an animated var (master-transform-world ax-outline1-toshadow sends no animation at all), so
        // this rule is unmeasured.
        if (from.atomFilters != to.atomFilters)
            if (auto filters = tweenFilters(from.atomFilters, to.atomFilters, from.atomFilters, f)) ap.atomFilters = filters;
        if (from.planeEffects && to.planeEffects && from.planeEffects->filters != to.planeEffects->filters)
            if (auto filters = tweenFilters(from.planeEffects, to.planeEffects, from.planeEffects, f)) ap.planeEffects = filters;
        // A colour-matrix tween blends every element by the eased factor, a plain colour counting as its diagonal
        // matrix (stock 516.1688, tests/data/color-tween-world).
        if (from.color != identityColor || to.color != identityColor) {
            const auto a = tintColor(from.color, from.tint), b = tintColor(to.color, to.tint);
            for (size_t i = 0; i < a.size(); ++i) ap.color[i] = float(a[i] + (double(b[i]) - a[i]) * f);
            ap.tint = {1, 1, 1, 1};
        }
        return ap;
    };
    auto add = [&](Appearance& ap, const Appearance& base, const Appearance& from, const Appearance& to, double raw, double f,
                   uint32_t flags) {
        const auto step = lerp(from, to, raw, f, flags);
        ap.offset += step.offset - base.offset;
        // Colour adds the step's change relative to its base, in matrix space once any side has a matrix, so an end
        // step whose target equals the settled colour changes nothing.
        if (ap.color != identityColor || step.color != identityColor || base.color != identityColor) {
            auto out = tintColor(ap.color, ap.tint);
            const auto now = tintColor(step.color, step.tint), was = tintColor(base.color, base.tint);
            for (size_t i = 0; i < out.size(); ++i) out[i] += now[i] - was[i];
            ap.color = out; ap.tint = {1, 1, 1, 1};
        } else for (int i = 0; i < 4; ++i) ap.tint[i] = std::clamp(ap.tint[i] + step.tint[i] - base.tint[i], 0.f, 1.f);
        // step * inverse(base) * out: the parallel step's change relative to its base.
        if (const auto undo = animation::inverse(base.matrix))
            ap.matrix = animation::multiply(step.matrix, animation::multiply(*undo, ap.matrix));
        else valid = false;
        if (from.icon != to.icon || from.state != to.state || base.icon != to.icon || base.state != to.state) {
            ap.icon = to.icon;
            ap.state = to.state;
        }
        if (from.blend != to.blend || base.blend != to.blend) ap.blend = to.blend;
        if (step.atomFilters != base.atomFilters) {
            auto filters = tweenFilters(ap.atomFilters, step.atomFilters, base.atomFilters, 1);
            ap.atomFilters = filters ? filters : step.atomFilters;
        }
        if (ap.planeEffects && step.planeEffects && base.planeEffects && step.planeEffects->filters != base.planeEffects->filters)
            ap.planeEffects = withFilters(ap.planeEffects, tweenFilters(ap.planeEffects, step.planeEffects, base.planeEffects, 1),
                                          step.planeEffects);
    };
    uint32_t lastTo = 0;
    composing = animation::composeChain(chain, nowNs, get, lerp, add, out, lastTo);
    if (!composing) {
        out = appearance(settled);
        return out.present && out.valid; // completed chains use ordinary cached static capabilities
    }
    const auto live = get(settled);
    if (lastTo != settled) {
        const auto end = get(lastTo);
        add(out, end, end, live, 1, 1, 0);
    }
    out.underlays = live.underlays;
    out.overlays = live.overlays;
    if (attachment) out.layerBits = live.layerBits; // a layer set after animate() began (animated-render-target rk-blink-layer-late)
    out.hasTransform = out.matrix != std::array<double, 6>{1, 0, 0, 0, 1, 0};
    out.transformValid = StaticAffine::make(out.matrix, {tile_, tile_}).has_value();
    out.matrixValid = out.transformValid;
    // Same classification as decodeEntityAppearance: positive axis scale without
    // translation keeps the scaled-rectangle path; anything else is affine.
    const auto& m = out.matrix;
    out.affine = !out.transformValid || m[1] != 0 || m[2] != 0 || m[3] != 0 || m[5] != 0 || m[0] <= 0 || m[4] <= 0;
    out.scaleX = out.affine ? 1.f : float(m[0]);
    out.scaleY = out.affine ? 1.f : float(m[4]);
    // A KEEP_TOGETHER surface takes an animating matrix as it takes a settled one (decodeEntityAppearance; stock
    // 516.1689, tests/data/stock-parity-world animated-groups).
    const bool surface = attachment && out.affine && out.transformValid && out.keepTogether && out.renderSource.isEmpty() &&
                         out.renderTarget.isEmpty();
    // A master's matrix turns its plane: it travels in the plane effects, as decodePlaneEffects puts a settled one there,
    // and the master's own placement treats it as decodeEntityAppearance does. KEEP_TOGETHER on a master with nothing to
    // group changes nothing; with overlays and a matrix the template draws nothing, as for a settled one (stock 516.1689,
    // master-transform-world ax-group-spin90, ax-tree-group-spin90).
    const bool masterSurface = hudMaster && out.master && out.planeEffects &&
                               (!out.keepTogether || (out.underlays.empty() && out.overlays.empty()));
    if (masterSurface) {
        auto effects = std::make_shared<PlaneEffects>(*out.planeEffects);
        effects->matrix.reset();
        if (out.hasTransform) effects->matrix = out.matrix;
        effects->supported = effects->valid({32, 32});
        out.planeEffects = std::move(effects);
        if (out.affine && out.transformValid) { out.transformValid = false; out.transformEffects = true; }
    }
    if (surface) { out.transformValid = false; out.transformEffects = true; }
    else if (out.affine && (out.keepTogether || !out.renderSource.isEmpty() || !out.renderTarget.isEmpty()))
        out.transformValid = out.matrixValid = false;
    out.imageUnsupported = out.imageParentUnsupported;
    return valid && (!hudMaster || finiteColor(out.color)) && (out.transformValid || surface || (masterSurface && out.matrixValid)) &&
        std::abs(out.offset.x()) <= 1'000'000 && std::abs(out.offset.y()) <= 1'000'000;
}

std::shared_ptr<const PlaneEffects> EntityMap::TemplateBuilder::withMasks(std::shared_ptr<const PlaneEffects> effects,
                                                                         bool& complete) {
    if (!effects || std::none_of(effects->filters.begin(), effects->filters.end(),
                                 [](const PlaneFilter& f) { return f.icon != UINT32_MAX; })) return effects;
    auto loaded = std::make_shared<PlaneEffects>(*effects);
    for (auto& f : loaded->filters) {
        if (f.icon == UINT32_MAX) continue;
        const auto res = resource(f.icon);
        if (!res.ready) { complete = false; continue; }
        const QImage frame = icons_.icon(res.id).frame(0);
        if (!frame.isNull()) f.image = std::make_shared<const QImage>(frame.convertToFormat(QImage::Format_RGBA8888));
    }
    return loaded;
}

EntityMap::Resource EntityMap::TemplateBuilder::resource(uint32_t ordinal) {
    const auto result = icons_.lookup(ordinal);
    return Resource{ordinal, result.id, result.mapped, result.ready, result.revision, result.status};
}

std::shared_ptr<const QImage> EntityMap::FrameLookup::operator()(const FrameKey& key) {
    auto found = frames_.find(key);
    if (found != frames_.end()) {
        found->second.used = epoch_;
        return found->second.image;
    }
    const auto& icon = icons_.icon(std::get<0>(key));
    const uint64_t pixels = uint64_t(icon.cell.width()) * icon.cell.height();
    if (pixels > 64 * 1024 * 1024 - decodedPixels) return {};
    decodedPixels += pixels;
    if (stats) ++stats->frameDecodes;
    auto image = std::make_shared<const QImage>(icon.frame(std::get<2>(key)).convertToFormat(QImage::Format_RGBA8888));
    frameBytes_ += image->sizeInBytes();
    frames_.emplace(key, CachedFrame{image, epoch_});
    return image;
}

void EntityMap::TemplateCache::clear() {
    ordinary.clear(); self.clear(); flick.clear(); image.clear(); vis.clear();
}

void EntityMap::TemplateCache::prune(uint64_t epoch) {
    auto cold = [epoch](const auto& entry) { return entry.second.checked != epoch; };
    std::erase_if(ordinary, cold); std::erase_if(self, cold); std::erase_if(vis, cold);
    std::erase_if(image, cold); std::erase_if(flick, cold);
}

const EntityMap::Template& EntityMap::TemplateBuilder::get(const TemplateRequest& request) {
    const auto& [rootId, rootDir, source, visibility] = request;
    auto& cached = std::visit(
        [&](const auto& context) -> Template& {
            using T = std::decay_t<decltype(context)>;
            if constexpr (std::is_same_v<T, VisRequest>) return cache.vis[{rootId, rootDir, context.context, context.inherit}];
            else if constexpr (std::is_same_v<T, ImageRequest>) return cache.image[{rootId, context.parent, rootDir}];
            else if constexpr (std::is_same_v<T, FlickRequest>)
                return cache.flick[{rootId, rootDir, context.iconResIndex, context.iconState}];
            else return (visibility == VisibilityPolicy::SeeSelf ? cache.self : cache.ordinary)
                [(uint64_t(rootId) << 32) | uint32_t(rootDir)];
        },
        source);
    if (cached.checked == epoch_) return cached;
    bool changed = !cached.revision;
    for (const auto& [id, revision] : cached.dependencies)
        if (appearance(id).revision != revision) changed = true;
    // Retry missing resources even on hits and for offscreen instances. A successful
    // retry changes ready, forcing a rebuild before any bounds-based culling.
    for (auto& dependency : cached.resources) {
        const auto current = resource(dependency.ordinal);
        if (current != dependency) changed = true;
        dependency.status = current.status; // evidence only; equality/readiness semantics are unchanged
    }
    cached.checked = epoch_;
    if (!changed) return cached;
    cached = construct(request, nullptr);
    return cached;
}

EntityMap::Template EntityMap::TemplateBuilder::sample(const SampleRequest& request, const Appearance& sampled) {
    const auto& [rootId, rootDir, inheritance, visibility] = request;
    return std::visit([&](const auto& context) {
        return construct({rootId, rootDir, context, visibility}, &sampled);
    }, inheritance);
}

EntityMap::Template EntityMap::TemplateBuilder::construct(const TemplateRequest& request, const Appearance* sampled) {
    const auto& [rootId, rootDir, source, visibility] = request;
    const auto& rootFields = sampled ? *sampled : appearance(rootId);
    if (std::holds_alternative<VisRequest>(source) ||
        (std::holds_alternative<OrdinaryRequest>(source) && !rootFields.master &&
         ((!rootFields.renderTarget.isEmpty() && rootFields.renderSource.isEmpty()) ||
          (rootFields.keepTogether && rootFields.renderSource.isEmpty() && rootFields.renderTarget.isEmpty()))))
        return constructTree(request,sampled);
    // A settled ordinary atom's own filters run on a surface of the whole atom. Trees the group path refuses (maptext,
    // a transformed root with overlays...) are drawn unfiltered below, as before.
    // Nested KEEP_TOGETHER groups take the same path: the flat tree has no group surfaces.
    auto filtersIn = [&](auto&& self, const Appearance& ap, int depth) -> bool {
        // A childless KEEP_TOGETHER | KEEP_APART overlay (tgstation's emissive blocker, on nearly every atom) groups
        // nothing and leaves nothing: the flat tree draws it the same, unless the atom is turned (the flat tree has no
        // KEEP_TOGETHER inside a turned one; tgstation's space dust, stock 516.1689).
        if (ap.atomFilters || (ap.keepTogether && !(depth && !rootFields.hasTransform && ap.keepApart && ap.underlays.empty() && ap.overlays.empty() &&
                                                    ap.maptext.isEmpty())))
            return true;
        // A turned atom's relay overlay: the flat tree refuses it, the group path draws it (stock 516.1689).
        if (depth && rootFields.hasTransform && !ap.renderSource.isEmpty()) return true;
        // Maptext that is turned, or over its own icon translucent, blended or coloured by a matrix, shares a surface with
        // that icon (stock 516.1689).
        if (!ap.maptext.isEmpty() &&
            (rootFields.hasTransform || ap.hasTransform ||
             (((ap.icon != UINT32_MAX && ap.icon != 0xffffu) || !ap.state.empty()) &&
              (rootFields.tint[3] != 1 || ap.tint[3] != 1 || rootFields.blend > 1 || ap.blend > 1 ||
               rootFields.color != identityColor || ap.color != identityColor))))
            return true;
        if (depth >= 4) return false;
        for (const auto& children : {std::cref(ap.underlays), std::cref(ap.overlays)})
            for (uint32_t child : children.get()) if (self(self, appearance(child), depth + 1)) return true;
        return false;
    };
    // Animating and flicking atoms keep their filters (stock 516.1688, filter-reach-world set b).
    if ((std::holds_alternative<OrdinaryRequest>(source) || std::holds_alternative<FlickRequest>(source)) && !rootFields.master &&
        rootFields.renderSource.isEmpty() && rootFields.renderTarget.isEmpty() && filtersIn(filtersIn, rootFields, 0)) {
        auto filtered = constructTree(request,sampled);
        if (filtered.diagnosticStatus != TraceReason::TemplateRefused) return filtered;
    }
    const auto* flick = std::get_if<FlickRequest>(&source);
    const auto* image = std::get_if<ImageRequest>(&source);
    const auto* vis = std::get_if<VisRequest>(&source);
    const uint32_t* imageParent = image ? &image->parent : nullptr;
    const VisContext* visContext = vis ? &vis->context : nullptr;
    if (stats) ++stats->templateBuilds;
    Template next;
    next.checked = epoch_;
    next.revision = ++revision_;
    std::vector<uint32_t> path;
    bool deferred = false, unsupported = false;
    const auto& treeRoot = sampled ? *sampled : appearance(rootId);
    const bool localCapture = !imageParent && !visContext && !treeRoot.master && !treeRoot.renderTarget.isEmpty();
    // Surface-backed HUD appearances reuse ordinary flat child expansion.
    // Their extended semantics bind only to plane targets at submission; the
    // bounded local-capture/forwarding path is intentionally unchanged.
    // A relay on the map takes the same path once it is turned or filtered; a plain one is a flat part.
    const bool sourceTree = !imageParent && !visContext && !localCapture && !flick && !treeRoot.master &&
                            !treeRoot.renderSource.isEmpty() &&
                            (!treeRoot.screenLoc.isEmpty() || treeRoot.hasTransform || treeRoot.surfaceFilters);
    const bool extendedSource =
        sourceTree && (treeRoot.hasTransform || treeRoot.animationEffects || !treeRoot.identityFilters || treeRoot.keepTogether ||
                       treeRoot.keepApart || !treeRoot.maptext.isEmpty() || !treeRoot.underlays.empty() || !treeRoot.overlays.empty());
    if (extendedSource) next.planeSourceDependency = treeRoot.renderSource;
    const bool replacedSourceTree = extendedSource && treeRoot.keepTogether && (!treeRoot.underlays.empty() || !treeRoot.overlays.empty());
    const bool masterTree =
        treeRoot.master && (treeRoot.keepTogether || treeRoot.keepApart || !treeRoot.underlays.empty() || !treeRoot.overlays.empty() ||
                            (treeRoot.planeEffects && treeRoot.planeEffects->boundedTreeFilters()));
    // A childless source master captures the same admitted plane operations;
    // the target name does not make it a nested/local group. Do not extend
    // target ownership to master-owned trees or nested captures here.
    const bool captureLeafMaster = treeRoot.master && !treeRoot.renderTarget.isEmpty() && treeRoot.underlays.empty() &&
                                   treeRoot.overlays.empty() && !treeRoot.keepApart;
    const int masterRootPlane = treeRoot.hasPlane ? resolvePlane(treeRoot.plane) : 0;
    const auto rootMatrix = treeRoot.matrix;
    const bool rootTree = !sourceTree && !treeRoot.master && !imageParent && !visContext && treeRoot.hasTransform &&
                          rootMatrix != std::array<double, 6>{1, 0, 0, 0, 1, 0} &&
                          (!treeRoot.underlays.empty() || !treeRoot.overlays.empty());
    // Any finite invertible matrix (vg's 0.7 shrink, mid-animation rotations).
    // Each part keeps its own center/bottom pivot and additive offsets, as
    // verified with unequal 16/32/48px icons; quarter turns and half size stay exact.
    const bool treeMatrixValid = treeRoot.transformValid;
    if (rootTree) next.affine = true;
    TraceTree details;
    std::pair<size_t, size_t> rootOwnParts{0, 0}; // the root's own icon and text, between its underlays and overlays
    std::vector<animation::Matrix> xforms{{1, 0, 0, 0, 1, 0}}; // composed transform per expansion depth
    auto reject = [&](TraceReason reason, uint32_t id) {
        if (next.first.reason == TraceReason::Unobserved) {
            next.first = {reason, id, path.empty() ? 0 : path.back() != id ? path.back() : path.size() > 1 ? path[path.size() - 2] : 0};
            if (trace_) {
                details.firstPath = path;
                if (path.empty() || path.back() != id) details.firstPath.push_back(id);
                if (details.firstPath.size() > 32) {
                    details.firstPath.resize(32);
                    details.truncated = true;
                }
            }
        }
    };
    auto expand = [&](auto&& self, uint32_t id, QPoint at, uint32_t inheritedIcon, int inheritedDir, int plane, float layer,
                      std::array<float, 4> tint, bool root, bool grouped, bool textEffects = false,
                      ColorMatrix inheritedColor = identityColor, int inheritedBlend = 0) -> void {
        if (next.visits >= maxSceneVisits || path.size() >= 32 || (masterTree && (path.size() >= 3 || next.visits >= 4096)) ||
            std::find(path.begin(), path.end(), id) != path.end()) {
            reject(TraceReason::TraversalLimit, id);
            if (visContext) next.complete = false;
            if (rootTree || masterTree || extendedSource) unsupported = true;
            return;
        }
        if (visContext && !visTemplateWork) {
            reject(TraceReason::SceneWork, id);
            deferred = true;
            return;
        }
        if (visContext) --visTemplateWork;
        ++next.visits;
        if (stats) ++stats->visits;
        const auto& ap = root && sampled ? *sampled : appearance(id);
        next.dependencies.emplace_back(id, ap.revision);
        const size_t nodeIndex = details.nodes.size();
        if (trace_) {
            if (nodeIndex < 16) {
                TraceNode n;
                n.path = path;
                n.path.push_back(id);
                if (ap.present && ap.valid) {
                    n.invisibility = ap.invisibility;
                    n.transformed = ap.hasTransform;
                    n.transformValid = ap.transformValid;
                }
                details.nodes.push_back(std::move(n));
            } else details.truncated = true;
        }
        if (!ap.present || !ap.valid) reject(!ap.present ? TraceReason::MissingAppearance : TraceReason::InvalidAppearance, id);
        if (rootTree) {
            // Descendant transforms compose with the root's (RESET_TRANSFORM
            // drops it); RESET_COLOR/RESET_ALPHA only change inherited RGBA;
            // maptext and flick frames take the composed matrix too. Group,
            // surface and vis effects inside a transformed tree stay refused.
            // Any icon size: a 64 px world turns trees the same way (stock 516.1688, wide-icon-transform-world).
            if (!ap.present || !ap.valid || !treeMatrixValid || (!root && ap.hasTransform && !ap.transformValid)) {
                reject(TraceReason::TransformTree, id);
                unsupported = true;
            }
            // KEEP_TOGETHER never reaches this flat tree, so KEEP_APART has no group to leave and changes nothing
            // (stock 516.1688, tests/data/transform-group-world ka-turn).
            // vis_flags mean nothing outside vis_contents, and a turned screen object is a turned atom (stock 516.1689).
            if (ap.imageParentUnsupported || (!root && !ap.screenLoc.isEmpty()) || ap.imageOverride) {
                reject(TraceReason::EffectCombination, id);
                unsupported = true;
            }
        }
        if (masterTree) {
            // Material filters currently have only a neutral, ungrouped leaf
            // contract. Do not silently drop a newly decoded mask on a tree.
            if (ap.planeEffects && std::any_of(ap.planeEffects->filters.begin(), ap.planeEffects->filters.end(), [](const auto& f) {
                return f.kind == PlaneFilter::AlphaMask || f.kind == PlaneFilter::Color;
            })) unsupported = true;
            // Stock-bounded HUD master selection/grouping. This is structural:
            // do not flatten separate planes or move surface operations into leaves.
            // Matrices (stock 516.1688, tests/data/master-transform-world): the root master's turns its whole plane
            // about the view centre, overlays included; a leaf's turns that leaf. KEEP_TOGETHER over overlays with a
            // master matrix draws nothing of the plane in stock, settled or animating (refused here, which also draws
            // nothing; 516.1689 ax-tree-group-turn90, ax-tree-group-spin90). Without overlays or underlays KEEP_TOGETHER
            // groups nothing and the matrix turns the plane, settled or animating (ax-group-turn90, ax-group-spin90).
            // Nested masters are unprobed.
            const bool matrixAdmitted = !ap.master ||
                (root && (!treeRoot.keepTogether || (treeRoot.underlays.empty() && treeRoot.overlays.empty())));
            if (!ap.present || !ap.valid || tile_ != 32 || (ap.hasTransform && !matrixAdmitted) || ap.visFlags ||
                (ap.animationEffects && !ap.master) ||
                (ap.keepTogether && ap.keepApart) || (!ap.master && ap.keepTogether) ||
                (ap.keepApart && (root || !treeRoot.keepTogether)) || ap.resetTransform ||
                !ap.colorValid || ap.blend > 4 || !ap.renderSource.isEmpty() ||
                (!ap.renderTarget.isEmpty() && !(root && captureLeafMaster)) || !ap.maptext.isEmpty() || ap.invisibility || imageParent ||
                visContext || flick ||
                (root ? !treeRoot.screenPlacement || treeRoot.mouseOpacity > 1
                      : !ap.screenLoc.isEmpty()) ||
                (ap.master && (!ap.planeEffects || !ap.planeEffects->supported || !ap.offset.isNull() || ap.mouseOpacity > 1 ||
                               (!root && std::bit_cast<float>(ap.layerBits) != -1))) ||
                (!ap.master && (path.size() != 1 || !ap.underlays.empty() || !ap.overlays.empty() ||
                                (ap.hasPlane && ap.plane != -32767 && (ap.plane < -10000 || ap.plane > 10000)) ||
                                std::abs(double(ap.offset.x())) > 8192 || std::abs(double(ap.offset.y())) > 8192)))
                unsupported = true;
        }
        if (localCapture &&
            (ap.master || ap.hasTransform || ap.animationEffects || (!root && ap.keepTogether) || ap.keepApart || ap.tint[3] != 1 ||
             ap.color != identityColor || ap.blend > 1 ||
             (root && ap.keepTogether && (ap.tint != std::array<float, 4>{1, 1, 1, 1} || ap.color != identityColor || ap.blend > 1)) ||
             !ap.maptext.isEmpty() || imageParent || visContext || flick ||
             (!root && (!ap.renderTarget.isEmpty() || !ap.renderSource.isEmpty()))))
            unsupported = true;
        // Whether the relay's matrix and filters can be drawn depends on what the name is, a plane's surface or an
        // atom's render target, and only the composition plan knows that.
        if (sourceTree && root && (!ap.planeEffects || ap.blend > 4)) unsupported = true;
        if (extendedSource) {
            // Stock 516.1688 replaces this bounded KEEP_TOGETHER tree with
            // the plane source, including ordinary underlays/overlays. This
            // is NOT an inclusive capture. Validate each child/dependency
            // before suppressing it; ungrouped ordering remains unchanged.
            const bool children = !ap.underlays.empty() || !ap.overlays.empty();
            const bool filters = root ? ap.planeEffects != nullptr : ap.identityFilters;
            const bool material = ap.blend <= 4;
            if (!ap.present || !ap.valid || ap.master || (!root && ap.hasTransform) || !filters || !material ||
                (ap.keepTogether && ap.keepApart) ||
                (root ? (!ap.offset.isNull() && !ap.screenLoc.isEmpty()) || (ap.visFlags & ~0x20u) != 0
                      : path.size() != 1 || children || ap.visFlags || ap.keepTogether || ap.keepApart || !ap.renderSource.isEmpty() ||
                            !ap.renderTarget.isEmpty() || !ap.screenLoc.isEmpty() || !ap.maptext.isEmpty() ||
                            (ap.hasPlane && ap.plane != -32767)) ||
                ap.resetColor || ap.resetAlpha || ap.resetTransform ||
                // Inherited root matrices/blends are established for ordinary
                // neutral-matrix/default-blend children. Explicit child material
                // composition needs a separate inheritance contract.
                (!root && !replacedSourceTree &&
                 ((ap.color != identityColor && treeRoot.color != identityColor) || (ap.blend > 1 && treeRoot.blend > 1))))
                unsupported = true;
            // No child pixels, bounds, playback or hit rectangles survive
            // replacement. Suppressed assets need no decode/upload, but an
            // absent or changed appearance still invalidates this template.
            if (replacedSourceTree && !root) return;
        } else if (!ap.renderSource.isEmpty() && !ap.master &&
                   // An overlay's relay may be turned or filtered: the composition plan runs both (overlay-relays ov-relay-turn).
                   ((root && (ap.hasTransform || ap.animationEffects)) || ap.keepTogether || ap.keepApart ||
                    !ap.maptext.isEmpty() || !ap.underlays.empty() || !ap.overlays.empty()))
            unsupported = true;
        if (ap.affine && !rootTree && !ap.master && !sourceTree && ap.renderSource.isEmpty()) {
            next.affine = true;
            // Transformed children of untransformed trees compose through the
            // expansion's matrix stack. Image-owned and vis-copy matrices are
            // separate inheritance contracts and stay refused.
            if (!ap.transformValid || visContext) {
                reject(ap.transformEffects  ? TraceReason::EffectCombination
                       : !ap.transformValid ? TraceReason::InvalidTransform
                                            : TraceReason::TransformTree,
                       id);
                unsupported = true;
            }
        }
        // KEEP_TOGETHER inside an image's own tree has no surface here: its parts draw flat, each with the tree's
        // alpha, rather than not at all (stock-parity-world sweep image-kt-own).
        if (imageParent && (ap.master || !ap.renderTarget.isEmpty() || (!root && ap.imageOverride) ||
                            (ap.keepApart && !root))) {
            reject(TraceReason::EffectCombination, id);
            unsupported = true;
        }
        // Independent vis leaves use their own finite material. Matrix/reset
        // inheritance through ordinary trees or nested vis holders is not yet
        // established; do not turn those forests into partially colored copies.
        const bool visMaterial = ap.resetColor || ap.resetAlpha || ap.color != identityColor;
        if (visContext && (ap.imageUnsupported || ap.keepApart || !ap.maptext.isEmpty() || !ap.screenLoc.isEmpty() || ap.imageOverride ||
                           !ap.colorValid || ap.blend > 1 ||
                           (visMaterial && (!root || !ap.underlays.empty() || !ap.overlays.empty())))) {
            reject(TraceReason::EffectCombination, id);
            unsupported = true;
        }
        if (visContext && (!ap.present || !ap.valid)) next.complete = false;
        // Stock ignores image-root invisibility and SEE_SELF tree invisibility.
        // The latter has its own cache namespace, never a global threshold.
        // Invisible image children remain unverified; omit that tree.
        if (imageParent && !root && ap.invisibility) unsupported = true;
        if (!ap.present || !ap.valid || (!(visibility == VisibilityPolicy::SeeSelf || (root && imageParent)) && ap.invisibility > seeInvisible_)) {
            reject(!ap.present ? TraceReason::MissingAppearance : !ap.valid ? TraceReason::InvalidAppearance : TraceReason::Invisible, id);
            return;
        }
        path.push_back(id);
        // Transform inheritance: parent * own, about each part's own pivot.
        // RESET_TRANSFORM drops the inherited part (stock overlay semantics).
        static constexpr animation::Matrix identityMatrix{1, 0, 0, 0, 1, 0};
        // Ordinary trees only: plane sources apply their root operation at
        // submission; masters, captures, images and vis copies have their own contracts.
        // An image inherits its target's matrix like an overlay: its own matrix first, then the target's, each part
        // about its own pivot (stock 516.1688, image-tree-transform-world). The stack starts with the target's.
        // A transformed root master's leaves take its matrix about their own pivot too, before the plane turns
        // (stock 516.1688, master-transform-world master-scale: the overlay is scaled twice).
        const bool inheritsTransform = (!root || imageParent) && !ap.resetTransform && xforms.back() != identityMatrix && !sourceTree &&
                                       (!masterTree || !ap.master) && !localCapture && !visContext;
        xforms.push_back(animation::multiply((root && !imageParent) || ap.resetTransform ? identityMatrix : xforms.back(),
                                             ap.hasTransform ? ap.matrix : identityMatrix));
        const animation::Matrix partMatrix = xforms.back(); // copy: children push
        // An overlay with neither an icon nor an icon_state draws no icon and hands none down: only a named state
        // borrows the parent's icon (stock 516.1689; maptext on one still draws).
        const uint32_t ordinal =
            root && flick ? flick->iconResIndex : ap.icon != UINT32_MAX && ap.icon != 0xffffu ? ap.icon :
            ap.state.empty() ? UINT32_MAX : inheritedIcon;
        bool missingOwnIcon = ordinal == UINT32_MAX;
        if (localCapture && root && missingOwnIcon && ap.renderSource.isEmpty()) unsupported = true;
        // A stored SOUTH value does not make an overlay's direction fixed.
        // Only the packed fixed-direction flag opts a child out of inheritance.
        int dir = root ? (rootDir ? rootDir : ap.dir) : (ap.fixedDir ? ap.dir : inheritedDir);
        if (root && !dir) dir = 2;
        if (root) dir = MapTurn(turn_).icon(dir);
        const float ownLayer = std::bit_cast<float>(ap.layerBits);
        if (std::isfinite(ownLayer) && ((root && !imageParent && !visContext) || ownLayer >= 0)) layer = ownLayer;
        if (visContext && root && ownLayer < 0 && ownLayer != -1) unsupported = true;
        if (root && visContext) plane = resolvePlane(ap.hasPlane ? ap.plane : 0, plane);
        else if (ap.hasPlane) plane = resolvePlane(ap.plane, plane);
        at += ap.offset;
        if (root) next.iconBottom = at + QPoint(0, tile_);
        // RESET_* affects inherited RGBA, never the child's own tint. In a
        // KEEP_TOGETHER subtree resets cannot escape the enclosing effect;
        // KEEP_APART makes this branch independent again. Actual group-surface
        // composition (and matrix inheritance) is still unsupported.
        if (ap.keepApart) grouped = false;
        if (!grouped) {
            if (ap.resetColor) {
                tint[0] = tint[1] = tint[2] = 1;
                for (int column = 0; column < 5; ++column) for (int row = 0; row < 3; ++row)
                    inheritedColor[4*column + row] = identityColor[4*column + row];
            }
            if (ap.resetAlpha) {
                tint[3] = 1;
                for (int column = 0; column < 5; ++column) inheritedColor[4*column + 3] = identityColor[4*column + 3];
            }
        }
        grouped |= ap.keepTogether;
        ColorMatrix material = ap.color;
        const int blend = masterTree && ap.blend == 0 ? inheritedBlend : ap.blend;
        const bool matrixTree = masterTree || visContext || (imageParent && inheritedColor != identityColor);
        if (matrixTree && (inheritedColor != identityColor || ap.color != identityColor)) {
            material = composeColor(tintColor(inheritedColor, tint), tintColor(ap.color, ap.tint));
            // Keep the legacy scalar path separate: its stock byte precision is
            // not equivalent to converting every scalar tint into a matrix.
            tint = {1,1,1,1};
            if (!finiteColor(material)) unsupported = true;
        } else for (int i = 0; i < 4; ++i) tint[i] *= ap.tint[i];
        if (trace_ && nodeIndex < 16) details.nodes[nodeIndex].alpha = tint[3];
        if (tint[3] <= 0) reject(TraceReason::ZeroAlpha, id);
        int nestedMasters = 0;
        if (!ap.master || masterTree)
            for (uint32_t child : ap.underlays) {
                if (next.visits >= maxSceneVisits || (masterTree && next.visits >= 4096)) {
                    if (visContext) next.complete = false;
                    if (rootTree || masterTree || extendedSource) unsupported = true;
                    break;
                }
                if (masterTree && appearance(child).master && ++nestedMasters > 1) unsupported = true;
                self(self, child, at, ordinal, dir, plane, layer, tint, false, grouped, textEffects || ap.maptextEffects,
                     masterTree || (imageParent && matrixTree) ? material : identityColor, blend);
            }
        if (root) rootOwnParts.first = next.parts.size();
        std::optional<Part> selectedMaster;
        if (ap.master || !ap.renderSource.isEmpty()) {
            Part part;
            part.position = at;
            part.plane = plane;
            part.layer = layer;
            part.tint = tint;
            part.color = material;
            part.blend = blend;
            part.master = ap.master;
            part.nearest = ap.pixelScale;   // a master's or a relay's matrix samples its surface unsmoothed
            const bool overlayRelay = !root && !ap.master && !ap.renderSource.isEmpty() && !localCapture;
            part.planeEffects =
                ap.master || (sourceTree && root) || overlayRelay ? withMasks(ap.planeEffects, next.complete) : nullptr;
            if (overlayRelay) part.smooth = ap.hasTransform && !ap.pixelScale;
            // Unsupported selected-master filters degrade to the same
            // underlying tree; structural tree/leaf checks above still apply.
            if (masterTree && ap.master && ap.planeEffects && ap.planeEffects->supported) {
                auto effects = std::make_shared<PlaneEffects>(*ap.planeEffects);
                // The tree's filters run before its matrix, like a lone master's (master-transform-world).
                auto unturned = *effects; unturned.matrix.reset();
                if (!effects->filters.empty() && !unturned.boundedTreeFilters()) effects->ignoreFilters();
                part.planeEffects = std::move(effects);
            }
            if (ap.master && !masterTree && (!ap.underlays.empty() || !ap.overlays.empty())) {
                auto refused = std::make_shared<PlaneEffects>();
                refused->supported = false;
                part.planeEffects = std::move(refused);
            }
            part.renderSource = ap.renderSource;
            part.renderTarget = ap.renderTarget;
            part.size = QSize(tile_, tile_);
            if (masterTree && ap.master) selectedMaster = std::move(part);
            else next.parts.push_back(std::move(part));
            next.bounds = next.bounds.united(QRect(at, QSize(tile_, tile_)));
        } else if (ordinal != UINT32_MAX) {
            const auto res = resource(ordinal);
            next.resources.push_back(res);
            if (trace_ && nodeIndex < 16) {
                auto& n = details.nodes[nodeIndex];
                n.ordinal = ordinal;
                n.direction = dir;
                const auto observed = res.result();
                n.assets = icons_.observations(ordinal, &observed);
            }
            if (!res.ready) reject(TraceReason::AssetUnavailable, id);
            if (visContext && !res.ready) next.complete = false;
            if ((rootTree || localCapture || masterTree) && !res.ready) deferred = true;
            if (res.ready) {
                const auto& icon = icons_.icon(res.id);
                if (visContext && icon.sheet.isNull()) next.complete = false;
                const auto name = QString::fromStdString(root && flick ? flick->iconState : ap.state);
                const auto* state = icon.state(name);
                // Stock flick uses default-state timing for a missing name, but
                // does NOT draw default-state pixels (ordinary states still do).
                if (root && flick && !icon.states.count(name)) state = nullptr;
                const FrameKey key{res.id, res.revision, state ? icon.index(*state, dir) : -1};
                if (icon.sheet.isNull()) reject(TraceReason::EmptyAsset, id);
                else if (!state) reject(TraceReason::MissingState, id);
                if (trace_ && nodeIndex < 16) {
                    auto& n = details.nodes[nodeIndex];
                    n.requested = name.left(64);
                    n.fallback = !icon.states.count(name) && state;
                    n.selected = state ? (n.fallback ? QString() : name.left(64)) : QStringLiteral("<none>");
                    n.cell = std::get<2>(key);
                    n.directionSlot = state ? n.cell - state->first : -1;
                }
                auto image = frameImage(key);
                if (root && image) next.iconSize = image->size();
                if ((localCapture || masterTree) && image && image->isNull()) unsupported = true;
                if (masterTree && ((state && state->frames > 1) || (image && image->size() != QSize(32, 32)))) unsupported = true;
                if (image && image->isNull() && !icon.sheet.isNull()) missingOwnIcon = true;
                if (image && image->isNull() && state && !icon.sheet.isNull()) reject(TraceReason::EmptyFrame, id);
                if (!image) {
                    reject(TraceReason::FrameWork, id);
                    deferred = true;
                }
                if (image && !image->isNull() && ap.hasTransform && !StaticAffine::make(ap.matrix, image->size())) {
                    reject(TraceReason::InvalidTransform, id);
                    unsupported = true;
                    image.reset(); // no unbounded size conversion, even on the legacy scale path
                }
                // A grouped master's same-plane local icon tree is not plane
                // content. Stock discards it (the master's own icon is ignored
                // too). KEEP_APART and distinct planes escape. Still validate
                // resources/structure above so missing/refused trees fail whole.
                const bool capturedByMaster = masterTree && grouped && plane == masterRootPlane;
                if (image && !image->isNull() && !capturedByMaster) {
                    const QSize size(std::max(1, int(std::lround(image->width() * ap.scaleX))),
                                     std::max(1, int(std::lround(image->height() * ap.scaleY))));
                    const QPoint top =
                        at + QPoint((image->width() - size.width()) / 2, tile_ - image->height() + (image->height() - size.height()) / 2);
                    Part part{std::move(image), top, plane, layer, tint};
                    part.color = material;
                    part.blend = blend;
                    part.renderTarget = ap.renderTarget;
                    part.size = size;
                    // Stock 516.1688 samples a scaled or transformed icon bilinearly (straight RGBA) unless its own
                    // appearance has PIXEL_SCALE; a parent's flag does not carry to overlays (static-transform-world).
                    const bool nearest = ap.pixelScale;
                    part.smooth = !nearest && part.size != part.image->size();
                    if (ap.affine || rootTree || inheritsTransform) {
                        next.affine = true;
                        part.position = at + QPoint(0, tile_ - part.image->height());
                        part.size = part.image->size(); // the matrix carries any scale
                        // Stock ordinary descendants retain additive pixel offsets and
                        // their OWN center/bottom anchor, including nested unequal icons.
                        part.affine = StaticAffine::make(partMatrix, part.image->size());
                        if (!part.affine) unsupported = true;
                        part.smooth = !nearest && partMatrix != std::array<double, 6>{1, 0, 0, 0, 1, 0};
                        if (rootTree) {
                            if (root) {
                                next.rootAffine = part.affine;
                                next.rootSize = part.image->size();
                            }
                            const auto bounds = part.affine ? part.affine->bounds.translated(part.position) : QRect();
                            if (std::max({std::abs(bounds.left()), std::abs(bounds.right()), std::abs(bounds.top()),
                                          std::abs(bounds.bottom())}) > 65536) {
                                reject(TraceReason::InvalidTransform, id);
                                unsupported = true;
                            }
                        }
                    }
                    next.bounds = next.bounds.united(part.affine ? part.affine->bounds.translated(part.position) : QRect(top, size));
                    const auto moving = state && !(root && flick) && icon.states.count(name) ? icon.moving.find(name) : icon.moving.end();
                    if (moving != icon.moving.end())
                        part.moving.emplace(moving->second, FrameKey{res.id, res.revision, icon.index(moving->second, dir)});
                    if (state && (state->frames > 1 || part.moving)) {
                        part.animation = *state;
                        part.frameKey = key;
                        part.flickRoot = root && flick;
                        next.animated.push_back(next.parts.size());
                    }
                    next.parts.push_back(std::move(part));
                }
            }
        }
        // Iconless ordinary roots retain a menu-only footprint. No stand-in
        // for pending/failed resources, transforms, relays or attachments.
        if (root && ap.named && missingOwnIcon && !ap.master && ap.renderSource.isEmpty() && ap.renderTarget.isEmpty() &&
            !ap.hasTransform && !imageParent && !visContext) {
            Part part;
            part.position = at;
            part.plane = plane;
            part.layer = layer;
            part.size = QSize(tile_, tile_);
            part.footprint = true;
            next.parts.push_back(std::move(part));
            next.bounds = next.bounds.united(QRect(at, QSize(tile_, tile_)));
        }
        // Maptext is independent of icon availability (including icon=null).
        // A plane source supplies the root's ink in place of its own text;
        // ordinary icon text still precedes equal-layer overlays.
        // An ordinary opacity-2 root retains its box even at alpha zero.
        if (!ap.maptext.isEmpty() && !(sourceTree && root) && (tint[3] > 0 || (!imageParent && treeRoot.mouseOpacity == 2)) &&
            !visContext) {
            const bool imageTextLeaf = imageParent && root && ap.keepApart && !ap.hasTransform && !ap.imageParentUnsupported &&
                                       ap.underlays.empty() && ap.overlays.empty();
            if ((ap.maptextEffects && !imageTextLeaf) || textEffects || grouped) reject(TraceReason::MaptextUnsupported, id);
            else {
                const QSize size(ap.maptextSize.width() == 0 ? tile_ : ap.maptextSize.width(),
                                 ap.maptextSize.height() == 0 ? tile_ : ap.maptextSize.height());
                const auto text = maptext_.lookup(ap.maptext, size);
                if (text.status == MapTextCache::Status::Deferred) {
                    reject(TraceReason::MaptextWork, id);
                    deferred = true;
                } else if (text.status == MapTextCache::Status::Unsupported) reject(TraceReason::MaptextUnsupported, id);
                else {
                    // (an outline or shadow reaches text.pad px past the box on every side)
                    const QPoint top = at + ap.maptextOffset + QPoint(0, tile_ - size.height()) - QPoint(text.pad, text.pad);
                    const QSize drawn = text.image ? text.image->size() : size;
                    Part part{text.image, top, plane, layer, tint};
                    part.color = ap.color;
                    part.blend = ap.blend;
                    part.maptext = true;
                    part.textMargin = text.pad;
                    if (partMatrix != identityMatrix && text.image) {
                        // Text turns with its atom about the icon's center, not the
                        // text box's: shift the box-centered matrix by (I - L) * delta.
                        const QPointF pivot = QPointF(at) + QPointF(tile_ / 2.0, tile_ / 2.0);
                        const QPointF center = QPointF(top) + QPointF(drawn.width() / 2.0, drawn.height() / 2.0);
                        const double dx = pivot.x() - center.x(), dy = center.y() - pivot.y(); // Y up
                        auto m = partMatrix;
                        m[2] += dx - (m[0] * dx + m[1] * dy);
                        m[5] += dy - (m[3] * dx + m[4] * dy);
                        part.affine = StaticAffine::make(m, text.image->size());
                        if (!part.affine) unsupported = true;
                        else {
                            next.affine = true;
                            next.bounds = next.bounds.united(part.affine->bounds.translated(top));
                        }
                    } else next.bounds = next.bounds.united(QRect(top, drawn));
                    next.parts.push_back(std::move(part));
                }
            }
        }
        if (root) rootOwnParts.second = next.parts.size();
        if (!ap.master || masterTree)
            for (uint32_t child : ap.overlays) {
                if (next.visits >= maxSceneVisits || (masterTree && next.visits >= 4096)) {
                    if (visContext) next.complete = false;
                    if (rootTree || masterTree || extendedSource) unsupported = true;
                    break;
                }
                if (masterTree && appearance(child).master && ++nestedMasters > 1) unsupported = true;
                self(self, child, at, ordinal, dir, plane, layer, tint, false, grouped, textEffects || ap.maptextEffects,
                     masterTree || (imageParent && matrixTree) ? material : identityColor, blend);
            }
        // Stock selects the enclosing master over a same-plane descendant.
        // Postorder commands preserve that precedence in the existing stable
        // last-command selection, without changing ordinary part ordering.
        if (selectedMaster) next.parts.push_back(std::move(*selectedMaster));
        xforms.pop_back();
        path.pop_back();
    };
    if (imageParent) {
        // An animating target supplies its sampled appearance (matrix, offset).
        const auto& parent = sampledParent ? *sampledParent : appearance(*imageParent);
        next.dependencies.emplace_back(*imageParent, parent.revision);
        const auto& root = sampled ? *sampled : appearance(rootId);
        next.dependencies.emplace_back(rootId, root.revision);
        // No target tree expansion, flick inheritance, or appearance mutation.
        // Root direction has already resolved implicit versus explicitly assigned.
        const auto& m = parent.matrix;
        // RESET_TRANSFORM images (e.g. runechat's APPEARANCE_UI_IGNORE_ALPHA)
        // ignore the target's matrix but keep its pixel offset.
        // An image on a KEEP_TOGETHER target is composed inside the target's surface, which then takes the target's
        // colour, alpha and matrix once. KEEP_APART, another plane or `override` keep it outside, with the target's
        // colour and alpha (stock 516.1689, stock-parity-world img-kt, img-kt-apart, img-kt-plane).
        const int parentPlane = parent.hasPlane ? resolvePlane(parent.plane) : 0;
        const bool joins = parent.keepTogether && !root.keepApart && !root.imageOverride &&
                           (!root.hasPlane || resolvePlane(root.plane, parentPlane) == parentPlane);
        next.joinsTarget = joins;
        // An `override` image on such a target takes neither: it replaces the surface that carried them
        // (img-kt-override; on a plain translucent target it keeps the alpha, img-alpha-override).
        const bool outside = !joins && !(parent.keepTogether && root.imageOverride);
        // ... nor its matrix: on a turned KEEP_TOGETHER atom the image stands upright in its place
        // (turned-mob-images rl-kt-image-override).
        const bool transformed = parent.hasTransform && m != std::array<double, 6>{1, 0, 0, 0, 1, 0} && !root.resetTransform && outside;
        // Any valid target matrix, including vg's 0.7 shrink and sampled
        // animation frames. Identity preserves the old attachment tree path.
        const bool bounded = parent.matrixValid;
        bool targetSupported = parent.underlays.empty() && parent.overlays.empty() && !parent.imageTargetTransformFlags;
        if (transformed && bounded && parent.underlays.empty() && !parent.overlays.empty()) {
            // Stock mob probe: ordinary overlays do not change attachment geometry, whatever their size
            // (image-tree-transform-world overlay-target-overlay has a 16 px one). Reuse ordinary validation and
            // dependencies, NOT its parts, pivot, sampling or playback; underlay trees remain refused.
            const auto& tree = get({*imageParent, rootDir, OrdinaryRequest{}, VisibilityPolicy::SeeSelf});
            next.dependencies.insert(next.dependencies.end(), tree.dependencies.begin(), tree.dependencies.end());
            next.resources.insert(next.resources.end(), tree.resources.begin(), tree.resources.end());
            next.visits += tree.visits;
            deferred |= tree.diagnosticStatus == TraceReason::TemplateDeferred;
            targetSupported = tree.complete && !tree.parts.empty() && !parent.imageTargetTransformFlags;
        }
        // The target may be a relay, have a render target or KEEP_APART: the image is drawn on it all the same (stock
        // 516.1689; whether a target's capture includes its images was not probed).
        if (parent.present && parent.valid && !parent.master &&
            parent.matrixValid && parent.colorValid &&
            (!transformed || (bounded && targetSupported && parent.maptext.isEmpty() &&
                              !root.imageTransformFlags)) &&
            root.screenLoc.isEmpty()) {
            if (transformed) {
                next.affine = true; // any icon size (wide-icon-transform-world)
                xforms.front() = m; // the image tree composes it below, part by part
                // Clicks on any part report the target's icon coordinates through its matrix, like an ordinary turned
                // tree (stock 516.1688, tests/data/click-world image-turned-target: an overlay at x+13 reads y=29).
                next.rootAffine = StaticAffine::make(m, {tile_, tile_});
                next.rootSize = QSize(tile_, tile_);
                if (!next.rootAffine) unsupported = true;
            }
            // An image with FLOAT_LAYER (an image's default) or any other negative layer draws at FLY_LAYER whatever its
            // target's layer is, on an obj, a turf or a screen object, and under what is at exactly 5 (stock 516.1689,
            // tests/data/stock-parity-world img-float, img-float-parent7, img-float-tie5, turf-img-float,
            // hud-img-float).
            // The target's colour matrix colours the image too (stock-parity-world img-matrix-parent).
            // An image that is KEEP_TOGETHER itself and has overlays is one surface of its own parts: its colour and alpha,
            // and its target's when it stays outside the target's surface, apply to that surface once (sweep
            // image-kt-own, image-kt-own-on-kt). With a matrix on the image or its target the parts still draw flat.
            // So is an image's icon with its own maptext when the image is translucent, blended or coloured, as an atom's
            // is (stock 516.1689).
            const bool texted = !root.maptext.isEmpty() && root.underlays.empty() && root.overlays.empty() &&
                                ((root.icon != UINT32_MAX && root.icon != 0xffffu) || !root.state.empty()) &&
                                (root.tint[3] != 1 || root.blend > 1 || root.color != identityColor);
            const bool ownSurface = ((root.keepTogether && (!root.underlays.empty() || !root.overlays.empty())) || texted) && !transformed &&
                                    !root.hasTransform && root.colorValid && !(root.atomFilters && !root.atomFilters->filters.empty());
            Appearance plain;
            const Appearance* const given = sampled;
            if (ownSurface) {
                plain = root; plain.tint = {1, 1, 1, 1}; plain.color = identityColor; plain.blend = 0;
                sampled = &plain; // what expand() reads the root from
            }
            const bool inherits = outside && !ownSurface;
            expand(expand, rootId, parent.offset, UINT32_MAX, 2, parentPlane, std::nextafter(5.0f, 0.0f),
                   inherits ? parent.tint : std::array<float, 4>{1, 1, 1, 1}, true, false, false,
                   inherits ? parent.color : identityColor);
            sampled = given;
            if (ownSurface && !next.affine && !next.parts.empty()) {
                auto group = std::make_shared<AppearanceGroup>();
                group->tint = root.tint; group->color = root.color; group->blend = root.blend; group->filterOnly = texted;
                if (outside && (parent.color != identityColor || root.color != identityColor)) {
                    group->color = composeColor(tintColor(parent.color, parent.tint), tintColor(root.color, root.tint));
                    group->tint = {1, 1, 1, 1};
                } else if (outside) for (int i = 0; i < 4; ++i) group->tint[i] *= parent.tint[i];
                const auto own = rootOwnParts.first < rootOwnParts.second && rootOwnParts.first < next.parts.size() ? rootOwnParts.first : 0;
                group->layer = next.parts[own].layer;
                for (auto& part : next.parts) part.group = group;
                next.grouped = true;
            }
            // An image's own filters filter its own icon, not its overlays or its target; on a transformed target the
            // filter runs first and the matrix then moves the filtered surface (stock 516.1688, atom-filter-world set b
            // image, filter-reach-world).
            const auto& [first, last] = rootOwnParts;
            if (root.atomFilters && !root.atomFilters->filters.empty() && last == first + 1 && last <= next.parts.size() &&
                next.parts[first].image && !next.parts[first].maptext) {
                auto& part = next.parts[first];
                auto group = std::make_shared<AppearanceGroup>();
                group->effects = withMasks(root.atomFilters, next.complete); group->filterOnly = true;
                group->tint = part.tint; group->color = part.color; group->blend = part.blend; group->layer = part.layer;
                if (part.affine) {
                    const auto& t = part.affine->forward;
                    const QPointF offset = QPointF(part.position) + t.map(-QPointF(part.position));
                    group->transform = QTransform(t.m11(), t.m12(), t.m21(), t.m22(), offset.x(), offset.y());
                    part.affine.reset(); part.smooth = false; // the group's quad samples the turned surface
                }
                part.tint = {1, 1, 1, 1}; part.color = identityColor; part.blend = 0;
                part.group = std::move(group);
                next.grouped = true;
            }
        } else unsupported = true;
    } else if (visContext) {
        expand(expand, rootId, {}, UINT32_MAX, 2, std::get<0>(*visContext), std::bit_cast<float>(std::get<1>(*visContext)),
               std::get<2>(*visContext), true, false);
    } else expand(expand, rootId, {}, UINT32_MAX, 2, 0, 0, {1, 1, 1, 1}, true, false);
    // Retain the root operation on every dependent part for atomic backend
    // admission. Submission applies it only to the sampled source, not children.
    if (extendedSource)
        for (auto& part : next.parts) {
            part.planeEffects = treeRoot.planeEffects;
            if (!part.renderSource.isEmpty()) part.smooth = treeRoot.hasTransform && !treeRoot.pixelScale;
            if (part.renderSource.isEmpty()) {
                if (treeRoot.color != identityColor) part.color = treeRoot.color;
                if (treeRoot.blend > 1) part.blend = treeRoot.blend;
                if (treeRoot.hasTransform && part.image) {
                    part.affine = StaticAffine::make(treeRoot.matrix, part.image->size());
                    part.smooth = !treeRoot.pixelScale;
                    if (part.affine) next.bounds = next.bounds.united(part.affine->bounds.translated(part.position));
                    else unsupported = true;
                }
            }
        }
    if (rootTree && !next.rootAffine) {
        // Iconless root (e.g. maptext-only children): input coordinates use the
        // root's matrix over one tile, the same reference an icon would give.
        next.rootAffine = StaticAffine::make(rootMatrix, {tile_, tile_});
        next.rootSize = QSize(tile_, tile_);
        if (!next.rootAffine) {
            reject(TraceReason::TransformTree, rootId);
            unsupported = true;
        }
    }
    std::sort(next.dependencies.begin(), next.dependencies.end());
    next.dependencies.erase(std::unique(next.dependencies.begin(), next.dependencies.end()), next.dependencies.end());
    std::sort(next.resources.begin(), next.resources.end(), [](const auto& a, const auto& b) { return a.ordinal < b.ordinal; });
    next.resources.erase(std::unique(next.resources.begin(), next.resources.end()), next.resources.end());
    next.diagnosticStatus = deferred      ? TraceReason::TemplateDeferred
                            : unsupported ? TraceReason::TemplateRefused
                                          : TraceReason::TemplateComplete;
    next.evidenceRevision = next.revision;
    if (trace_) {
        // Keep selected evidence across unrelated builds; all other evidence is bounded FIFO.
        while (traceTrees_.size() >= 64) {
            auto oldest = traceTrees_.begin();
            if (oldest->first == protectedTraceRevision_ && ++oldest == traceTrees_.end()) break;
            traceTrees_.erase(oldest);
        }
        traceTrees_[next.revision] = std::move(details);
    }
    if (unsupported || deferred) {
        // Playback indices address the original parts, not refusal commands or
        // the local-capture marker below. Retire them with the discarded ink.
        next.animated.clear();
        if (masterTree) {
            std::erase_if(next.parts, [](const Part& p) { return !p.master; });
            for (auto& part : next.parts) {
                auto refused = std::make_shared<PlaneEffects>();
                refused->supported = false;
                part.planeEffects = std::move(refused);
            }
        } else {
            next.parts.clear();
            next.bounds = {};
        }
        next.complete = false;
    }
    if (localCapture && (next.parts.empty() || !next.complete)) {
        Part marker;
        marker.renderTarget = treeRoot.renderTarget;
        marker.footprint = true;
        marker.plane = treeRoot.hasPlane ? resolvePlane(treeRoot.plane) : 0;
        marker.size = QSize(tile_, tile_);
        next.parts = {marker};
        next.bounds = QRect({}, marker.size);
    }
    if (deferred) next.revision = 0;
    return next;
}

MapSprites EntityMap::prepare(const model::World& w, const TurfMap::Frame& terrain,
                             const TurfMap::Read& read, int64_t nowNs, Stats* stats, bool scene) {
    using Clock = std::chrono::steady_clock;
    const auto start = stats ? Clock::now() : Clock::time_point{};
    if (stats) *stats = {};
    if (!beginAttempt(w, terrain, read, nowNs)) return {};
    const auto& g = terrain.geometry;
    AppearanceLookup appearance{w.appearances, appearances_, epoch_, revision_, tile_, stats};
    FrameLookup frameImage{icons_, frames_, frameBytes_, epoch_, stats};
    TemplateBuilder builder{appearance, frameImage, icons_, maptext_, templates_, epoch_, revision_,
                            tile_, seeInvisible_, stats, trace_ != nullptr, traceTrees_, protectedTraceRevision_};
    builder.turn_ = turn_;
    AnimationSampler sampler{appearance, tile_, nowNs};
    // World-space instances survive terrain recentering. Camera-only movement needs
    // only viewport culling/translation; it cannot invalidate an appearance template.
    const QPoint translation(-g.region.x() * tile_, (g.region.y() + g.region.height()) * tile_);
    const QRect view = terrain.source.translated(-translation);
    // Screen objects are placed against the view itself; the scene may be larger for those outside it.
    const QMargins around = g.hud;
    QMargins wanted;
    const QRect inner = view - around * tile_;
    // What the map shows before it is turned by client.dir; world sprites are placed and culled against this.
    const MapTurn turn(g.dir);
    const QRect worldInner = turn.rect(inner), worldView = turn.rect(view).united(worldInner);
    if (trace_) trace_->map.view = view;
    bool geometryChanged = false, orderChanged = false;
    nextVisible_.clear();
    uint64_t budget = maxSceneVisits, candidates = maxSceneVisits;
    // Resolve a shared flick ordinal once, rather than charging dependency work
    // for every independently timed instance using the same asset.
    std::unordered_map<uint32_t, Resource> flickResources;
    // Stock supplement images extend their target's visibility bounds, just like
    // overlays. They do not inherit target invisibility. Reserve bounded discovery
    // work before the ordinary occurrences, without changing playback timing.
    std::unordered_map<uint32_t,QRect> imageVisibilityBounds;
    if (scene && mapName_.isEmpty() && g.visibility) {
        uint64_t work=maxSceneVisits;
        for (const auto& [id,image] : w.imageInstances) {
            if (!work) break;
            --work;
            if (image.targetHud || !w.imageTargetCurrent(id,image.identity,image.target,image.targetIdentity)) continue;
            const auto& target=w.atoms.at(image.target);
            if (sampler.unsettled(target.chain)) continue; // animated attachment inheritance is not recovered here
            Appearance sampled; bool composing = false;
            const auto chain = w.imageChains.find(id);
            const bool animated = chain != w.imageChains.end() && (chain->second.rejected || !chain->second.steps.empty());
            if (animated && !sampler.sample(chain->second, image.appid, sampled, composing)) continue;
            const int dir=appearance(image.appid).imageOwnDir ? 0 : (target.dir ? target.dir : appearance(target.appid).dir);
            const TemplateRequest request{image.appid, dir, ImageRequest{target.appid}};
            Template sampledShape;
            const auto& shape = composing ?
                (sampledShape = builder.sample({image.appid, dir, ImageRequest{target.appid}}, sampled)) : builder.get(request);
            if (shape.visits>work) break;
            work-=shape.visits;
            imageVisibilityBounds[image.target]=imageVisibilityBounds[image.target].united(shape.bounds);
        }
    }
    std::map<uint32_t, std::vector<uint32_t>> hudImages;
    std::set<uint32_t> worldOverrides, hudOverrides;
    if (scene) {
        uint64_t work = maxSceneVisits;
        for (const auto& [id, im] : w.imageInstances) {
            if (!work) break;
            --work;
            if (!w.imageTargetCurrent(id, im.identity, im.target, im.targetIdentity)) continue;
            const bool hud = w.imageHudCurrent(id);
            if (hud) {
                const auto& owner = appearance(w.hudAppid.at(im.target));
                if (!owner.screenTarget || owner.screenTarget->map != mapName_) continue;
            } else if (!mapName_.isEmpty()) continue;
            if (hud) hudImages[im.target].push_back(id);
            if (!appearance(im.appid).imageOverride ||
                (!im.targetHud && sampler.unsettled(w.atoms.at(im.target).chain))) continue;
            Appearance sampled; bool composing = false;
            const auto chain = w.imageChains.find(id);
            if (chain != w.imageChains.end() && !sampler.sample(chain->second, im.appid, sampled, composing)) continue;
            auto admitOverride = [&](uint32_t parent, int dir, std::set<uint32_t>& out) {
                const TemplateRequest request{im.appid, appearance(im.appid).imageOwnDir ? 0 : dir, ImageRequest{parent}};
                Template sampledShape;
                const auto& shape = composing ?
                    (sampledShape = builder.sample({request.rootId, request.rootDir, ImageRequest{parent}}, sampled)) : builder.get(request);
                if (shape.visits > work) { work = 0; return; }
                work -= shape.visits;
                if (shape.complete && appearance(im.appid).present && appearance(im.appid).valid) out.insert(im.target);
            };
            if (!im.targetHud) {
                const auto& target = w.atoms.at(im.target);
                admitOverride(target.appid, target.dir ? target.dir : appearance(target.appid).dir, worldOverrides);
            }
            if (hud) admitOverride(w.hudAppid.at(im.target), appearance(w.hudAppid.at(im.target)).dir, hudOverrides);
        }
    }
    struct VisPlacement {
        const Template* shape = nullptr;
        QPoint origin, cell, iconBottom;
        std::shared_ptr<const VisHit> hit;
        uint32_t order = 0;
        bool screen = false;
        uint32_t screenOrder = 0;
        uint8_t underlay = 0; // as MapSprite::visUnderlay
        QPoint holderOrigin; // an underlay sorts at its holder's position, not by its own offset
        std::optional<QPoint> parentFoot; // a nested underlay sorts where its parent copy does
        // At an equal layer stock orders a copy by where its source stands (any z), and a nullspace source by the
        // copy's own offset like any atom (516.1688, vis-world set a: obj-turf-south, obj-l2-north/south/null).
        std::optional<QPoint> sourceCell;
        uint64_t joinKey = 0; // the KEEP_TOGETHER holder's instance, in whose surface the copy is composed (+1)
    };
    // The render targets ordinary atoms produce, found on first use.
    std::optional<std::set<QString>> atomTargets;
    auto atomTarget = [&](const QString& name) {
        if (!atomTargets) {
            atomTargets.emplace();
            for (const auto& [key, atom] : w.atoms) {
                const auto& ap = appearance(atom.appid);
                if (ap.present && !ap.master && !ap.renderTarget.isEmpty()) atomTargets->insert(ap.renderTarget);
            }
        }
        return atomTargets->count(name) != 0;
    };
    enum class WorldPlacement { Turf, Entity };
    // placed: position is a master's own, one of its range. repeat: a later one, which draws only what the master owns.
    struct HudPlacement { QPoint position; uint32_t order; const model::Atom::Chain* chain = nullptr; bool placed = false, repeat = false; };
    using Location = std::variant<WorldPlacement, HudPlacement>;
    struct ImagePlacement { const model::World::ImageInstance& image; Location location; };
    // Images can be world or HUD. Vis copies reserve their entire template and
    // world/screen placement during discovery; place() never resamples them.
    using Placement = std::variant<WorldPlacement, HudPlacement, ImagePlacement, std::reference_wrapper<const VisPlacement>>;
    auto place = [&](uint64_t key, const model::Atom& atom, const Placement& placement) {
        const auto* attached = std::get_if<ImagePlacement>(&placement);
        const auto* image = attached ? &attached->image : nullptr;
        const auto* copy = std::get_if<std::reference_wrapper<const VisPlacement>>(&placement);
        const auto* vis = copy ? &copy->get() : nullptr;
        const auto* hud = attached ? std::get_if<HudPlacement>(&attached->location) : std::get_if<HudPlacement>(&placement);
        MapSprite::Kind kind;
        if (hud || (vis && vis->screen)) kind = MapSprite::Screen;
        else if (vis) kind = MapSprite::Vis;
        else {
            const auto world = attached ? std::get<WorldPlacement>(attached->location) : std::get<WorldPlacement>(placement);
            if (world == WorldPlacement::Turf) kind = MapSprite::Turf;
            else if (image) kind = MapSprite::Image;
            else kind = MapSprite::Entity;
        }
        QPoint screenPosition = hud ? hud->position : QPoint();
        uint32_t screenOrder = hud ? hud->order : 0;
        auto* diagnostic = trace_ && key == trace_->key && kind == MapSprite::Entity ? &trace_->map : nullptr;
        if ((!budget && !vis) || !candidates) { if (diagnostic) diagnostic->placement = TraceReason::SceneWork; return; }
        --candidates; // absent/invalid appearances and off-view candidates also consume work
        const bool screen = hud != nullptr || (vis && vis->screen);
        if (!screen && !vis && (!atom.hasPos || atom.z != g.z ||
            atom.x < 1 || atom.y < 1 || atom.x > w.bounds.maxX || atom.y > w.bounds.maxY)) {
            if (diagnostic) diagnostic->placement = TraceReason::Placement;
            return;
        }
        if (stats) ++stats->candidates;
        const model::World::Flick* flick = nullptr;
        // Stock plays a flick on an obj, a mob, a turf and a screen object (516.1689).
        if (!image && !vis) {
            const model::World::Flick* held = nullptr;
            if (kind == MapSprite::Turf) {
                const auto event = w.turfFlicks.find({uint16_t(atom.x - 1), uint16_t(atom.y - 1), uint16_t(atom.z)});
                if (event != w.turfFlicks.end()) held = &event->second;
            } else if (const auto event = w.flicks.find(uint32_t(key)); event != w.flicks.end() &&
                       event->second.key == atom.key && event->second.haveKey == atom.haveKey) held = &event->second;
            if (held && held->haveRes) {
                const auto& f = *held;
                auto [asset, inserted] = flickResources.try_emplace(f.iconResIndex);
                if (inserted) asset->second = builder.resource(f.iconResIndex);
                const auto& res = asset->second;
                if (res.ready) {
                    const auto& icon = icons_.icon(res.id);
                    const auto* state = icon.state(QString::fromStdString(f.iconState));
                    if (!icon.generated && state && state->flickFrameAt(nowNs, f.startNs) >= 0) flick = &f;
                }
            }
        }
        const bool turfImage = image && image->locKind == 7;
        uint32_t parentId = 0;
        if (image) {
            if (turfImage) parentId = w.tiles.at(image->turfCell);
            else if (screen) parentId = w.hudAppid.at(image->target);
            else parentId = w.atoms.at(image->target).appid;
        }
        // Images follow an animating target's sampled transform/offset.
        Appearance sampledTarget; bool targetComposing = false;
        if (image && !turfImage && !image->targetHud) {
            const auto& target = w.atoms.at(image->target);
            if ((!target.chain.steps.empty() || target.chain.rejected) &&
                !sampler.sample(target.chain, target.appid, sampledTarget, targetComposing, false, true)) return;
        }
        if (turfImage && (image->hasVis || appearance(image->appid).imageOverride || w.imageChains.count(uint32_t(key)))) return;
        const model::Atom::Chain* chain = vis ? nullptr : hud && !image ? hud->chain :
            atom.chain.steps.empty() && !atom.chain.rejected ? nullptr : &atom.chain;
        if (image) {
            const auto found = w.imageChains.find(uint32_t(key));
            chain = found != w.imageChains.end() && (found->second.rejected || !found->second.steps.empty()) ? &found->second : nullptr;
        }
        Appearance sampled; bool composing = false;
        if ((chain && !sampler.sample(*chain, atom.appid, sampled, composing, screen && !image && appearance(atom.appid).master,
                                        !image && appearance(atom.appid).keepTogether)) || (composing && vis)) {
            if (diagnostic) diagnostic->placement = TraceReason::AnimationUnavailable;
            return;
        }
        // Stock draws a screen object at its screen_loc whatever its own pixel offset, animated or not (hud-anim-world).
        if (hud && !image && composing) sampled.offset = {};
        int rootDir = atom.dir;
        if (image) {
            if (appearance(atom.appid).imageOwnDir) rootDir = 0;
            else if (!rootDir) rootDir = appearance(parentId).dir;
        }
        auto visibility = VisibilityPolicy::RespectInvisibility;
        if (g.visibility && (g.visibility->sight & 0x20u) && kind == MapSprite::Entity &&
            w.haveSelf && key == (w.selfRef | model::kRegistryA)) visibility = VisibilityPolicy::SeeSelf;
        Template sampledShape; // owned until all placement/playback consumers finish
        const auto& shape = [&]() -> const Template& {
            if (vis) return *vis->shape;
            if (composing || targetComposing) {
                // A flick plays its own frames on the sampled (animating) root.
                // FlickRequest holds a reference, so the variant is initialized, not assigned.
                const SampleRequest request =
                    image ? SampleRequest{atom.appid, rootDir, ImageRequest{parentId}, visibility}
                    : flick ? SampleRequest{atom.appid, rootDir, FlickRequest{flick->iconResIndex, flick->iconState}, visibility}
                            : SampleRequest{atom.appid, rootDir, OrdinaryRequest{}, visibility};
                builder.sampledParent = targetComposing ? &sampledTarget : nullptr;
                sampledShape = builder.sample(request, composing ? sampled : appearance(atom.appid));
                builder.sampledParent = nullptr;
                return sampledShape;
            }
            if (image) return builder.get({atom.appid, rootDir, ImageRequest{parentId}, visibility});
            if (flick) return builder.get({atom.appid, rootDir, FlickRequest{flick->iconResIndex, flick->iconState}, visibility});
            return builder.get({atom.appid, rootDir, OrdinaryRequest{}, visibility});
        }();
        // Stock verifies the same childless world geometry in both atom registries.
        // Tree/effect validation remains in TemplateBuilder, including the narrower
        // inherited image subset. No HUD, vis copies or flick combinations.
        if (diagnostic) {
            diagnostic->first = shape.first; diagnostic->revision = shape.revision;
            diagnostic->templateStatus = shape.diagnosticStatus;
            diagnostic->complete = shape.complete; diagnostic->parts = shape.parts.size();
            diagnostic->bounds = shape.bounds; diagnostic->affine = shape.affine;
            if (!shape.parts.empty() && shape.parts[0].affine) diagnostic->validatedTransform = shape.parts[0].affine->forward;
            const auto detail = traceTrees_.find(shape.evidenceRevision);
            if (detail != traceTrees_.end()) {
                protectedTraceRevision_ = shape.evidenceRevision;
                diagnostic->detailsObserved = true; diagnostic->tree = detail->second;
                for (auto& node : diagnostic->tree.nodes) if (node.ordinal != UINT32_MAX) {
                    const auto res = std::lower_bound(shape.resources.begin(), shape.resources.end(), node.ordinal,
                        [](const Resource& r, uint32_t ordinal) { return r.ordinal < ordinal; });
                    if (res != shape.resources.end() && res->ordinal == node.ordinal) {
                        const auto observed = res->result(); node.assets = icons_.observations(node.ordinal, &observed);
                    }
                }
            }
        }
        // A turned turf, or a turf with a turned overlay, draws like an obj (stock 516.1689).
        if (shape.affine && ((kind != MapSprite::Entity && kind != MapSprite::Image && kind != MapSprite::Screen &&
                              kind != MapSprite::Turf) || (flick && kind != MapSprite::Turf) || vis)) {
            if (diagnostic) diagnostic->placement = TraceReason::EffectCombination;
            return;
        }
        // A copied turf names itself (no atom record); everything else names its target atom.
        const bool visTurf = vis && vis->hit->turf;
        const auto visTarget = vis && !visTurf ? w.atoms.find(vis->hit->target) : w.atoms.end();
        uint32_t pickingAppearance = atom.appid;
        if (visTarget != w.atoms.end()) pickingAppearance = visTarget->second.appid;
        else if (image) pickingAppearance = parentId;
        const int mouseOpacity = appearance(pickingAppearance).mouseOpacity;
        const bool mouseTransparent = mouseOpacity == 0 || (vis && !visTurf && visTarget == w.atoms.end());
        const bool interactive = (screen || image || vis) && (atom.haveKey || turfImage || visTurf) && !mouseTransparent;
        const uint64_t imageIdentity = image ? image->identity : 0;
        const uint64_t targetIdentity = image ? image->targetIdentity : atom.renderIdentity;
        const uint64_t hudIdentity = vis ? vis->hit->hudMembership : screen ? atom.renderIdentity : 0;
        // Bound total expanded scene size even when many atoms share one cached tree.
        // Do not cache a partial tree dependent on another atom's remaining frame budget.
        if (!vis) { // vis forests reserve their entire cost before placing siblings
            if (shape.visits > budget) { if (diagnostic) diagnostic->placement = TraceReason::SceneWork; budget = 0; return; }
            budget -= shape.visits;
        }
        QPoint origin((atom.x - 1) * tile_, -atom.y * tile_);
        const auto settled = w.appearances.find(atom.appid);
        const auto targetSettled = targetComposing ? w.appearances.find(w.atoms.at(image->target).appid) : w.appearances.end();
        const bool endpointPixels = (chain && !chain->steps.empty() && !image && settled != w.appearances.end() && settled->second.fields.hasPixelOffsets &&
            (composing || atom.appid == chain->steps.back().to)) ||
            // The sampled target offset replaces the copied settled target pixels.
            (targetSettled != w.appearances.end() && targetSettled->second.fields.hasPixelOffsets);
        origin += QPoint(atom.stepX, -atom.stepY);
        if (!endpointPixels) origin += QPoint(atom.pixelX + atom.pixelW, -atom.pixelY - atom.pixelZ);
        if (atom.gliding && nowNs && atom.glide.startNs) {
            const double left = model::glideLeft(atom.glide, nowNs);
            origin += QPoint(int(std::lround(atom.glide.sx * tile_ * left)),
                             -int(std::lround(atom.glide.sy * tile_ * left)));
        }
        if (vis) { origin = vis->origin; screenOrder = vis->screen ? vis->screenOrder : vis->order; }
        bool screenMasterPlacementRefused = false, masterPartsOffScene = false;
        if (hud) {
            const auto& ap = appearance(image ? parentId : atom.appid);
            if (ap.master) {
                // Only owned children follow screen_loc. Plane capture/material
                // commands stay viewport-wide; the renderer ignores their origin.
                const auto positions = ap.screenPlacement && !ap.screenPlacement->range ?
                    ap.screenPlacement->positions({inner.width() / tile_, inner.height() / tile_}, tile_, around) : std::vector<QPoint>{};
                if (positions.size() == 1) screenPosition = positions.front();
                else if (!hud->placed) {
                    // A master placed off the view acts on its plane like any other, and draws what it owns there,
                    // in the larger map stock makes for it (stock 516.1689, master-screen-loc-world "8,1", "0,4",
                    // "6,6 to 9,9"). Until the scene has grown that far, or when it cannot, its parts lie outside
                    // the scene and are left out: an approximation for a part large enough to reach back in.
                    const QMargins reach = ap.screenPlacement ?
                        ap.screenPlacement->outside({inner.width() / tile_, inner.height() / tile_}) : QMargins();
                    masterPartsOffScene = reach.left() > around.left() || reach.top() > around.top() ||
                        reach.right() > around.right() || reach.bottom() > around.bottom();
                    // A tree whose screen_loc is a form this does not place (a range with a percent end, or one
                    // that runs backwards with pixel offsets) is refused, which drops its plane: stock places a
                    // master's range like an ordinary screen object's, and those forms are not understood for either.
                    screenMasterPlacementRefused = !masterPartsOffScene && (ap.keepTogether || ap.keepApart ||
                        !ap.underlays.empty() || !ap.overlays.empty() ||
                        (ap.planeEffects && ap.planeEffects->boundedTreeFilters()));
                }
            }
            // A relay of a plane's surface draws the plane's picture moved as far from the view's corner as its
            // screen_loc places it from 1,1, pixel offsets included (stock 516.1689, plane-relay-world loc23, locpx); the
            // composition plan reads that from the relay's position. A relay of an atom's render target is drawn like
            // an icon (stock-parity-world relay-hud*, relay-obj). Every form places it as it places any screen object
            // with a one-tile icon: CENTER, EAST, NORTH and the edges count in tiles (loccenter, locne, locleft
            // "LEFT+1,BOTTOM+2" and locright "RIGHT-5,TOP-4" draw as 2,3 does), a percent moves it like an ordinary
            // object at that screen_loc (locpd-mark, locpe, locpf-mark), and a range draws the picture once at every
            // position (locrange, locrow). The relay's own pixel_x/y/w/z moves neither its picture nor its clicks (rpx,
            // rpw-loc23, where DreamDaemon sends the relay's appearance without them); an appearance that carries
            // them is drawn unmoved too, which is inferred, not measured.
            const bool planeRelay = !ap.master && !ap.renderSource.isEmpty() && ap.renderTarget.isEmpty() &&
                                    !atomTarget(ap.renderSource);
            QPoint relayAlign, relayOffset;
            if (planeRelay) {
                const auto& loc = ap.screenPlacement;
                if (!loc) return;
                // The placement aligned a RIGHT or TOP end by the root icon's size; a relay counts as one tile
                // (measured with a relay that has no icon of its own).
                if (!loc->range && !shape.iconSize.isEmpty()) {
                    if (loc->first.x.anchor == ScreenLoc::Anchor::FarEdge) relayAlign.rx() = shape.iconSize.width() - tile_;
                    if (loc->first.y.anchor == ScreenLoc::Anchor::FarEdge) relayAlign.ry() = shape.iconSize.height() - tile_;
                }
                relayOffset = ap.offset;
            }
            // A plane's surface and its relay cover the whole scene; anything else stands on the view.
            const QRect& base = !ap.master && !ap.renderSource.isEmpty() && !atomTarget(ap.renderSource) ? view : inner;
            origin = QPoint(base.left() + screenPosition.x() + relayAlign.x(),
                            base.bottom() + 1 - tile_ - screenPosition.y() - relayAlign.y()) - relayOffset;
        }
        if (diagnostic) { diagnostic->placedBounds = shape.bounds.translated(origin); diagnostic->view = view; }
        const auto& rootAppearance = appearance(atom.appid);
        // screen_loc on a world atom does not make this occurrence a HUD tree.
        // Keep the verified placement bound without exposing raw refused planes.
        const bool refuseMasterPlacement = screenMasterPlacementRefused || (!screen && rootAppearance.master &&
            (rootAppearance.keepTogether || rootAppearance.keepApart ||
             !rootAppearance.underlays.empty() || !rootAppearance.overlays.empty()));
        // A plane's surface is relayed only with the matrices and filters stock was probed with (relay-gate-world); a
        // relay of an atom's render target takes any, and is settled by the composition plan.
        for (const auto& part : shape.parts)
            if (!part.master && !part.renderSource.isEmpty() && part.planeEffects && !part.planeEffects->relayFilters() &&
                !atomTarget(part.renderSource)) return;
        const bool localCapture = !rootAppearance.master && !rootAppearance.renderTarget.isEmpty();
        // The map ends at the scene: the view and the tiles a screen object outside it adds, where stock draws the
        // world too, turfs and objects of any plane (516.1689, master-screen-loc-world "8,1|obj|wide").
        if (shape.bounds.isEmpty() || (!screen && !localCapture && rootAppearance.renderSource.isEmpty() && !shape.bounds.translated(origin).intersects(worldView))) {
            if (diagnostic) diagnostic->placement = shape.bounds.isEmpty() ? TraceReason::EmptyBounds : TraceReason::OffView;
            if (stats) ++stats->culled;
            return;
        }
        if (!screen && g.visibility) {
            const uint32_t target=image ? image->target : uint32_t(key);
            const bool atomCategory = kind==MapSprite::Entity || (image && !turfImage);
            auto category=kind==MapSprite::Turf ? VisibilityFrame::Turf :
                atomCategory && (target & model::kRegistryA) ? VisibilityFrame::Mob : VisibilityFrame::Object;
            if (atomCategory && w.haveSelf && target==(w.selfRef|model::kRegistryA))
                category=VisibilityFrame::Category(uint32_t(category)|0x20u);
            QRect visibilityBounds=shape.bounds;
            if (kind==MapSprite::Entity || (image && !turfImage)) {
                const auto attachments=imageVisibilityBounds.find(target);
                if (attachments!=imageVisibilityBounds.end()) visibilityBounds=visibilityBounds.united(attachments->second);
                if (image) {
                    const auto& root=w.atoms.at(target);
                    visibilityBounds=visibilityBounds.united(builder.get({root.appid,root.dir}).bounds);
                }
            }
            const QPoint anchor=(vis ? vis->cell : QPoint(atom.x,atom.y))-QPoint(1,1);
            const auto outcome=g.visibility->test(visibilityBounds.translated(origin),category,anchor);
            if (diagnostic) {
                diagnostic->visibility=visibilityName(outcome);
                diagnostic->visibilityBounds=visibilityBounds.translated(origin);
            }
            if (outcome!=VisibilityOutcome::Visible && outcome!=VisibilityOutcome::Sight) {
                if (diagnostic) diagnostic->placement=outcome==VisibilityOutcome::Hidden ? TraceReason::Occluded :
                    outcome==VisibilityOutcome::Unsupported ? TraceReason::VisibilityUnsupported :
                    outcome==VisibilityOutcome::Budget ? TraceReason::VisibilityBudget :
                    outcome==VisibilityOutcome::OffView ? TraceReason::OffView : TraceReason::VisibilityUnknown;
                return;
            }
        }
        // An image composed in its target's KEEP_TOGETHER surface takes that surface's group and occurrence, so the
        // compositor draws it with the target's parts. The target must be drawn this frame, still or animating.
        std::shared_ptr<const AppearanceGroup> joinGroup;
        QPoint joinOrigin, joinBottom;
        uint64_t joinOccurrence = 0;
        if ((image && shape.joinsTarget && !turfImage) || (vis && vis->joinKey)) {
            const uint64_t targetKey = vis ? vis->joinKey - 1 : screen ? (((key >> 32) & 0x0fffffffu) << 40) | (uint64_t(2) << 32) | image->target
                                              : uint64_t(image->target);
            const auto placed = instances_.find(targetKey);
            if (placed != instances_.end() && placed->second.used == epoch_) {
                joinGroup = placed->second.rootGroup;
                joinOrigin = placed->second.origin;
                if (!placed->second.sprites.isEmpty()) joinBottom = placed->second.sprites.front().iconBottom;
                joinOccurrence = targetKey + 1;
            }
            if (!joinGroup) return; // the surface it belongs in is not drawn
        }
        nextVisible_.push_back(key);
        auto& instance = instances_[key];
        instance.used = epoch_;
        const QPoint cell = vis ? vis->cell : QPoint(atom.x, atom.y);
        const bool shapeChanged = instance.revision != shape.revision || instance.sprites.empty() ||
            instance.masterPlacementRefused != refuseMasterPlacement || instance.masterPartsOffScene != masterPartsOffScene ||
            instance.joined != joinGroup.get();
        instance.joined = joinGroup.get();
        instance.rootGroup = shape.complete ? shape.rootGroup : nullptr;
        const bool cellChanged = instance.cell != cell || instance.screenOrder != screenOrder ||
            (!instance.sprites.empty() && instance.sprites.front().attachmentOrder != (vis ? vis->order : 0));
        if (shapeChanged || cellChanged || instance.origin != origin || (rootAppearance.master && view_ != view) ||
            instance.key != atom.key || instance.haveKey != atom.haveKey ||
            instance.imageIdentity != imageIdentity || instance.targetIdentity != targetIdentity || instance.hudIdentity != hudIdentity || instance.visHit != (vis ? vis->hit : nullptr) ||
            (kind == MapSprite::Turf && localCapture && !instance.sprites.empty() &&
             instance.sprites.front().turfDimensions != QSize(w.bounds.maxX,w.bounds.maxY))) {
            if (stats) ++stats->instanceUpdates;
            geometryChanged = true;
            orderChanged |= shapeChanged || cellChanged || instance.imageIdentity != imageIdentity;
            std::set<int> ownedMasterPlanes;
            if (rootAppearance.master) for (const auto& part : shape.parts)
                if (part.master) ownedMasterPlanes.insert(part.plane);
            if (shapeChanged) {
                instance.sprites.clear();
                instance.sprites.reserve(qsizetype(shape.parts.size()));
                std::map<const AppearanceGroup*, std::shared_ptr<const AppearanceGroup>> joinedGroups;
                for (const auto& part : shape.parts) {
                    if ((refuseMasterPlacement || masterPartsOffScene) && !part.master) continue;
                    if (hud && hud->repeat && part.master) continue;
                    MapSprite sprite{part.image ? *part.image : QImage(), origin + part.position, origin + shape.iconBottom, cell,
                                     uint32_t(key), atom.key, atom.haveKey, part.plane, part.layer, part.tint};
                    sprite.kind = part.master ? MapSprite::Master : kind;
                    sprite.appearanceId = atom.appid;
                    sprite.sourcePath = part.sourcePath;
                    // Selected owned planes report leaf-local coordinates;
                    // unmastered escapes report viewport-relative coordinates,
                    // independent of the root's screen_loc. Both name the root.
                    if (rootAppearance.master && !part.master && part.image)
                        sprite.iconBottom = ownedMasterPlanes.contains(part.plane) ?
                            sprite.position + QPoint(0,part.image->height()) : QPoint(view.left(),view.bottom()+1);
                    sprite.hudIdentity = hudIdentity;
                    sprite.targetIdentity = targetIdentity;
                    if (image) {
                        sprite.atom = image->target; sprite.imageId = uint32_t(key);
                        sprite.imageIdentity = imageIdentity; sprite.targetIdentity = targetIdentity;
                    }
                    if (vis) {
                        sprite.atom = vis->hit->target; sprite.visHit = vis->hit;
                        sprite.key = visTarget != w.atoms.end() ? visTarget->second.key : 0;
                        sprite.haveKey = visTarget != w.atoms.end() && visTarget->second.haveKey;
                        sprite.iconBottom = vis->iconBottom; sprite.visUnderlay = vis->underlay;
                    }
                    sprite.screenOrder = screenOrder;
                    sprite.screenInteractive = interactive && !part.master && part.renderSource.isEmpty();
                    sprite.maptext = part.maptext; sprite.textMargin = part.textMargin; sprite.footprint = part.footprint;
                    sprite.mouseOpacity = mouseOpacity;
                    sprite.mouseTransparent = mouseTransparent || (part.maptext && mouseOpacity < 2);
                    sprite.color = part.color; sprite.blend = part.blend;
                    sprite.inkInteractive = part.inkInteractive && !(rootAppearance.master && !part.master && part.blend == 4);
                    sprite.renderSource = part.renderSource; sprite.renderTarget = part.renderTarget; sprite.size = part.size;
                    sprite.planeSourceDependency = shape.planeSourceDependency;
                    sprite.affine = part.affine; sprite.planeEffects = part.planeEffects;
                    sprite.appearanceGroup=part.group;
                    if (part.group) sprite.groupLayer=part.layer;
                    for (auto group=part.group;group;group=group->parent) sprite.layer=group->layer;
                    if (joinGroup) {
                        // The image's own groups hang under the target's surface; a part of none sorts in it by its layer.
                        auto under = [&](auto&& self, const std::shared_ptr<const AppearanceGroup>& own)
                            -> std::shared_ptr<const AppearanceGroup> {
                            if (!own) return joinGroup;
                            auto& copy = joinedGroups[own.get()];
                            if (!copy) {
                                auto next = std::make_shared<AppearanceGroup>(*own);
                                next->parent = self(self, own->parent);
                                // (an image's own surface sorts in the target's like the image would)
                                if (!own->parent && next->layer == std::nextafter(5.0f, 0.0f)) next->layer = 5.0f;
                                copy = std::move(next);
                            }
                            return copy;
                        };
                        // In the surface a float-layer image is at 5 exactly, over a target at 5 (flick-world).
                        if (!part.group) sprite.groupLayer = part.layer == std::nextafter(5.0f, 0.0f) ? 5.0f : part.layer;
                        sprite.appearanceGroup = under(under, part.group);
                        sprite.layer = joinGroup->layer;
                    }
                    if (refuseMasterPlacement) {
                        auto effects = std::make_shared<PlaneEffects>(); effects->supported = false;
                        sprite.planeEffects = std::move(effects);
                    }
                    sprite.rootAffine = shape.rootAffine; sprite.rootSize = shape.rootSize;
                    sprite.smooth = part.smooth; // a turf's too (stock 516.1689)
                    sprite.nearest = part.nearest;
                    instance.sprites.push_back(std::move(sprite));
                }
            } else {
                const QPoint delta = origin - instance.origin;
                for (auto& sprite : instance.sprites) {
                    sprite.position += delta; sprite.iconBottom += delta;
                    if (rootAppearance.master && sprite.kind != MapSprite::Master && !ownedMasterPlanes.contains(sprite.plane))
                        sprite.iconBottom = QPoint(view.left(),view.bottom()+1);
                    sprite.cell = cell; sprite.key = atom.key; sprite.haveKey = atom.haveKey;
                    sprite.hudIdentity = hudIdentity;
                    sprite.targetIdentity = targetIdentity;
                    if (image) {
                        sprite.atom = image->target; sprite.imageId = uint32_t(key);
                        sprite.imageIdentity = imageIdentity; sprite.targetIdentity = targetIdentity;
                    }
                    if (vis) {
                        sprite.atom = vis->hit->target; sprite.visHit = vis->hit;
                        sprite.key = visTarget != w.atoms.end() ? visTarget->second.key : 0;
                        sprite.haveKey = visTarget != w.atoms.end() && visTarget->second.haveKey;
                        sprite.iconBottom = vis->iconBottom; sprite.visUnderlay = vis->underlay;
                    }
                    sprite.screenOrder = screenOrder;
                    sprite.screenInteractive = interactive && sprite.kind != MapSprite::Master && sprite.renderSource.isEmpty();
                    sprite.mouseOpacity = mouseOpacity;
                    sprite.mouseTransparent = mouseTransparent || (sprite.maptext && mouseOpacity < 2);
                }
            }
            const QPoint offset = vis && vis->underlay ? vis->holderOrigin - QPoint((cell.x()-1)*tile_,-cell.y()*tile_) :
                vis && vis->parentFoot ? *vis->parentFoot - QPoint((cell.x()-1)*tile_,-cell.y()*tile_) :
                origin - QPoint((cell.x()-1)*tile_,-cell.y()*tile_) + shape.iconBottom - QPoint(0,tile_);
            const QPoint sortCell = screen ? QPoint() : vis && !vis->underlay && vis->sourceCell ? *vis->sourceCell :
                cell + QPoint(std::min(0, int(std::floor(double(offset.x()) / tile_))),
                              std::min(0, int(std::floor(double(-offset.y()) / tile_))));
            for (auto& sprite : instance.sprites) {
                // Independent stock oracle: iconless neutral leaf HUD master at
                // 1,1; no tree, affine, filter, attachment or relay admission.
                sprite.masterFallback = sprite.kind == MapSprite::Master && screen && interactive &&
                    tile_ == 32 && !image && !vis && !flick && !chain && !refuseMasterPlacement &&
                    mouseOpacity == 2 && rootAppearance.screenLoc == "1,1" &&
                    (rootAppearance.icon == UINT32_MAX || rootAppearance.icon == 0xffffu) &&
                    rootAppearance.offset.isNull() && rootAppearance.underlays.empty() && rootAppearance.overlays.empty() &&
                    !rootAppearance.hasTransform && !rootAppearance.animationEffects && !rootAppearance.visFlags &&
                    !rootAppearance.keepTogether && !rootAppearance.keepApart && !rootAppearance.passMouse &&
                    !rootAppearance.resetColor && !rootAppearance.resetAlpha && !rootAppearance.resetTransform &&
                    rootAppearance.maptext.isEmpty() && rootAppearance.renderSource.isEmpty() && rootAppearance.renderTarget.isEmpty() &&
                    rootAppearance.tint == std::array<float,4>{1,1,1,1} && rootAppearance.color == identityColor && rootAppearance.blend <= 1 &&
                    sprite.planeEffects && sprite.planeEffects->supported && sprite.planeEffects->filters.empty() && !sprite.planeEffects->matrix;
                sprite.passMouse = rootAppearance.passMouse;
                if (kind == MapSprite::Turf && !image && (localCapture || !sprite.renderSource.isEmpty()))
                    sprite.turfDimensions={w.bounds.maxX,w.bounds.maxY};
                sprite.groupOccurrence=joinGroup ? joinOccurrence : shape.grouped ? key+1 : 0;
                if (sprite.appearanceGroup) sprite.groupOrigin=joinGroup ? joinOrigin : origin;
                // A click on it reads the target's icon coordinates, like an overlay's (click-mask-world img-kt).
                if (image && joinGroup && !joinBottom.isNull()) sprite.iconBottom=joinBottom;
                sprite.attachmentOrder=vis ? vis->order : 0;
                if (localCapture && (rootAppearance.keepTogether || !sprite.renderTarget.isEmpty())) {
                    sprite.localGroup = key + 1;
                    sprite.renderTarget = rootAppearance.renderTarget;
                    sprite.captureOrigin = origin + shape.iconBottom - QPoint(0,tile_);
                    sprite.relayMouseBottom = origin + QPoint(0,tile_);
                } else if (!sprite.renderTarget.isEmpty() && sprite.kind != MapSprite::Master && !sprite.image.isNull()) {
                    // An overlay that is itself a render target is captured by itself: hidden when its name starts with
                    // "*", drawn where it stands otherwise, and relayed either way (stock 516.1689, stock-parity-world
                    // overlay-relays ov-target-child*).
                    sprite.localGroup = (key + 1) ^ ((uint64_t(&sprite - &instance.sprites.front()) + 1) << 44);
                    const int height = sprite.size.isEmpty() ? sprite.image.height() : sprite.size.height();
                    sprite.captureOrigin = sprite.position + QPoint(0, height - tile_);
                    sprite.relayMouseBottom = origin + QPoint(0,tile_);
                }
                orderChanged |= sprite.sortCell != sortCell;
                sprite.sortCell = sortCell;
            }
            instance.origin = origin; instance.cell = cell; instance.revision = shape.revision;
            instance.iconSize = shape.iconSize;
            instance.masterPlacementRefused = refuseMasterPlacement; instance.masterPartsOffScene = masterPartsOffScene;
            instance.screenOrder = screenOrder;
            instance.key = atom.key; instance.haveKey = atom.haveKey;
            instance.imageIdentity = imageIdentity; instance.targetIdentity = targetIdentity; instance.hudIdentity = hudIdentity;
            instance.visHit = vis ? vis->hit : nullptr;
        }
        // Animation changes images only: all cells in a DMI have identical bounds
        // and ordering. Do not detach the immutable list until a cell actually changes.
        const bool moving = !flick && atom.gliding && nowNs && atom.glide.startNs && model::glideLeft(atom.glide, nowNs) > 0;
        for (size_t index : shape.animated) {
            const auto& part = shape.parts[index];
            const bool twin = moving && part.moving;
            const auto& animation = twin ? part.moving->first : part.animation;
            auto frameKey = twin ? part.moving->second : part.frameKey;
            int frame = 0;
            if (flick) {
                // The root is one forward pass. Children keep their own state,
                // loops and rewind, but share the flick's elapsed-time origin.
                const int64_t elapsed = nowNs > std::max<int64_t>(0, flick->startNs)
                    ? nowNs - std::max<int64_t>(0, flick->startNs) : 0;
                frame = part.flickRoot ? part.animation.flickFrameAt(nowNs, flick->startNs)
                                       : part.animation.frameAt(elapsed, 0);
            } else frame = animation.frameAt(nowNs, atom.iconStartNs, w.iconClockNs);
            std::get<2>(frameKey) += std::max(0, frame) * animation.dirs;
            const auto frameImageValue = frameImage(frameKey);
            if (diagnostic) for (auto& node : diagnostic->tree.nodes)
                if (!node.assets.empty() && node.assets.front().result.id == std::get<0>(part.frameKey) && node.cell == std::get<2>(part.frameKey)) node.frame = frame;
            if (!frameImageValue) {
                if (diagnostic) diagnostic->placement = TraceReason::FrameWork;
                nextVisible_.pop_back(); return;
            } // retry the whole instance next preparation
            if (instance.sprites.at(qsizetype(index)).image.cacheKey() != frameImageValue->cacheKey()) {
                instance.sprites[qsizetype(index)].image = *frameImageValue;
                geometryChanged = true;
            }
        }
        if (diagnostic) { diagnostic->placement = TraceReason::Prepared; diagnostic->prepared = int(instance.sprites.size()); }
    };
    // An atom's particle set (model/particles.h), drawn over its own icon at its plane and layer. As the old renderer
    // measured on stock DreamSeeker 516 (not re-measured here): the set is clipped to a width x height box centred on
    // the holder's icon; a set whose first particle has no icon draws every particle as a one-pixel dot at
    // ceil(centre + x - 0.5), ceil(centre - y - 0.5); an icon particle is its state's south frame centred on
    // (round-half-even(x), -round-half-even(y)), through its rotation and scale (point sampled) and the set's transform,
    // its colour multiplied by the particle's; a set with depth draws its far particles first. Particles take no mouse.
    // A vis_contents copy of the holder draws the set too, at the copy (vg's airflow dust is a nullspace holder in a
    // turf's vis_contents); every copy shows the one simulation.
    auto placeParticles = [&](uint64_t key, uint32_t atomKey, const model::Atom& atom, const VisPlacement* vis = nullptr,
                              bool screen = false) {
        const auto found = w.particles.find(atomKey);
        if (found == w.particles.end()) return;
        model::ParticleSet& set = found->second;
        const int64_t frameNs = w.frameNanos > 0 ? w.frameNanos : w.tickNanos > 0 ? w.tickNanos : model::kDefaultTickNanos;
        model::advanceParticles(set, nowNs, frameNs, atomKey * 2654435761u ^ set.pid);
        set.drawnNs = nowNs;
        const uint64_t particleKey = (uint64_t(vis ? 6 : 7) << 60) | key;
        MapSprites sprites;
        const auto holder = instances_.find(key);
        const bool placed = holder != instances_.end() && holder->second.used == epoch_ && !holder->second.sprites.isEmpty();
        const auto& ap = appearance(atom.appid);
        if (!set.live.empty() && (placed || vis || (atom.hasPos && atom.z == g.z)) && ap.present && ap.valid) {
            const QPoint cell = vis ? vis->cell : QPoint(atom.x, atom.y);
            QPoint bottom = vis ? vis->iconBottom
                                : QPoint((atom.x - 1) * tile_, -atom.y * tile_ + tile_) + QPoint(atom.stepX, -atom.stepY) +
                                  QPoint(atom.pixelX + atom.pixelW, -atom.pixelY - atom.pixelZ) + ap.offset;
            QSize size(tile_, tile_);
            QPoint sortCell = cell;
            const float ownLayer = std::bit_cast<float>(ap.layerBits);
            int plane = ap.hasPlane ? resolvePlane(ap.plane) : 0;
            float layer = std::isfinite(ownLayer) && ownLayer >= 0 ? ownLayer : 0;
            if (placed) {
                const auto& first = holder->second.sprites.front();
                bottom = first.iconBottom;
                sortCell = first.sortCell;
                if (!holder->second.iconSize.isEmpty()) size = holder->second.iconSize;
                if (vis) { plane = first.plane; layer = first.layer; } // the copy's context plane and layer
            }
            const int iconX = bottom.x(), iconY = bottom.y() - size.height();
            const int bx = (size.width() - int(set.width)) / 2, by = (size.height() - int(set.height)) / 2;
            const int cx = iconX + (2 * bx + int(set.width)) / 2, cy = iconY + (2 * by + int(set.height)) / 2;
            const QRect clip(iconX + bx, iconY + by, set.width, set.height);
            const bool dots = set.live.front().icon == 0xffff;
            std::vector<size_t> order(set.live.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return set.live[a].pos[2] > set.live[b].pos[2]; });
            static const QImage dot = [] {
                QImage image(1, 1, QImage::Format_RGBA8888);
                image.fill(Qt::white);
                return image;
            }();
            const auto& M = set.transform;
            // The holder's colour, alpha and transform apply to its particles, on the map and on the HUD; a scaled or
            // turned particle is cut at the set's box like any other (stock 516.1689, tests/data/particle-holder-world).
            const auto& H = ap.matrix;
            const bool held = ap.hasTransform && ap.matrixValid;
            for (size_t index : order) {
                if (!budget) break;
                const model::Particle& p = set.live[index];
                const uint32_t c = p.drawColor;
                if (!(c >> 24)) continue;
                --budget;
                double x = p.pos[0], y = p.pos[1];
                const double z = p.pos[2];
                if (!dots) { x = std::nearbyint(x); y = std::nearbyint(y); }
                // The set's transform is a row-vector 4x4 in DM space (+y up), then the w divide. t is its linear
                // part in image space (+y down), q the particle's own.
                double ox = x, oy = y;
                std::array<double, 4> t{1, 0, 0, 1};
                if (set.hasTransform) {
                    const double wd = x * M[3] + y * M[7] + z * M[11] + M[15];
                    if (wd <= 0) continue;
                    ox = (x * M[0] + y * M[4] + z * M[8] + M[12]) / wd;
                    oy = (x * M[1] + y * M[5] + z * M[9] + M[13]) / wd;
                    t = {M[0] / wd, -M[4] / wd, -M[1] / wd, M[5] / wd};
                }
                if (held) {
                    const double hx = H[0] * ox + H[1] * oy + H[2], hy = H[3] * ox + H[4] * oy + H[5];
                    ox = hx; oy = hy;
                    const std::array<double, 4> h{H[0], -H[1], -H[3], H[4]};
                    t = {h[0] * t[0] + h[1] * t[2], h[0] * t[1] + h[1] * t[3], h[2] * t[0] + h[3] * t[2], h[2] * t[1] + h[3] * t[3]};
                }
                MapSprite sprite;
                sprite.kind = screen ? MapSprite::Screen : MapSprite::Entity;
                if (screen && placed) sprite.screenOrder = holder->second.sprites.front().screenOrder;
                sprite.atom = atomKey; sprite.key = atom.key; sprite.haveKey = atom.haveKey;
                sprite.cell = cell; sprite.sortCell = sortCell; sprite.iconBottom = bottom;
                sprite.plane = plane; sprite.layer = layer;
                sprite.tint = {float(c & 255) / 255, float((c >> 8) & 255) / 255, float((c >> 16) & 255) / 255, float(c >> 24) / 255};
                for (int i = 0; i < 4; ++i) sprite.tint[i] *= ap.tint[i];
                sprite.color = ap.color;
                sprite.mouseTransparent = true; sprite.inkInteractive = false; sprite.mouseOpacity = 0;
                sprite.appearanceId = atom.appid;
                if (dots) {
                    sprite.image = dot;
                    sprite.position = QPoint(int(std::ceil(cx + ox - 0.5)), int(std::ceil(cy - oy - 0.5)));
                    if (!held && !clip.contains(sprite.position)) continue;
                } else {
                    if (p.icon >= w.resources.size()) continue;
                    const auto res = builder.resource(p.icon);
                    if (!res.ready) continue;
                    const auto& icon = icons_.icon(res.id);
                    const auto* state = icon.state(QString::fromStdString(p.iconState));
                    if (!state || icon.sheet.isNull()) continue;
                    const auto image = frameImage({res.id, res.revision, icon.index(*state, 2, state->frameAt(nowNs, 0, w.iconClockNs))});
                    if (!image || image->isNull()) continue;
                    const int iw = image->width(), ih = image->height();
                    QPoint at(cx + int(ox) - (iw + 1) / 2, cy - int(oy) - (ih + 1) / 2);
                    if (set.hasTransform || held)
                        at = QPoint(int(std::floor(cx + ox - iw / 2.0 + 0.5)), int(std::floor(cy - oy - ih / 2.0 + 0.5)));
                    const std::array<double, 4> q{p.m[0], p.m[1], p.m[2], p.m[3]};
                    const std::array<double, 4> m{t[0] * q[0] + t[1] * q[2], t[0] * q[1] + t[1] * q[3],
                                                  t[2] * q[0] + t[3] * q[2], t[2] * q[1] + t[3] * q[3]};
                    sprite.image = *image;
                    sprite.position = at;
                    const QRect whole(at, image->size());
                    if (m == std::array<double, 4>{1, 0, 0, 1}) {
                        const QRect cut = held ? whole : whole.intersected(clip);
                        if (cut.isEmpty()) continue;
                        if (cut != whole) {
                            sprite.image = image->copy(cut.translated(-at));
                            sprite.position = cut.topLeft();
                        }
                    } else {
                        // Image space to the DM-space matrix StaticAffine takes.
                        sprite.affine = StaticAffine::make(std::array<double, 6>{m[0], -m[1], 0, -m[2], m[3], 0}, image->size());
                        if (!sprite.affine || (!held && !sprite.rect().intersects(clip))) continue;
                        sprite.smooth = true;  // stock filters a scaled or turned particle
                        if (!held && !clip.contains(sprite.rect())) {  // drawn here, cut at the box
                            const QRect cut = sprite.rect().intersected(clip);
                            QImage drawn(cut.size(), QImage::Format_RGBA8888);
                            drawn.fill(Qt::transparent);
                            QPainter painter(&drawn);
                            painter.setRenderHint(QPainter::SmoothPixmapTransform);
                            painter.setTransform(sprite.affine->forward * QTransform::fromTranslate(at.x() - cut.x(), at.y() - cut.y()));
                            painter.drawImage(0, 0, *image);
                            painter.end();
                            sprite.image = drawn; sprite.position = cut.topLeft(); sprite.affine.reset(); sprite.smooth = false;
                        }
                    }
                }
                sprites.push_back(std::move(sprite));
            }
        }
        const auto previous = instances_.find(particleKey);
        if (sprites.isEmpty()) {
            if (previous != instances_.end() && previous->second.used + 1 == epoch_) orderChanged = geometryChanged = true;
            return;
        }
        auto& instance = instances_[particleKey];
        if (instance.used + 1 != epoch_ || instance.sprites != sprites) orderChanged = geometryChanged = true;
        instance.used = epoch_;
        instance.sprites = std::move(sprites);
        instance.cell = QPoint(atom.x, atom.y);
        instance.key = atom.key; instance.haveKey = atom.haveKey;
        nextVisible_.push_back(particleKey);
    };
    if (scene && mapName_.isEmpty()) {
        TurfColumnLookup tiles(w.tiles, g.region.width());
        TurfColumnLookup tileTiming(w.tileIconStart, g.region.width());
        TurfColumnLookup tileRevision(w.tileRevision, g.region.width());
        for (int y = g.region.y(); y < g.region.y() + g.region.height(); ++y)
            for (int x = g.region.x(); x < g.region.x() + g.region.width(); ++x) {
                if (x < 0 || y < 0 || x >= w.bounds.maxX || y >= w.bounds.maxY) continue;
                const auto* found = tiles.find(x - g.region.left(), {uint16_t(x), uint16_t(y), uint16_t(g.z)});
                if (!found) continue;
                model::Atom atom; atom.hasPos = true; atom.x = x + 1; atom.y = y + 1; atom.z = g.z; atom.appid = *found;
                if (const auto* timing = tileTiming.find(x - g.region.left(), {uint16_t(x), uint16_t(y), uint16_t(g.z)}))
                    atom.iconStartNs = timing->ns;
                if (const auto* revision = tileRevision.find(x - g.region.left(), {uint16_t(x), uint16_t(y), uint16_t(g.z)}))
                    atom.renderIdentity = *revision;
                if (!w.turfChains.empty())
                    if (const auto chain = w.turfChains.find({uint16_t(x), uint16_t(y), uint16_t(g.z)}); chain != w.turfChains.end())
                        atom.chain = chain->second;
                place((uint64_t(1) << 32) | (uint32_t(y) << 16) | uint32_t(x), atom, WorldPlacement::Turf);
            }
    }
    // The client keeps every atom, turf vis_contents and image it has been sent, on every level. Only those near the
    // view may use the frame's candidate and visit budgets, or far ones starve visible turf vis_contents (vg's ZAS gas
    // overlays) and the HUD, which are placed after them. 16 tiles of margin keep large icons and offsets.
    const QRect nearView = g.region.adjusted(-16, -16, 16, 16);
    auto near = [&](bool hasPos, int x, int y, int z) { // x, y one-based
        return hasPos && z == g.z && nearView.contains(x - 1, y - 1);
    };
    if (mapName_.isEmpty()) for (const auto& [key, atom] : w.atoms) {
        if (!budget || !candidates) break;
        if (!near(atom.hasPos, atom.x, atom.y, atom.z) && !(trace_ && key == trace_->key)) continue;
        if (!w.resync.freed.count(key) && !worldOverrides.count(key)) {
            place(key, atom, WorldPlacement::Entity);
            placeParticles(key, key, atom);
        } else --candidates;
    }
    if (trace_ && trace_->map.placement == TraceReason::NotVisited && (!budget || !candidates))
        trace_->map.placement = TraceReason::SceneWork;
    // One bounded forest walker for world/turf/image roots and each HUD
    // occurrence. It reserves every template/playback dependency before publishing.
    uint64_t visDiscovery=maxSceneVisits;
    // What stands on each turf, built once per frame and only when a turf is a vis_contents member.
    std::unordered_map<uint64_t,std::vector<uint32_t>> turfContents; bool turfContentsReady=false;
    auto contentsOf=[&](uint64_t turf) -> const std::vector<uint32_t>* {
        if (!turfContentsReady) {
            turfContentsReady=true;
            for (const auto& [key,atom]:w.atoms)
                if (atom.hasPos && atom.x>=1 && atom.y>=1 && !w.resync.freed.count(key))
                    turfContents[model::VisContents::turf(uint16_t(atom.x-1),uint16_t(atom.y-1),uint16_t(atom.z))].push_back(key);
        }
        const auto found=turfContents.find(turf);
        return found==turfContents.end() ? nullptr : &found->second;
    };
    auto emitVis = [&](uint64_t holderKey, const model::Atom& root, const Appearance& rootAp,
                       QPoint rootOrigin, bool screen, uint32_t screenOrder, uint64_t repetition=0) {
        if (!rootAp.present || !rootAp.valid || rootAp.master || !rootAp.matrixValid ||
            rootAp.keepApart ||
            !rootAp.maptext.isEmpty() || !rootAp.renderSource.isEmpty() || !rootAp.renderTarget.isEmpty() ||
            !rootAp.colorValid || rootAp.blend>4 || rootAp.invisibility>seeInvisible_ ||
            root.stepX || root.stepY || sampler.unsettled(root.chain)) return;
        const auto membership=w.vis.lists.find(holderKey);
        if (membership==w.vis.lists.end() || membership->second.holderIdentity!=root.renderIdentity) return;
        struct Planned { uint64_t key; const model::Atom* atom; VisPlacement placement; };
        std::vector<Planned> planned;
        std::deque<Template> samples; // stable addresses through recursion/publication
        std::vector<uint64_t> path{holderKey};
        std::vector<size_t> parents; // planned index of each member on the path
        std::vector<uint64_t> occurrencePath{holderKey,root.renderIdentity,uint64_t(screen),repetition};
        VisHit hit;
        if (screen) {
            const auto hud=w.hudIdentity.find(uint32_t(holderKey));
            if (hud==w.hudIdentity.end() || !w.hudObjectCurrent(uint32_t(holderKey),root.key)) return;
            hit.hudRoot=uint32_t(holderKey);hit.hudKey=root.key;hit.hudMembership=hud->second.membership;
            occurrencePath.push_back(hit.hudMembership);
        }
        // A transformed holder lends its matrix to every copy, about each copy's own centre; offsets are not transformed
        // (stock 516.1688, tests/data/vis-world set c), as for overlays.
        VisContext context{rootAp.hasPlane ? resolvePlane(rootAp.plane) : 0,rootAp.layerBits,rootAp.tint,rootAp.color,rootAp.blend,
                           rootAp.matrix};
        // A KEEP_TOGETHER holder composes its copies in its own surface, which carries its colour, alpha and matrix
        // (stock 516.1689). A holder that only has filters keeps them outside.
        const uint64_t joinKey=!rootAp.keepTogether ? 0 :
            (screen ? (repetition<<40)|(uint64_t(2)<<32)|uint32_t(holderKey) : holderKey)+1;
        if (joinKey) context={std::get<0>(context),rootAp.layerBits,{1,1,1,1},identityColor,0,{1,0,0,0,1,0}};
        bool complete=true; uint64_t cost=0;
        // The holder's effective icon, state and direction, for VIS_INHERIT_ICON/ICON_STATE/DIR.
        struct Look { int dir; uint32_t icon; std::string state; };
        const Look rootLook{root.dir ? root.dir : rootAp.dir ? rootAp.dir : 2,rootAp.icon,rootAp.state};
        // One member of a vis forest. Turf members draw the turf, then its contents and its own vis_contents, each by
        // its own layer (stock 516.1688, tests/data/vis-world sets a and d); the contents take no vis_flags.
        struct Child {
            uint64_t key, membership; uint32_t index;
            const model::Atom* atom; uint32_t target; uint64_t identity;
            bool turf, flags;
        };
        std::deque<model::Atom> turfAtoms; // stable addresses for planned turf copies
        std::function<void(uint64_t,QPoint,const VisContext&,const Look&)> walk;
        std::function<void(uint64_t,QPoint,const VisContext&,const Look&,const Child&)> visit;
        visit=[&](uint64_t parent,QPoint origin,const VisContext& parentContext,const Look& look,const Child& c) {
            if (!visDiscovery || path.size()>=16 || planned.size()>=4096 ||
                std::find(path.begin(),path.end(),c.key)!=path.end()) {complete=false;return;}
            --visDiscovery;
            const auto& atom=*c.atom;
            Appearance sampled; bool composing=false;
            if (!c.turf && (!atom.chain.steps.empty() || atom.chain.rejected) &&
                !sampler.sample(atom.chain,atom.appid,sampled,composing,false,true)) {complete=false;return;}
            const auto& ap=composing ? sampled : appearance(atom.appid);
            const uint16_t flags=c.flags ? ap.visFlags : 0;
            const auto* contents=c.turf ? contentsOf(c.key) : nullptr;
            const bool children=w.vis.lists.count(c.key)!=0 || (contents && !contents->empty());
            const bool parentSpecial=(parent & (model::VisContents::turfBit|model::VisContents::imageBit))!=0;
            if (!ap.present || !ap.valid || atom.stepX || atom.stepY ||
                !ap.screenLoc.isEmpty() || !std::isfinite(std::bit_cast<float>(ap.layerBits)) ||
                (children && (ap.keepTogether || ap.keepApart)) || !ap.matrixValid || (flags & ~0xffu) ||
                ((flags & 0x20) && (path.size()!=1 || children || c.turf))) {complete=false;return;}
            if (flags & 0x80) return; // VIS_HIDE: neither the member nor anything under it
            VisInherit inherit{};
            if (flags & 0x1b) inherit={uint16_t(flags & 0x1b),flags & 1 ? look.icon : 0,flags & 2 ? look.state : std::string()};
            const int dir=flags & 4 ? look.dir : atom.dir;
            const Template* shape;
            if (composing) {
                samples.push_back(builder.sample({atom.appid,dir,VisRequest{parentContext,inherit}},sampled));
                shape=&samples.back();
            } else shape=&builder.get({atom.appid,dir,VisRequest{parentContext,inherit}});
            if (shape->visits>budget-cost) {cost=budget;complete=false;return;}
            cost+=shape->visits;
            if (!shape->complete) {complete=false;return;}
            if (ap.invisibility>seeInvisible_) return;
            const auto settled=w.appearances.find(atom.appid);
            const bool endpointPixels=!atom.chain.steps.empty() && settled!=w.appearances.end() &&
                settled->second.fields.hasPixelOffsets && (composing || atom.appid==atom.chain.steps.back().to);
            const QPoint at=origin+(endpointPixels || c.turf ? QPoint() : QPoint(atom.pixelX+atom.pixelW,-atom.pixelY-atom.pixelZ));
            const bool inheritId=(flags & 0x20)!=0;
            hit.path.push_back({parent,c.membership,c.index,c.target,c.identity});
            // VIS_INHERIT_ID names the holder: an atom, or the turf itself.
            const bool inheritTurf=inheritId && (parent & model::VisContents::turfBit);
            const uint64_t turfKey=inheritTurf ? parent : c.key;
            hit.turf=c.turf || inheritTurf;
            hit.cell=hit.turf ? std::array<uint16_t,3>{uint16_t(turfKey),uint16_t(turfKey>>16),uint16_t(turfKey>>32)} :
                                std::array<uint16_t,3>{};
            hit.turfId=hit.turf ? (uint32_t(hit.cell[2])*uint32_t(w.bounds.maxY)+hit.cell[1])*uint32_t(w.bounds.maxX)+hit.cell[0] : 0;
            // In an image's vis_contents it is drawn too (stock 516.1689, stock-parity-world sweep image-vis) and
            // named here by the image's target, which was not probed.
            const uint32_t named=inheritId && (parent & model::VisContents::imageBit) ?
                w.imageInstances.at(uint32_t(parent)).target : uint32_t(parent);
            hit.target=inheritTurf ? 0 : inheritId ? named : c.target;
            const auto holderRevision=inheritTurf ? w.tileRevision.find(hit.cell) : w.tileRevision.end();
            hit.targetIdentity=inheritTurf ? (holderRevision!=w.tileRevision.end() ? holderRevision->second : 0) :
                               inheritId ? w.atoms.at(named).renderIdentity : c.identity;
            occurrencePath.push_back(c.membership);occurrencePath.push_back(c.membership ? c.index : c.key);
            auto& occurrence=occurrences_[occurrencePath];
            if (!occurrence.id) occurrence.id=(uint64_t(4)<<60)|++nextOccurrence_;
            occurrence.used=epoch_;
            if (!occurrence.hit || *occurrence.hit!=hit) occurrence.hit=std::make_shared<const VisHit>(hit);
            const QPoint bottom=inheritId ? origin+QPoint(0,tile_) : at+shape->iconBottom;
            // A VIS_UNDERLAY member of another member draws just before that member; one of the holder ranks before
            // the holder (stock 516.1688, vis-world sets b and e).
            const bool nestedUnderlay=(flags & 0x40) && !parents.empty();
            const size_t slot=nestedUnderlay ? parents.back() : planned.size();
            planned.insert(planned.begin()+std::ptrdiff_t(slot),{occurrence.id,&atom,{shape,at,screen ? QPoint() : QPoint(root.x,root.y),
                bottom,occurrence.hit,0,screen,screenOrder}});
            for (auto& p:parents) if (p>=slot) ++p;
            auto& placed=planned[slot].placement;
            placed.joinKey=joinKey;
            placed.underlay=!(flags & 0x40) || nestedUnderlay ? 0 : (holderKey & model::VisContents::turfBit) ? 2 : 1;
            placed.holderOrigin=rootOrigin;
            if (atom.hasPos) placed.sourceCell=QPoint(atom.x,atom.y);
            if (nestedUnderlay) {
                const auto& parent=planned[parents.back()].placement;
                placed.sourceCell=parent.sourceCell;
                placed.parentFoot=parent.parentFoot ? parent.parentFoot :
                    std::optional<QPoint>(parent.origin+parent.shape->iconBottom-QPoint(0,tile_));
            }
            auto nextContext=parentContext;
            if (!(flags & 0x10)) std::get<0>(nextContext)=resolvePlane(ap.hasPlane ? ap.plane : 0,std::get<0>(parentContext));
            if (!(flags & 8) && std::bit_cast<float>(ap.layerBits)>=0) std::get<1>(nextContext)=ap.layerBits;
            auto tint=std::get<2>(parentContext);auto color=std::get<3>(parentContext);
            if (ap.resetColor) {
                tint[0]=tint[1]=tint[2]=1;
                for (int k=0;k<5;++k) for (int r=0;r<3;++r) color[4*k+r]=identityColor[4*k+r];
            }
            if (ap.resetAlpha) {tint[3]=1;for (int k=0;k<5;++k) color[4*k+3]=identityColor[4*k+3];}
            if (color!=identityColor || ap.color!=identityColor) {
                color=composeColor(tintColor(color,tint),tintColor(ap.color,ap.tint));tint={1,1,1,1};
            } else for (int i=0;i<4;++i) tint[i]*=ap.tint[i];
            std::get<2>(nextContext)=tint;std::get<3>(nextContext)=color;
            if (ap.blend) std::get<4>(nextContext)=ap.blend;
            std::get<5>(nextContext)=ap.resetTransform ? ap.matrix : animation::multiply(std::get<5>(parentContext),ap.matrix);
            const Look nextLook{flags & 4 ? look.dir : atom.dir ? atom.dir : ap.dir ? ap.dir : 2,
                                flags & 1 ? look.icon : ap.icon,flags & 2 ? look.state : ap.state};
            path.push_back(c.key); parents.push_back(slot);
            if (contents) for (size_t i=0;i<contents->size() && complete;++i) {
                const uint32_t key=(*contents)[i];
                const auto found=w.atoms.find(key);
                if (found==w.atoms.end()) continue;
                visit(c.key,at+ap.offset,nextContext,nextLook,
                      {key,0,uint32_t(i),&found->second,key,found->second.renderIdentity,false,false});
            }
            if (complete) walk(c.key,at+ap.offset,nextContext,nextLook);
            path.pop_back();parents.pop_back();occurrencePath.resize(occurrencePath.size()-2);hit.path.pop_back();
        };
        walk=[&](uint64_t parent,QPoint origin,const VisContext& parentContext,const Look& look) {
            const auto list=w.vis.lists.find(parent);
            if (list==w.vis.lists.end()) return;
            const bool special=(parent & (model::VisContents::turfBit|model::VisContents::imageBit))!=0;
            const auto parentAtom=w.atoms.find(uint32_t(parent));
            // A turf parent is the holder or a turf member; an image parent only ever holds the forest.
            if ((!special && (parentAtom==w.atoms.end() || parentAtom->second.renderIdentity!=list->second.holderIdentity)) ||
                ((parent & model::VisContents::imageBit) && parent!=holderKey)) {complete=false;return;}
            for (size_t index=0;index<list->second.members.size() && complete;++index) {
                const auto& member=list->second.members[index];
                if (member.reference.kind==1) {
                    const int mx=w.bounds.maxX,my=w.bounds.maxY;
                    if (mx<=0 || my<=0) {complete=false;break;}
                    const uint32_t flat=member.reference.ref;
                    const std::array<uint16_t,3> cell{uint16_t(flat%uint32_t(mx)),uint16_t(flat/uint32_t(mx)%uint32_t(my)),
                                                      uint16_t(flat/(uint32_t(mx)*uint32_t(my)))};
                    const auto tile=w.tiles.find(cell);
                    if (tile==w.tiles.end()) {complete=false;break;}
                    auto& turf=turfAtoms.emplace_back();
                    turf.hasPos=true;turf.x=cell[0]+1;turf.y=cell[1]+1;turf.z=cell[2];turf.appid=tile->second;
                    if (const auto timing=w.tileIconStart.find(cell);timing!=w.tileIconStart.end()) turf.iconStartNs=timing->second.ns;
                    if (const auto revision=w.tileRevision.find(cell);revision!=w.tileRevision.end()) turf.renderIdentity=revision->second;
                    visit(parent,origin,parentContext,look,{model::VisContents::turf(cell[0],cell[1],cell[2]),
                          list->second.identity,uint32_t(index),&turf,0,turf.renderIdentity,true,true});
                    continue;
                }
                if (!model::VisContents::movable(member.reference)) {complete=false;break;}
                const uint32_t child=model::VisContents::atom(member.reference);
                const auto found=w.atoms.find(child);
                if (found==w.atoms.end() || !member.childIdentity || w.resync.freed.count(child) ||
                    found->second.renderIdentity!=member.childIdentity) {complete=false;break;}
                visit(parent,origin,parentContext,look,
                      {child,list->second.identity,uint32_t(index),&found->second,child,member.childIdentity,false,true});
            }
        };
        walk(holderKey,rootOrigin+rootAp.offset,context,rootLook);
        budget-=cost;
        if (!complete || planned.size()>candidates) return;
        for (size_t i=0;i<planned.size();++i) planned[i].placement.order=uint32_t(i+1);
        for (const auto& item:planned) for (size_t index:item.placement.shape->animated) {
            const auto& part=item.placement.shape->parts[index];auto key=part.frameKey;
            std::get<2>(key)+=part.animation.frameAt(nowNs,item.atom->iconStartNs,w.iconClockNs)*part.animation.dirs;
            if (!frameImage(key)) complete=false;
        }
        if (complete) for (const auto& item:planned) {
            place(item.key,*item.atom,std::cref(item.placement));
            if (!item.placement.hit->turf) placeParticles(item.key,item.placement.hit->target,*item.atom,&item.placement);
        }
    };
    if (scene && mapName_.isEmpty()) for (const auto& [holderKey,membership]:w.vis.lists) {
        if (!visDiscovery || !candidates || !budget) break;
        const bool turfHolder=(holderKey & model::VisContents::turfBit)!=0;
        const bool imageHolder=(holderKey & model::VisContents::imageBit)!=0;
        if (turfHolder && !near(true,int(uint16_t(holderKey))+1,int(uint16_t(holderKey>>16))+1,int(uint16_t(holderKey>>32)))) continue;
        if (!turfHolder && !imageHolder) {
            const auto holder=w.atoms.find(uint32_t(holderKey));
            if (holder!=w.atoms.end() && !near(holder->second.hasPos,holder->second.x,holder->second.y,holder->second.z)) continue;
        }
        --visDiscovery;
        model::Atom root;
        Appearance neutral;neutral.present=neutral.valid=true;neutral.layerBits=0;
        if (turfHolder) {
            const uint16_t x=uint16_t(holderKey),y=uint16_t(holderKey>>16),z=uint16_t(holderKey>>32);
            const auto tile=w.tiles.find({x,y,z});
            if (tile==w.tiles.end()) continue;
            const auto& ap=appearance(tile->second);
            if (!ap.present || !ap.valid || ap.imageUnsupported || ap.keepApart || ap.invisibility>seeInvisible_) continue;
            // Copies at FLOAT_LAYER or inheriting the layer take the turf's layer and plane (vis-world set e turf-float).
            neutral.layerBits=ap.layerBits; neutral.hasPlane=ap.hasPlane; neutral.plane=ap.plane;
            root.hasPos=true;root.x=x+1;root.y=y+1;root.z=z;root.renderIdentity=membership.holderIdentity;
        } else if (imageHolder) {
            const uint32_t id=uint32_t(holderKey);const auto im=w.imageInstances.find(id);
            if (im==w.imageInstances.end() || im->second.targetHud || im->second.identity!=membership.holderIdentity ||
                !w.imageTargetCurrent(id,im->second.identity,im->second.target,im->second.targetIdentity)) continue;
            root=w.atoms.at(im->second.target);
            const auto& ap=appearance(root.appid);
            if (sampler.unsettled(root.chain) || ap.imageUnsupported || ap.keepApart) continue;
            root.renderIdentity=membership.holderIdentity;
        } else {
            const auto holder=w.atoms.find(uint32_t(holderKey));
            if (holder==w.atoms.end() || w.resync.freed.count(uint32_t(holderKey))) continue;
            root=holder->second;
        }
        if (!root.hasPos || root.z!=g.z || root.x<1 || root.y<1 || root.x>w.bounds.maxX || root.y>w.bounds.maxY) continue;
        // An animating holder lends its sampled appearance (offset, tint) to its
        // vis copies, like ordinary placement. A transformed holder stays refused
        // in emitVis: vis-copy matrix inheritance is not established.
        Appearance sampledHolder; bool holderComposing=false;
        if (!turfHolder && !imageHolder && (!root.chain.steps.empty() || root.chain.rejected)) {
            if (!sampler.sample(root.chain,root.appid,sampledHolder,holderComposing,false,appearance(root.appid).keepTogether)) continue;
            root.chain={}; // sampled here; emitVis sees a settled holder
        }
        const auto& ap=turfHolder || imageHolder ? neutral : holderComposing ? sampledHolder : appearance(root.appid);
        QPoint origin((root.x-1)*tile_,-root.y*tile_);
        const auto holderSettled=holderComposing ? w.appearances.find(root.appid) : w.appearances.end();
        if (holderSettled==w.appearances.end() || !holderSettled->second.fields.hasPixelOffsets) // sampled offset replaces them
            origin+=QPoint(root.pixelX+root.pixelW,-root.pixelY-root.pixelZ);
        if (root.gliding && nowNs && root.glide.startNs) {
            const double left=model::glideLeft(root.glide,nowNs);
            origin+=QPoint(int(std::lround(root.glide.sx*tile_*left)),-int(std::lround(root.glide.sy*tile_*left)));
        }
        emitVis(holderKey,root,ap,origin,false,0);
    }
    if (scene && mapName_.isEmpty()) for (const auto& [id, image] : w.imageInstances) {
        if (!budget || !candidates) break;
        if (image.locKind == 7 ? !near(true, image.turfCell[0]+1, image.turfCell[1]+1, image.turfCell[2]) :
            !image.targetHud && [&] { const auto t = w.atoms.find(image.target);
                                      return t != w.atoms.end() && !near(t->second.hasPos, t->second.x, t->second.y, t->second.z); }())
            continue;
        --candidates;
        if (image.locKind == 7) {
            if (!w.imageTurfCurrent(id,image.identity,image.turfCell)) continue;
            model::Atom atom; atom.hasPos = true; atom.x = image.turfCell[0]+1; atom.y = image.turfCell[1]+1;
            atom.z = image.turfCell[2]; atom.appid = image.appid;
            const auto timing = w.imageIconStart.find(id);
            if (timing != w.imageIconStart.end()) atom.iconStartNs = timing->second.ns;
            place((uint64_t(3) << 32) | id, atom, ImagePlacement{image, WorldPlacement::Turf});
            continue;
        }
        if (image.targetHud ||
            !w.imageTargetCurrent(id, image.identity, image.target, image.targetIdentity)) continue;
        const auto& target = w.atoms.at(image.target);
        model::Atom atom; // copy placement only, never target animation chains/flicks
        atom.hasPos = target.hasPos; atom.x = target.x; atom.y = target.y; atom.z = target.z;
        atom.pixelX = target.pixelX; atom.pixelY = target.pixelY; atom.pixelW = target.pixelW; atom.pixelZ = target.pixelZ;
        atom.stepX = target.stepX; atom.stepY = target.stepY;
        atom.gliding = target.gliding; atom.glide = target.glide;
        atom.key = target.key; atom.haveKey = target.haveKey; atom.appid = image.appid; atom.dir = target.dir;
        const auto timing = w.imageIconStart.find(id);
        if (timing != w.imageIconStart.end()) atom.iconStartNs = timing->second.ns;
        place((uint64_t(3) << 32) | id, atom, ImagePlacement{image, WorldPlacement::Entity});
    }
    uint32_t screenOrder = 0;
    if (scene) for (uint32_t key : w.hudOrder) {
        if (!budget || !candidates) break;
        --candidates;
        ++screenOrder;
        const auto found = w.hudAppid.find(key);
        if (found == w.hudAppid.end()) continue;
        model::Atom atom; atom.appid = found->second;
        const auto& ap = appearance(atom.appid);
        // Route before master selection, template expansion and attached images/
        // vis. Invalid coordinates may still select a LOCAL leaf master, but a
        // different MAP never may, even when its placement is unsupported.
        if (!ap.screenTarget || ap.screenTarget->map != mapName_) continue;
        // A plane master without a screen_loc, or with one that is not coordinates, is no master: its plane draws as
        // if it were absent, with none of its filters, colour, alpha or overlays, and without a screen_loc no render
        // target (stock 516.1689, master-screen-loc-world; "bogus" is the only such text that was tried).
        if (ap.master && (ap.screenTarget->location.isEmpty() || ap.screenMalformed)) continue;
        const auto object = w.atoms.find(key);
        const auto* chain = object != w.atoms.end() && (!object->second.chain.steps.empty() || object->second.chain.rejected) ?
            &object->second.chain : nullptr;
        // Screen objects animate on the same clock as world atoms (stock 516.1688, tests/data/hud-anim-world).
        const auto identity = w.hudIdentity.find(key);
        if (identity != w.hudIdentity.end()) {
            if (identity->second.kind != 2) continue; // only verified object HUD records
            atom.key = identity->second.key; atom.haveKey = true;
            atom.renderIdentity = identity->second.membership;
        }
        const auto timing = w.hudIconStart.find(key);
        if (timing != w.hudIconStart.end()) atom.iconStartNs = timing->second.ns;
        // A master selects its planes once, even when placement is invalid.
        // place() resolves its single child anchor or publishes refusal markers;
        // it must not repeat plane selection for an unverified screen_loc range.
        // A master with a range acts once on its plane and draws what it owns at every position (stock 516.1689,
        // master-screen-loc-world "1,1 to 3,3"): the first placement carries the master, the others only its parts.
        const bool masterRange = ap.master && ap.screenPlacement && ap.screenPlacement->range;
        auto positions = ap.master && !masterRange ? std::vector<QPoint>{{}} : ap.screenPlacement ?
            ap.screenPlacement->positions({inner.width() / tile_, inner.height() / tile_}, tile_, around) : std::vector<QPoint>{};
        const bool placedRange = masterRange && !positions.empty();
        if (ap.master && positions.empty()) positions = {{}}; // a range off the scene or not placed: left to place()
        // What the next frame's scene must hold (TurfMap::prepare). A plane master off the view enlarges the map
        // like any other screen object (stock 516.1689, master-screen-loc-world "8,1|ov" against "8,1|obj").
        if (ap.screenPlacement) {
            const QSize icon = builder.get({atom.appid, 0}).iconSize;
            const auto m = ap.screenPlacement->outside({inner.width() / tile_, inner.height() / tile_},
                                                       {std::max(1, (icon.width() + tile_ - 1) / tile_), std::max(1, (icon.height() + tile_ - 1) / tile_)});
            wanted = QMargins(std::max(wanted.left(), m.left()), std::max(wanted.top(), m.top()),
                              std::max(wanted.right(), m.right()), std::max(wanted.bottom(), m.bottom()));
        }
        if (positions.empty()) continue;
        const auto& shape = builder.get({atom.appid, 0});
        if (positions.size() * shape.visits > budget || positions.size() > candidates) { budget = 0; break; }
        // Stock edge alignment uses the root icon, even when an overlay extends
        // beyond it. Compass/CENTER anchors remain tile based. Unequal-icon
        // ranges and plane masters are not part of this single-root extension.
        QPoint aligned;
        if (!ap.master && ap.screenPlacement && !ap.screenPlacement->range && !shape.iconSize.isEmpty()) {
            const auto& anchor = ap.screenPlacement->first;
            if (anchor.x.anchor == ScreenLoc::Anchor::FarEdge) aligned.rx() = tile_ - shape.iconSize.width();
            if (anchor.y.anchor == ScreenLoc::Anchor::FarEdge) aligned.ry() = tile_ - shape.iconSize.height();
        }
        for (size_t i = 0; i < positions.size(); ++i) {
            const QPoint position = positions[i] + aligned;
            if (!hudOverrides.count(key))
                place((uint64_t(i) << 40) | (uint64_t(2) << 32) | key, atom, HudPlacement{position, screenOrder, chain, placedRange, placedRange && i > 0});
            placeParticles((uint64_t(i) << 40) | (uint64_t(2) << 32) | key, key, atom, nullptr, true);
            if (object!=w.atoms.end() && w.vis.lists.count(key)) {
                auto root=object->second; root.appid=atom.appid;root.key=atom.key;root.haveKey=atom.haveKey;
                const QPoint origin(inner.left()+position.x(),inner.bottom()+1-tile_-position.y());
                emitVis(key,root,ap,origin,true,screenOrder,i);
            }
            const auto attached = hudImages.find(key);
            if (attached == hudImages.end()) continue;
            for (uint32_t id : attached->second) {
                if (!budget || !candidates) break;
                const auto& im = w.imageInstances.at(id);
                auto child = atom; child.appid = im.appid;
                const auto imageStart = w.imageIconStart.find(id);
                child.iconStartNs = imageStart == w.imageIconStart.end() ? 0 : imageStart->second.ns;
                place((uint64_t(5) << 60) | (uint64_t(i) << 32) | id, child, ImagePlacement{im, HudPlacement{position, screenOrder}});
            }
        }
    }
    // The float32 cell tie-break is viewport-relative, including threshold rounding.
    orderChanged |= visible_ != nextVisible_;
    if (!orderChanged && view_ != view) {
        // Screen keys do not depend on the viewport; one world part cannot reorder.
        orderChanged = std::count_if(order_.begin(),order_.end(),[](const Draw& d) {
            const auto kind = d.instance->sprites[d.part].kind;
            return kind != MapSprite::Screen && kind != MapSprite::Master;
        }) > 1;
    }
    visible_.swap(nextVisible_);
    hudMargins_ = wanted;
    const auto sortStart = stats ? Clock::now() : Clock::time_point{};
    sortOccurrences(orderChanged, view, stats);
    const auto assembleStart = stats ? Clock::now() : Clock::time_point{};
    const bool blacknessActive = scene && g.visibility && g.visibility->hasBlackness() && g.visibility->blacknessReady;
    const std::vector<QRect> noBlackness;
    assembleOutput(orderChanged || geometryChanged, translation, view.united(worldView), blacknessActive,
                   blacknessActive ? g.visibility->blackness : noBlackness, stats);
    const auto pruneStart = stats ? Clock::now() : Clock::time_point{};
    prune();
    if (stats) {
        stats->resourceReads = icons_.reads();
        stats->maptextRasters = maptext_.rasterizations();
        stats->traverseMs = std::chrono::duration<double, std::milli>(sortStart - start).count();
        stats->sortMs = std::chrono::duration<double, std::milli>(assembleStart - sortStart).count();
        stats->assembleMs = std::chrono::duration<double, std::milli>(pruneStart - assembleStart).count();
        stats->pruneMs = std::chrono::duration<double, std::milli>(Clock::now() - pruneStart).count();
    }
    // client.color (stock 516.1689, stock-parity-world client-color*) tints what the map draws, HUD included,
    // except what has NO_CLIENT_COLOR: an icon once; a group or an atom with filters on its surface, after its own
    // colour and filters; a relay of an atom's render target twice (the target is drawn tinted in stock); and a plane
    // master's surface once more when the master has filters.
    MapSprites drawn = output_;
    if (turn.turned()) {
        const QRect world = worldInner.translated(translation);
        for (auto& sprite : drawn) {
            if (sprite.kind == MapSprite::Screen || sprite.kind == MapSprite::Master || sprite.hudIdentity) continue;
            const int height = sprite.image.isNull() || !sprite.size.isEmpty() ? sprite.size.height() : sprite.image.height();
            if (sprite.kind == MapSprite::Blackness) {  // a run of tiles, not an icon
                const QRect run = QRect(turn.point(sprite.position, world),
                                        turn.point(sprite.position + QPoint(sprite.size.width(), height), world)).normalized();
                sprite.position = run.topLeft(); sprite.size = run.size() - QSize(1, 1);
                continue;
            }
            auto corner = [&](QPoint bottomLeft) { return turn.bottomLeft(bottomLeft, world, tile_); };
            auto box = [&](QPoint topLeft) { return corner(topLeft + QPoint(0, tile_)) - QPoint(0, tile_); };
            sprite.position = corner(sprite.position + QPoint(0, height)) - QPoint(0, height);
            sprite.iconBottom = corner(sprite.iconBottom);
            if (sprite.appearanceGroup) sprite.groupOrigin = box(sprite.groupOrigin);
            if (sprite.localGroup) { sprite.captureOrigin = box(sprite.captureOrigin); sprite.relayMouseBottom = corner(sprite.relayMouseBottom); }
        }
    }
    const ColorMatrix tint = clientColor(w, nowNs);
    if (tint == identityColor) return drawn;
    if (tint != clientTint_ || clientTintGroups_.size() > 4096) { clientTintGroups_.clear(); clientTint_ = tint; }
    auto recolour = [&](ColorMatrix& color, std::array<float, 4>& own) {
        color = composeColor(tint, tintColor(color, own));
        own = {1, 1, 1, 1};
    };
    auto surface = [&](auto&& self, const std::shared_ptr<const AppearanceGroup>& group,
                       const AppearanceGroup* outermost) -> std::shared_ptr<const AppearanceGroup> {
        if (!group) return nullptr;
        auto& entry = clientTintGroups_[group.get()];
        if (!entry.second) {
            auto copy = std::make_shared<AppearanceGroup>(*group);
            copy->parent = self(self, group->parent, outermost);
            if (group.get() == outermost) recolour(copy->color, copy->tint);
            entry = {group, std::move(copy)};
        }
        return entry.second;
    };
    MapSprites tinted = drawn;
    for (auto& sprite : tinted) {
        if (sprite.kind == MapSprite::Blackness || !sprite.renderTarget.isEmpty() ||
            appearance(sprite.appearanceId).noClientColor) continue;
        if (sprite.kind == MapSprite::Master) {
            if (sprite.planeEffects && sprite.planeEffects->supported && !sprite.planeEffects->filters.empty())
                recolour(sprite.color, sprite.tint);
            continue;
        }
        if (!sprite.renderSource.isEmpty()) {  // a plane's surface is not tinted again; an atom's target is, twice
            if (atomTarget(sprite.renderSource)) {  // the tinted target, the relay's own colour, the tint again
                sprite.color = composeColor(tint, composeColor(tintColor(sprite.color, sprite.tint), tint));
                sprite.tint = {1, 1, 1, 1};
            }
            continue;
        }
        const AppearanceGroup* outermost = nullptr;  // a group or a filtered atom: after its own colour and filters
        for (auto g = sprite.appearanceGroup.get(); g; g = g->parent.get()) outermost = g;
        if (outermost) sprite.appearanceGroup = surface(surface, sprite.appearanceGroup, outermost);
        else recolour(sprite.color, sprite.tint);
    }
    return tinted;
}

void EntityMap::sortOccurrences(bool changed, const QRect& view, Stats* stats) {
    if (changed) {
        order_.clear();
        for (uint64_t key : visible_) {
            const auto& instance = instances_.at(key);
            for (qsizetype i = 0; i < instance.sprites.size(); ++i) order_.push_back({&instance, i});
        }
        // Sort references, not QImages. Gliding within a cell and camera-only frames
        // preserve order; equal keys preserve underlay/base/overlay traversal order.
        const int originX = int(std::floor(double(view.x()) / tile_));
        const int originY = int(std::floor(double(-view.bottom()-1) / tile_));
        const auto keyFor = [&](const MapSprite& s) {
            const bool screen = s.kind == MapSprite::Screen || s.kind == MapSprite::Master;
            return drawKey(s.layer, screen, screen ? -1 :
                32 * (s.sortCell.y()-originY) + s.sortCell.x()-originX + 33);
        };
        std::stable_sort(order_.begin(), order_.end(), [&](const Draw& left, const Draw& right) {
            const auto& a = left.instance->sprites[left.part];
            const auto& b = right.instance->sprites[right.part];
            if (a.plane != b.plane) return a.plane < b.plane;
            const auto ak = keyFor(a), bk = keyFor(b);
            if (ak != bk) return ak < bk;
            // Equal keys use south/west-first tile traversal; within a tile ordinary tree, attachment and creation order.
            // BLEND_ADD takes no turn of its own: an additive atom or overlay made before an opaque atom on its tile is
            // drawn under it, and one occurrence's additive underlay stays under its opaque parent (stock 516.1689,
            // stock-parity-world kt-add-under-flat, relay-overlay-matrix rj-add-*, rj-hidden-add-child-before-kt).
            // A VIS_UNDERLAY copy at its holder's layer draws before the holder (stock 516.1688, vis-world set b).
            // Under a turf holder it draws before the turf, so an opaque floor hides it (vis-world set e).
            auto rank=[](const MapSprite& s) {
                return s.kind != MapSprite::Vis || !s.visUnderlay ? 2*int(s.kind) : s.visUnderlay == 2 ? -1 : 2*int(MapSprite::Entity)-1;
            };
            return std::tuple(a.kind == MapSprite::Screen ? 0 : a.cell.y(), a.kind == MapSprite::Screen ? 0 : a.cell.x(),
                              rank(a), a.screenOrder, a.attachmentOrder, a.imageIdentity, a.atom) <
                   std::tuple(b.kind == MapSprite::Screen ? 0 : b.cell.y(), b.kind == MapSprite::Screen ? 0 : b.cell.x(),
                              rank(b), b.screenOrder, b.attachmentOrder, b.imageIdentity, b.atom);
        });
        if (stats) stats->sortedSprites = order_.size();
    }
}

// Worker provenance/refusal and immutable command assembly only. Backend graph
// selection/admission remains in CompositionPlan, not this publication stage.
void EntityMap::assembleOutput(bool changed, QPoint translation, const QRect& view,
                              bool blacknessActive, const std::vector<QRect>& blackness, Stats* stats) {
    if (changed || translation_ != translation || view_ != view ||
        blacknessActive_ != blacknessActive || blackness_ != blackness) {
        MapSprites next, planeZeroScreen;
        next.reserve(qsizetype(order_.size()+blackness.size()));
        // A cross-plane owned leaf is verified only with no selected master on
        // that plane, or a selected master inside its own bounded tree. Do not
        // guess cross-root escape ownership/coordinates. Preserve established
        // same-plane last-master selection; refuse only this new combination.
        std::map<int, const Instance*> selectedMasters;
        std::map<const Instance*, int> rootPlanes;
        for (const auto& draw : order_) {
            const auto& sprite = draw.instance->sprites[draw.part];
            if (sprite.kind == MapSprite::Master) {
                selectedMasters[sprite.plane] = draw.instance;
                // Templates emit the owning root master last (postorder).
                rootPlanes.try_emplace(draw.instance, draw.instance->sprites.back().plane);
            }
        }
        std::set<const Instance*> refusedEscapes;
        for (const auto& draw : order_) {
            const auto& sprite = draw.instance->sprites[draw.part];
            const auto own = rootPlanes.find(draw.instance);
            const auto selected = selectedMasters.find(sprite.plane);
            if (sprite.kind == MapSprite::Screen && own != rootPlanes.end() && own->second != sprite.plane &&
                selected != selectedMasters.end() && selected->second != draw.instance)
                refusedEscapes.insert(draw.instance);
        }
        // Reverse transitions admit fractional alpha, but still require ordinary
        // 32px sources. Validate all contributors, not just owned leaves. Failed
        // provenance omits the chain; the GUI never reads the model.
        std::map<int,const Instance*> simplePlanes;
        std::set<int> ignoredFilterPlanes;
        for (const auto& draw : order_) {
            const auto& s = draw.instance->sprites[draw.part];
            if (s.kind != MapSprite::Master) continue;
            simplePlanes.erase(s.plane); ignoredFilterPlanes.erase(s.plane);
            if (s.color!=identityColor && s.tint!=std::array<float,4>{1,1,1,1}) ignoredFilterPlanes.insert(s.plane);
            if (s.planeEffects && s.planeEffects->requiresSimpleSources()) simplePlanes[s.plane]=draw.instance;
        }
        // Relay reverse chains reuse the same worker-side source proof.
        // Unlike master degradation, a failed proof refuses every dependent part.
        std::map<QString,int> relaySourcePlanes;
        std::set<QString> refusedRelaySources;
        for (const auto& draw : order_) {
            const auto& s=draw.instance->sprites[draw.part];
            if (s.planeSourceDependency.isEmpty() || !s.planeEffects || !s.planeEffects->requiresSimpleSources()) continue;
            relaySourcePlanes.emplace(s.planeSourceDependency,0);
        }
        std::map<QString,int> relaySourceCounts;
        for (const auto& draw : order_) {
            const auto& m=draw.instance->sprites[draw.part];
            if (m.kind!=MapSprite::Master || !relaySourcePlanes.contains(m.renderTarget) ||
                selectedMasters[m.plane]!=draw.instance) continue;
            ++relaySourceCounts[m.renderTarget]; relaySourcePlanes[m.renderTarget]=m.plane;
            simplePlanes[m.plane]=draw.instance;
            if (m.tint!=std::array<float,4>{1,1,1,1} || m.color!=identityColor || m.blend>1 ||
                (m.planeEffects && (!m.planeEffects->supported || !m.planeEffects->filters.empty() ||
                 m.planeEffects->ignoredFilters || m.planeEffects->matrix))) refusedRelaySources.insert(m.renderTarget);
        }
        for (const auto& [name,plane] : relaySourcePlanes)
            if (relaySourceCounts[name]!=1) refusedRelaySources.insert(name);
        for (const auto& draw : order_) {
            const auto& s=draw.instance->sprites[draw.part];
            const auto selected=simplePlanes.find(s.plane);
            if (selected==simplePlanes.end() || s.kind==MapSprite::Master || ignoredFilterPlanes.contains(s.plane) ||
                refusedEscapes.contains(selected->second)) continue;
            const bool simple=s.image.size()==QSize(32,32) && s.color==identityColor && s.blend<=1 &&
                !s.appearanceGroup && !s.affine && !s.rootAffine && !s.maptext && s.renderSource.isEmpty() && s.renderTarget.isEmpty() &&
                !s.imageIdentity && !s.visHit;
            if (!simple) ignoredFilterPlanes.insert(s.plane);
        }
        for (const auto& [name,plane] : relaySourcePlanes)
            if (ignoredFilterPlanes.contains(plane)) refusedRelaySources.insert(name);
        std::shared_ptr<const PlaneEffects> refusedEscapeEffects;
        if (!refusedEscapes.empty()) {
            auto effects = std::make_shared<PlaneEffects>(); effects->supported = false;
            refusedEscapeEffects = std::move(effects);
        }
        bool covered=false;
        auto cover=[&] {
            if (covered || !blacknessActive) return;
            covered=true;
            // At plane 0, after world appearances (regardless of layer), before
            // screen appearances. Masters then transform this whole group.
            for (const auto& run:blackness) {
                MapSprite command; command.kind=MapSprite::Blackness;
                command.position=QPoint(run.x()*tile_,-(run.y()+1)*tile_)+translation;
                command.size={run.width()*tile_,tile_}; command.mouseTransparent=true;
                next.push_back(std::move(command));
            }
            next.append(planeZeroScreen);
        };
        for (const auto& draw : order_) {
            const auto& sprite = draw.instance->sprites[draw.part];
            if (sprite.plane>0) cover();
            if (refusedRelaySources.contains(sprite.planeSourceDependency)) continue;
            const bool refuseEscape = refusedEscapes.contains(draw.instance);
            if (refuseEscape && sprite.kind != MapSprite::Master) continue;
            if (!sprite.appearanceGroup && sprite.kind != MapSprite::Master && sprite.renderTarget.isEmpty() && sprite.renderSource.isEmpty() && !sprite.rect().intersects(view)) continue;
            auto& list=blacknessActive && sprite.plane==0 && sprite.kind==MapSprite::Screen ? planeZeroScreen : next;
            list.push_back(sprite);
            if (refuseEscape) list.back().planeEffects = refusedEscapeEffects;
            else if (sprite.kind==MapSprite::Master && ignoredFilterPlanes.contains(sprite.plane) && sprite.planeEffects &&
                     sprite.planeEffects->requiresSimpleSources()) {
                auto effects=std::make_shared<PlaneEffects>(*sprite.planeEffects);
                effects->ignoreFilters();list.back().planeEffects=std::move(effects);
            }
            list.back().position += translation; list.back().iconBottom += translation;
            if (sprite.appearanceGroup) list.back().groupOrigin += translation;
            if (sprite.localGroup) {
                list.back().captureOrigin += translation; list.back().relayMouseBottom += translation;
            }
        }
        cover();
        output_ = std::move(next);
        blackness_=blackness; blacknessActive_=blacknessActive;
        translation_ = translation; view_ = view;
    } else if (stats) ++stats->sceneReuses;
}

void EntityMap::prune() {
    std::erase_if(instances_, [&](const auto& entry) { return entry.second.used != epoch_; });
    templates_.prune(epoch_);
    std::erase_if(occurrences_, [&](const auto& entry) { return entry.second.used != epoch_; });
    std::erase_if(appearances_, [&](const auto& entry) { return entry.second.checked != epoch_; });
    // Retain recently selected cells across animation cycles. The additional decoded
    // cache is bounded; active templates/output own their images independently.
    if (frameBytes_ > 64 * 1024 * 1024 || frames_.size() > 65536) {
        std::vector<decltype(frames_)::iterator> cold;
        for (auto it = frames_.begin(); it != frames_.end(); ++it) cold.push_back(it);
        std::sort(cold.begin(), cold.end(), [](auto a, auto b) { return a->second.used < b->second.used; });
        for (auto it : cold) {
            if (frameBytes_ <= 64 * 1024 * 1024 && frames_.size() <= 65536) break;
            frameBytes_ -= it->second.image->sizeInBytes(); frames_.erase(it);
        }
    }
}
}
