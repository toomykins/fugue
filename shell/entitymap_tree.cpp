// Shared anonymous appearance trees for ordinary groups and vis copies.
// Templates contain assets/materials only; placement and playback remain instance-owned.
#include "shell/entitymap.h"
#include <bit>
#include "shell/animation.h"

namespace fugue::shell {
EntityMap::Template EntityMap::TemplateBuilder::constructTree(const TemplateRequest& request, const Appearance* sampled) {
    if (stats) ++stats->templateBuilds;
    Template next;
    next.checked = epoch_; next.revision = ++revision_;
    bool deferred = false, refused = false, retry = false;
    // A vis copy's inherited icon, state, layer and plane replace the root's own; its overlays still inherit from it.
    Appearance inheritedRoot;
    if (const auto* vis = std::get_if<VisRequest>(&request.source); vis && std::get<0>(vis->inherit)) {
        inheritedRoot = sampled ? *sampled : appearance(request.rootId);
        const auto& [flags, icon, state] = vis->inherit;
        if (flags & 1) inheritedRoot.icon = icon;
        if (flags & 2) inheritedRoot.state = state;
        if (flags & 8) inheritedRoot.layerBits = std::bit_cast<uint32_t>(-1.f); // the context layer
        if (flags & 0x10) { inheritedRoot.hasPlane = true; inheritedRoot.plane = -32767; } // FLOAT_PLANE: the context plane
        sampled = &inheritedRoot;
    }
    const auto& rootFields = sampled ? *sampled : appearance(request.rootId);
    // An atom with a render target is captured, also as a vis_contents member (tgstation's eyelids are hidden target
    // objects in the mob's vis_contents; stock 516.1689, stock-parity-world overlay-relays).
    const bool localCapture = (std::holds_alternative<OrdinaryRequest>(request.source) ||
                               std::holds_alternative<VisRequest>(request.source)) && !rootFields.renderTarget.isEmpty();
    // KEEP_TOGETHER on a target with no children groups nothing (tgstation's starlight; tgstation-refusals rf-target-star).
    const bool capturedTree = localCapture && rootFields.keepTogether &&
                              (!rootFields.underlays.empty() || !rootFields.overlays.empty());
    // A hidden target is only ever drawn by its relays, each with its own blend mode: BLEND_ADD on the target itself
    // changes nothing in what is captured, while BLEND_MULTIPLY captures nothing (tgstation-refusals rf-target-add, rf-target-mul).
    const bool additiveTarget = localCapture && rootFields.blend == 2 && rootFields.renderTarget.startsWith('*');
    // A hidden target is captured turned, filtered, with its maptext, and as a KEEP_TOGETHER group with its colour and
    // alpha applied once to the group (stock 516.1689). A visible one this cannot capture is drawn as itself (below).
    const bool hiddenCapture = localCapture && rootFields.renderTarget.startsWith('*');
    struct Material {
        std::array<float,4> tint{1,1,1,1};
        ColorMatrix color = identityColor;
        int blend = 0;
    };
    auto combine = [](Material parent, const Material& child) {
        parent.blend = child.blend ? child.blend : parent.blend;
        if (parent.color != identityColor || child.color != identityColor) {
            parent.color = composeColor(tintColor(parent.color,parent.tint), tintColor(child.color,child.tint));
            parent.tint = {1,1,1,1};
        } else for (int i=0;i<4;++i) parent.tint[i] *= child.tint[i];
        return parent;
    };
    auto fail = [&](TraceReason reason, uint32_t id) {
        refused = true;
        if (next.first.reason == TraceReason::Unobserved) next.first = {reason,id,0};
    };
    std::vector<uint32_t> path;
    std::map<const AppearanceGroup*,QRectF> groupBounds;
    const bool visTree = std::holds_alternative<VisRequest>(request.source);
    // A part's own filters run on a surface of its own icon; its overlays and underlays draw unfiltered around it unless
    // KEEP_TOGETHER groups them, and an overlay's own filters filter that overlay (stock 516.1688, atom-filter-world).
    auto filterGroup = [&](const Appearance& ap, bool root) {
        return ap.atomFilters && !ap.atomFilters->filters.empty() &&
               (localCapture ? root && hiddenCapture : ap.renderTarget.isEmpty());
    };
    auto expand = [&](auto&& self, uint32_t id, QPoint at, uint32_t inheritedIcon, int direction, int plane, float layer,
                      Material inherited, std::shared_ptr<const AppearanceGroup> group, bool root,
                      const animation::Matrix& inheritedMatrix) -> void {
        if (refused || deferred) return;
        if (!visTemplateWork) {deferred=true;retry=true;return;}
        if (path.size() >= 32 || next.visits >= 4096 ||
            std::find(path.begin(),path.end(),id) != path.end()) { fail(TraceReason::TraversalLimit,id); return; }
        --visTemplateWork; ++next.visits;
        if (stats) ++stats->visits;
        const auto& ap = root && sampled ? *sampled : appearance(id);
        next.dependencies.emplace_back(id,ap.revision);
        if (!ap.present || !ap.valid) { fail(TraceReason::MissingAppearance,id); return; }
        // Stock applies invisibility to the root/vis occurrence, not the
        // ordinary children in the local-target appearance tree (including
        // the invisibility=101 source overlay in the reduced arrival fixture).
        if ((root || !localCapture) && ap.invisibility > seeInvisible_ && request.visibility != VisibilityPolicy::SeeSelf) return;
        // An overlay that is itself a hidden render target is not captured here yet: it is left out, as stock leaves it
        // undrawn, and the rest of the atom drawn. A visible target on an overlay draws as the overlay it is.
        if (!localCapture && !root && ap.renderTarget.startsWith('*')) return;
        // An overlay with a render_source relays that target where the overlay stands, in the atom's group when it has
        // one, with the overlay's own colour, alpha, matrix and filters (overlay-relays: ov-relay*, ov-vis-src*).
        const bool source = !ap.renderSource.isEmpty() && (localCapture || !root);
        const bool captured = localCapture && (root || capturedTree);
        // A captured atom's own colour and alpha are in what is captured (ov-relay-src-color, ov-relay-src-alpha).
        const bool hiddenRoot = root && hiddenCapture;
        if ((captured && ((ap.hasTransform && !hiddenRoot) || (ap.surfaceFilters && !(hiddenRoot && filterGroup(ap,true))) ||
                // (a colour matrix is captured too, and KEEP_APART on the target itself changes nothing, stock 516.1689)
                (ap.blend > 1 && !(root && additiveTarget)) || (!root && ap.keepApart) ||
                (!root && ap.keepTogether) || source ||
                (root && capturedTree && !hiddenCapture && ap.tint != std::array<float,4>{1,1,1,1}))) ||
            (source && (localCapture ? group || ap.hasTransform || ap.surfaceFilters || !ap.underlays.empty() ||
                                           !ap.overlays.empty() || ap.tint[3] != 1 || ap.color != identityColor || ap.blend > 1
                                     : !ap.underlays.empty() || !ap.overlays.empty())) ||
            ap.master || (!ap.identityFilters && !(source && !localCapture) && (root ? !filterGroup(ap,true) : localCapture)) ||
            (!source && !ap.renderSource.isEmpty()) ||
            (!ap.renderTarget.isEmpty() && root && !localCapture) ||
            (!ap.maptext.isEmpty() && localCapture && !hiddenCapture) || (!root && !ap.screenLoc.isEmpty() && !localCapture) || ap.imageOverride || !ap.colorValid ||
            ap.blend > 5 || (localCapture && ap.blend == 3) ||
            !ap.matrixValid || !StaticAffine::make(ap.matrix,{tile_,tile_})) { fail(TraceReason::EffectCombination,id); return; }
        const float ownLayer = std::bit_cast<float>(ap.layerBits);
        if (!std::isfinite(ownLayer) || (ownLayer < 0 && ownLayer != -1)) { fail(TraceReason::EffectCombination,id); return; }
        if (ownLayer >= 0) layer = ownLayer;
        const int resolvedPlane = !root && !ap.hasPlane ? plane : resolvePlane(ap.hasPlane ? ap.plane : 0,plane);
        const int inheritedPlane = plane;
        plane = resolvedPlane;
        at += ap.offset;
        if (root) next.iconBottom = at + QPoint(0,tile_);
        // Overlays, underlays and vis copies compose the parent's matrix about each part's own centre unless a
        // KEEP_TOGETHER surface above already carries it; RESET_TRANSFORM drops it (stock 516.1688, vis-world set c,
        // transform-group-world).
        auto matrix=!ap.resetTransform ? animation::multiply(inheritedMatrix,ap.matrix) : ap.matrix;
        auto detach = [&](bool material) {
            if (!ap.resetTransform) {
                const auto& p=group->matrix;const auto c=matrix;
                matrix={p[0]*c[0]+p[1]*c[3],p[0]*c[1]+p[1]*c[4],p[0]*c[2]+p[1]*c[5]+p[2],
                        p[3]*c[0]+p[4]*c[3],p[3]*c[1]+p[4]*c[4],p[3]*c[2]+p[4]*c[5]+p[5]};
            }
            if (material) inherited = combine({group->tint,group->color,group->blend},inherited);
            group = group->parent;
        };
        // KEEP_APART detaches from its immediate capture. An overlay on another plane leaves every capture above it,
        // with the colour and alpha of the innermost one only (stock 516.1689, stock-parity-world kt-overlay-plane,
        // kt-nested-plane).
        // With no group above it KEEP_APART has nothing to leave: tgstation's emissive blockers on an item in an
        // inventory slot's vis_contents, the hunger bar's outlined burger (tgstation-refusals rf-vis-blocker*, rf-ka-filter*).
        const bool ungrouped = !group;
        bool left = false;
        if (ap.keepApart && group) { detach(true); left = true; }
        while (group && resolvedPlane != inheritedPlane) { detach(!left); left = true; }
        if (!group) {
            if (ap.resetColor) {
                inherited.tint[0]=inherited.tint[1]=inherited.tint[2]=1;
                for (int c=0;c<5;++c) for (int r=0;r<3;++r) inherited.color[4*c+r]=identityColor[4*c+r];
            }
            if (ap.resetAlpha) {
                inherited.tint[3]=1;
                for (int c=0;c<5;++c) inherited.color[4*c+3]=identityColor[4*c+3];
            }
        }
        Material material = combine(inherited,{ap.tint,ap.color,ap.blend});
        if (root && additiveTarget) material.blend = 0;
        if (!finiteColor(material.color)) { fail(TraceReason::EffectCombination,id); return; }
        const auto* flick = root ? std::get_if<FlickRequest>(&request.source) : nullptr;
        const uint32_t ordinal = flick ? flick->iconResIndex : ap.icon != UINT32_MAX && ap.icon != 0xffffu ? ap.icon :
                                 ap.state.empty() ? UINT32_MAX : inheritedIcon; // as in expand() of entitymap.cpp
        const int dir = root ? MapTurn(turn_).icon(request.rootDir ? request.rootDir : ap.dir ? ap.dir : 2) : ap.fixedDir ? ap.dir : direction;
        std::shared_ptr<const QImage> image;
        DmiIcon::State animation;
        FrameKey frameKey{};
        std::optional<std::pair<DmiIcon::State, FrameKey>> moving;
        if (captured && root && ordinal == UINT32_MAX) { fail(TraceReason::MissingAppearance,id); return; }
        if (ordinal != UINT32_MAX && !source) {
            const auto res = resource(ordinal); next.resources.push_back(res);
            if (!res.ready) { deferred=true; return; }
            const auto& icon=icons_.icon(res.id);
            const auto name=QString::fromStdString(flick ? flick->iconState : ap.state);
            const auto* state=icon.state(name);
            // A flick of a missing state keeps default-state timing but draws nothing, as on the flat path.
            if (flick && !icon.states.count(name)) state=nullptr;
            frameKey={res.id,res.revision,state ? icon.index(*state,dir) : -1};
            image=frameImage(frameKey);
            if (!image || icon.sheet.isNull()) { deferred=true; retry=!image; return; }
            if (state) animation=*state;
            if (const auto twin=icon.moving.find(name); state && !flick && icon.states.count(name) && twin!=icon.moving.end())
                moving.emplace(twin->second,FrameKey{res.id,res.revision,icon.index(twin->second,dir)});
        }
        const QSize iconSize=image && !image->isNull() ? image->size() : QSize(tile_,tile_);
        if (root && image) next.iconSize=image->size();
        const QPoint top=at+QPoint(0,tile_-iconSize.height());
        const bool transformed=matrix!=std::array<double,6>{1,0,0,0,1,0};
        // Filters run on the part's untransformed surface and the matrix then moves the filtered surface: a drop
        // shadow's offset turns with it and a stretched outline widens (stock 516.1688, transform-group-world).
        const bool filtered=filterGroup(ap,root);
        // KEEP_TOGETHER on an overlay with nothing to keep together and no group above is a plain overlay: no surface,
        // and a matrix smooths it like any part (stock 516.1689).
        const bool lone=!root && ungrouped && ap.underlays.empty() && ap.overlays.empty() && ap.maptext.isEmpty();
        // Maptext is drawn in one surface with its own icon, which then takes the alpha, colour matrix, blend mode and
        // matrix once: translucent text hides the icon under it, and a matrix smooths the two together, the icon's edge
        // without a lone icon's dark fringe. Overlays stay outside it (stock 516.1689).
        // Inside a KEEP_TOGETHER surface the text is one more part of that surface and its icon counts by its ink like any
        // other (te-kt-overlay-maptext-small-turn, te-kt-maptext-turn).
        // Text without an icon needs the surface only for a matrix, which smooths it premultiplied (tb-maptext-only-turn).
        const bool texted=!ap.maptext.isEmpty() && !localCapture && !group &&
            (transformed || (image && !image->isNull() &&
                             (material.tint[3]!=1 || material.blend>1 || material.color!=identityColor)));
        const bool grouping=(ap.keepTogether && !lone) || filtered || texted; // the matrix moves the group's surface, not its parts
        auto childGroup=group; auto childMaterial=material; // a filter-only group holds the root's own icon
        // A leaf render_source replaces its own ink. With no enclosing
        // group or children, KEEP_TOGETHER/KEEP_APART add no capture edge.
        // A captured group is its own surface; a hidden one's matrix, filters or material still run on a surface inside
        // what is captured.
        const bool hiddenSurface=hiddenRoot && (filtered || transformed || (capturedTree && (material.color!=identityColor ||
                                                material.tint!=std::array<float,4>{1,1,1,1})));
        if (grouping && !source && !(localCapture && root && ap.keepTogether && !hiddenSurface)) {
            next.grouped=true;
            auto capture=std::make_shared<AppearanceGroup>();
            if (filtered) capture->effects=withMasks(ap.atomFilters,next.complete);
            // KEEP_TOGETHER with no overlays keeps nothing together: with maptext it is the surface of an icon and its
            // text, like any atom's (td-kt-maptext-small-turn; with an overlay the icon counts by its ink,
            // te-kt-maptext-turn).
            capture->filterOnly=!ap.keepTogether || (!ap.maptext.isEmpty() && ap.underlays.empty() && ap.overlays.empty());
            capture->parent=group; capture->tint=material.tint; capture->color=material.color;
            capture->blend=material.blend; capture->layer=layer;
            capture->matrix=matrix; capture->nearest=ap.pixelScale;
            const auto affine=StaticAffine::make(matrix,iconSize);
            if (!affine) { fail(TraceReason::InvalidTransform,id); return; }
            const auto& t=affine->forward;
            const QPointF offset=QPointF(top)+t.map(-QPointF(top));
            capture->transform=QTransform(t.m11(),t.m12(),t.m21(),t.m22(),offset.x(),offset.y());
            group=std::move(capture);
            if (root && ap.keepTogether) next.rootGroup=group;
            material={}; // the group's material/transform runs ONCE, after its contents
            if (ap.keepTogether) { childGroup=group; childMaterial=material; }
        } else { childGroup=group; childMaterial=material; }
        path.push_back(id);
        static constexpr animation::Matrix identityMatrix{1,0,0,0,1,0};
        // Children of a filter-only group sit outside its surface and inherit the matrix themselves.
        const auto& childMatrix=ap.keepTogether ? identityMatrix : matrix;
        for (auto child:ap.underlays) self(self,child,at,ordinal,dir,plane,layer,childMaterial,childGroup,false,childMatrix);
        if (source || (image && !image->isNull())) {
            Part part{image,top,plane,layer,material.tint};
            part.color=material.color; part.blend=material.blend; part.size=iconSize; part.group=group;
            part.renderSource=ap.renderSource;
            if (source) {
                part.sourcePath=std::make_shared<const std::vector<uint32_t>>(path);
                // Only default ordinary-child source routing has a stock receipt.
                // Other child-owned mouse modes/flags remain visually admitted,
                // but must not silently borrow the root's PASS_MOUSE behavior.
                part.inkInteractive=ap.mouseOpacity==1 && !ap.passMouse && !rootFields.passMouse;
            }
            if (captured) part.renderTarget=rootFields.renderTarget;
            if (source && !localCapture) {  // the composition plan runs the relay's own filters and matrix
                part.planeEffects=withMasks(ap.planeEffects,next.complete);
                // The matrix is the relay's own composed with its atom's, about the relayed icon's centre: a turned or
                // scaled atom turns its relay overlay with it, the overlay's offset unturned (relay-overlay-matrix rj-*-relay-*).
                if (part.planeEffects && matrix!=ap.matrix) {
                    auto effects=std::make_shared<PlaneEffects>(*part.planeEffects);
                    if (transformed) effects->matrix=matrix; else effects->matrix.reset();
                    effects->supported &= !transformed || StaticAffine::make(matrix,{tile_,tile_}).has_value();
                    part.planeEffects=std::move(effects);
                }
                part.smooth=transformed && !ap.pixelScale;
            }
            if (transformed && !grouping && !source) {
                part.affine=StaticAffine::make(matrix,image->size());
                if (!part.affine) fail(TraceReason::InvalidTransform,id);
                part.smooth=!ap.pixelScale; // bilinear unless its own PIXEL_SCALE (static-transform-world, vis-world set c)
            }
            QRectF bounds=part.affine ? QRectF(part.affine->bounds.translated(top)) : QRectF(top,iconSize);
            for (auto g=group;g;g=g->parent) {
                groupBounds[g.get()]=groupBounds[g.get()].united(bounds);
                bounds=g->transform.mapRect(bounds);
            }
            if (!bounds.isValid() || std::max({std::abs(bounds.left()),std::abs(bounds.right()),
                std::abs(bounds.top()),std::abs(bounds.bottom())})>65536) fail(TraceReason::InvalidTransform,id);
            next.bounds=next.bounds.united(bounds.toAlignedRect());
            if (animation.frames>1 || moving) {
                part.animation=std::move(animation); part.frameKey=frameKey; part.flickRoot=flick!=nullptr;
                part.moving=std::move(moving);
                next.animated.push_back(next.parts.size());
            }
            next.parts.push_back(std::move(part));
        }
        // Maptext joins its atom's filter or KEEP_TOGETHER surface: stock outlines the text and the icon as one shape
        // (stock 516.1688, atom-filter-world set b maptext, filter-reach-world). Ungrouped text on a matrix keeps the flat path's pivot.
        if (!ap.maptext.isEmpty() && material.tint[3] > 0) {
            const QSize size(ap.maptextSize.width()==0 ? tile_ : ap.maptextSize.width(),
                             ap.maptextSize.height()==0 ? tile_ : ap.maptextSize.height());
            const auto text=maptext_.lookup(ap.maptext,size);
            if (text.status==MapTextCache::Status::Deferred) { deferred=true; retry=true; return; }
            if (text.status!=MapTextCache::Status::Ready) { fail(TraceReason::MaptextUnsupported,id); return; }
            if (text.image) {
                // (the box's corner; an outline or shadow reaches text.pad px past it on every side)
                const QPoint textTop=at+ap.maptextOffset+QPoint(0,tile_-size.height())-QPoint(text.pad,text.pad);
                Part part{text.image,textTop,plane,layer,material.tint};
                part.color=material.color; part.blend=material.blend; part.maptext=true; part.textMargin=text.pad; part.group=group;
                if (captured) part.renderTarget=rootFields.renderTarget;
                QRectF bounds(textTop,text.image->size());
                if (transformed && !grouping) {
                    // Ungrouped text turns about its atom's icon centre, as on the flat path (stock 516.1689).
                    const QPointF centre=QPointF(textTop)+QPointF(text.image->width()/2.0,text.image->height()/2.0);
                    const double dx=at.x()+tile_/2.0-centre.x(), dy=centre.y()-(at.y()+tile_/2.0); // Y up
                    auto m=matrix;
                    m[2]+=dx-(m[0]*dx+m[1]*dy); m[5]+=dy-(m[3]*dx+m[4]*dy);
                    part.affine=StaticAffine::make(m,text.image->size());
                    if (!part.affine) { fail(TraceReason::InvalidTransform,id); return; }
                    part.smooth=!ap.pixelScale;
                    bounds=part.affine->bounds.translated(textTop);
                }
                for (auto g=group;g;g=g->parent) {
                    groupBounds[g.get()]=groupBounds[g.get()].united(bounds);
                    bounds=g->transform.mapRect(bounds);
                }
                next.bounds=next.bounds.united(bounds.toAlignedRect());
                next.parts.push_back(std::move(part));
            }
        }
        for (auto child:ap.overlays) self(self,child,at,ordinal,dir,plane,layer,childMaterial,childGroup,false,childMatrix);
        path.pop_back();
    };
    int plane=0; float layer=0; Material inherited; animation::Matrix matrix{1,0,0,0,1,0};
    if (const auto* vis=std::get_if<VisRequest>(&request.source)) {
        plane=std::get<0>(vis->context); layer=std::bit_cast<float>(std::get<1>(vis->context));
        inherited={std::get<2>(vis->context),std::get<3>(vis->context),std::get<4>(vis->context)};
        matrix=std::get<5>(vis->context);
    }
    expand(expand,request.rootId,{},UINT32_MAX,2,plane,layer,inherited,nullptr,true,matrix);
    for (const auto& [group,bounds]:groupBounds) {
        (void)group;
        if (bounds.width()>8192 || bounds.height()>8192) fail(TraceReason::TraversalLimit,request.rootId);
    }
    std::sort(next.dependencies.begin(),next.dependencies.end());
    next.dependencies.erase(std::unique(next.dependencies.begin(),next.dependencies.end()),next.dependencies.end());
    std::sort(next.resources.begin(),next.resources.end(),[](const auto& a,const auto& b){return a.ordinal<b.ordinal;});
    next.resources.erase(std::unique(next.resources.begin(),next.resources.end()),next.resources.end());
    next.complete=!(deferred || refused);
    next.diagnosticStatus=deferred ? TraceReason::TemplateDeferred : refused ? TraceReason::TemplateRefused : TraceReason::TemplateComplete;
    next.evidenceRevision=next.revision;
    if (!next.complete) { next.parts.clear();next.animated.clear();next.bounds={}; }
    // A visible render target this path cannot capture (turned, filtered, translucent, a group with material) is still
    // drawn as the atom it is; only what would have consumed it goes without (stock 516.1689, stock-parity-world
    // sweep target-turn, target-outline, kt-target).
    if (localCapture && refused && !deferred && !rootFields.renderTarget.startsWith('*')) {
        Appearance visible = rootFields;
        visible.renderTarget.clear();
        if (visible.transformEffects && !visible.keepTogether && visible.renderSource.isEmpty()) {
            visible.transformValid = visible.matrixValid; visible.transformEffects = false;  // as without a target
        }
        auto drawn = construct(request, &visible);
        if (drawn.complete) return drawn;
    }
    if (localCapture && !next.complete) {
        Part marker;
        marker.renderTarget=rootFields.renderTarget; marker.footprint=true;
        marker.plane=rootFields.hasPlane ? resolvePlane(rootFields.plane) : 0;
        marker.size={tile_,tile_}; next.parts={marker}; next.bounds=QRect({},marker.size);
    }
    if (retry) next.revision=0;
    return next;
}
}
