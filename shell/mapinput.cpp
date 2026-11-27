#include "shell/mapinput.h"
#include "shell/screenloc.h"
#include "shell/compositionplan.h"
#include "model/json.h"
#include "model/statpanel.h"
#include "model/world.h"
#include "shell/mappresentation.h"
#include <algorithm>
#include <cmath>
#include <climits>
#include <optional>
#include <set>
#include <map>
#include <iterator>

namespace fugue::shell {
namespace {
// A client image or a vis_contents member on a filtered or turned plane is clicked like the plane's own atoms: by its
// own ink where it stood, through the plane's matrix, naming the image's atom or the member (stock 516.1689,
// plane-click-world outline1, turn90, scalex2).
bool planeInputPart(const MapSprite& s) {
    return !s.affine && !s.rootAffine && !s.maptext && (s.size.isEmpty() || s.size == s.image.size());
}
// A turned master's own overlays carry its matrix about their own centre as well as the plane's: the pointer goes back
// through the plane's, then the part's (stock 516.1689, master-transform-world clicks on master-turn, master-scale).
// So do a client image and a vis_contents member with a matrix of their own, clicked and listed where their turned icon
// is drawn, naming the image's atom or the member (plane-click-world turn90-selfturn, scalex2-selfturn).
bool turnedPlanePart(const MapSprite& s) {
    const bool member = s.kind == MapSprite::Image || s.kind == MapSprite::Vis || s.imageIdentity || s.visHit;
    return s.affine && !s.rootAffine && (s.kind == MapSprite::Screen || member) && !s.maptext &&
        s.mouseOpacity == 1 && s.blend <= 1 && s.renderSource.isEmpty() && s.renderTarget.isEmpty();
}
// Stock popup screenshots establish original rectangles (including alpha holes).
// Independent Enter/Drag/Drop receipts establish original ink for those endpoints.
// Neither contract admits generated ink, attachments, transforms or relay chains.
bool filteredInputPart(const MapSprite& s) {
    return planeInputPart(s) && s.mouseOpacity == 1 && s.image.size() == QSize(32,32) &&
        s.blend <= 1 && s.renderSource.isEmpty() && s.renderTarget.isEmpty();
}
// A plane filter (vg's size-0 perception blur on every master) does not
// change which original pixels own input. Ordinary transformed entity parts,
// such as lying mobs, keep their inverse-mapped source-alpha test.
bool outlinedInputPart(const MapSprite& s) {
    return filteredInputPart(s) || turnedPlanePart(s) ||
        (s.affine && s.kind == MapSprite::Entity && !s.imageIdentity && !s.visHit && !s.maptext && s.mouseOpacity == 1 &&
         s.blend <= 1 && s.renderSource.isEmpty() && s.renderTarget.isEmpty());
}
// A turned group's part is tested and named at its untransformed position (inverse integer corner, like root trees).
QPoint groupPixel(QPoint pixel, const CompositionPlan::Binding& binding) {
    if (!binding.groupInverse) return pixel;
    const auto p = binding.groupInverse->map(QPointF(pixel - binding.groupOrigin)) + QPointF(binding.groupOrigin);
    return {int(std::floor(p.x())), int(std::floor(p.y()))};
}
QPointF planePoint(QPoint pixel, int plane, QRect source, const std::map<int, StaticAffine>& transforms, bool center) {
    const auto it = transforms.find(plane);
    if (it == transforms.end()) return pixel;
    return it->second.inverse.map(QPointF(pixel-source.topLeft()) + (center ? QPointF(.5,.5) : QPointF())) + source.topLeft();
}
QPoint planePixel(QPoint pixel, int plane, QRect source, const std::map<int, StaticAffine>& transforms, bool center) {
    const auto p = planePoint(pixel, plane, source, transforms, center);
    return {int(std::floor(p.x())), int(std::floor(p.y()))};
}
// A part with a matrix of its own on a turned plane takes the pointer through both inverses in one go: the point the
// plane's inverse gives is not rounded to a pixel in between (stock 516.1689, master-transform-world clicks on
// master-scale-child-turn, where a half pixel left by the plane's scale decides the icon cell and the ink's edge).
std::optional<QPointF> turnedPartPoint(const MapSprite& s, QPoint pixel, QRect source,
                                       const std::map<int, StaticAffine>& transforms) {
    if (!s.affine || !transforms.count(s.plane)) return {};
    return planePoint(pixel, s.plane, source, transforms, false);
}
// Where a part with a matrix of its own is under the pointer, in its untransformed icon; outside the icon is nothing.
// partPixel is after the sprite's group inverse; samplePixel is that after the plane's inverse, rounded down.
std::optional<QPoint> turnedIconPixel(const MapSprite& s, QPoint partPixel, QPoint samplePixel, QRect source,
                                      const std::map<int, StaticAffine>& transforms) {
    // Stock ordinary root trees and half-size images test the inverse integer corner, including visible edge pixels
    // whose corner maps to transparent source alpha.
    const auto exact = turnedPartPoint(s, partPixel, source, transforms);
    // The ink is tested in the cell the click will name: the same boundary nudge as nameSprite().
    const QPointF p = s.affine->inverse.map((exact ? *exact - s.position : QPointF(samplePixel - s.position)) +
        (s.smooth || s.rootAffine ? QPointF() : QPointF(.5,.5))) + (exact ? QPointF(1e-6,1e-6) : QPointF());
    if (p.x() < 0 || p.y() < 0 || p.x() >= s.image.width() || p.y() >= s.image.height()) return {};
    return QPoint(int(std::floor(p.x())), int(std::floor(p.y())));
}
// The alpha of one part of a render target at a scene pixel: its own pixel there, times its own alpha.
double partAlpha(const MapSprite& s, QPoint pixel) {
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
// An alpha mask filter on a plane master whose mask is a render target, an atom's or a plane's, hides from clicks what
// it masks out, as a mask from an icon does; MASK_INVERSE hides what it keeps (stock 516.1689, plane-click-world
// rtalpha, ptalpha, rtalpha-inverse, turn90-rtalpha). An atom's target is centred on the plane, a plane's stays where
// it is drawn, both moved by the filter's x, y, as the drawing places them. The mask's alpha here is its parts' own
// pixels over one another, each through its own matrix, and a plane's through its master's; filters on the mask's source
// are not applied, as stock does not (rtalpha-rtturn, ptalpha-ptturn, rtalpha-rtoutline, ptalpha-ptoutline).
bool renderMaskedOut(const CompositionPlan& plan, const MapSprite& s, QPoint planePixel) {
    if (plan.hiddenTree.count(s.plane)) return false;
    const auto master = plan.masters.find(s.plane);
    if (master == plan.masters.end() || master->second < 0 || master->second >= plan.sprites.size()) return false;
    const auto* effects = plan.sprites[master->second].planeEffects.get();
    if (!effects) return false;
    for (const auto& f : effects->filters) {
        if (f.kind != PlaneFilter::AlphaMask || f.icon != UINT32_MAX || f.source.isEmpty()) continue;
        const QPoint offset(int(std::lround(f.x)), int(std::lround(f.y)));
        const std::vector<qsizetype>* parts = nullptr;
        QPoint at;
        const int group = plan.local.find(f.source);
        if (group >= 0 && plan.local.groups[size_t(group)].valid) {
            const auto& g = plan.local.groups[size_t(group)];
            const QPoint topLeft = plan.source.topLeft() + offset +
                QPoint((plan.source.width() - g.bounds.width()) / 2, (plan.source.height() - g.bounds.height()) / 2);
            at = planePixel - topLeft + g.bounds.topLeft(); parts = &g.parts;
        } else if (const auto* surface = plan.planeSources.find(f.source)) {
            at = planePixel - offset; parts = &surface->parts;
            // A plane's target is its picture under its own master's matrix: the point goes back through that too
            // (stock 516.1689, plane-click-world ptalpha-ptturn).
            if (surface->master >= 0 && surface->master < plan.sprites.size())
                at = ::fugue::shell::planePixel(at, plan.sprites[surface->master].plane, plan.source, plan.transforms, false);
        }
        double alpha = 0;   // a target that is not there is an empty mask
        if (parts) for (auto i : *parts) alpha += partAlpha(plan.sprites[i], at) * (1 - alpha);
        const int level = int(std::lround(alpha * 255));
        if ((f.flags & 1) ? level == 255 : level == 0) return true;
    }
    return false;
}
// In a map enlarged by screen objects outside the view, stock 516.1689 takes the pointer to the world only in the view:
// a world atom in the added tiles is neither clicked nor listed in the right-click menu, drawn there or not, while one in
// the view is, also where the black strip at the control's left or top hides it, and a colour-matrix atom is taken on
// its own tile, not where it is drawn stretched. Screen objects are taken anywhere (master-screen-loc-world clicks.json).
bool outsideWorldView(const MapSprite& s, const MapPointer& pointer) {
    const bool world = s.kind == MapSprite::Turf || s.kind == MapSprite::Entity || s.kind == MapSprite::Image ||
                       s.kind == MapSprite::Vis;
    return world && !pointer.worldView.isNull() && !pointer.worldView.contains(pointer.scenePixel);
}
bool mouseCandidate(const MapSprite& s, QPoint pixel, const std::set<int>& hidden) {
    return s.kind != MapSprite::Blackness && s.kind != MapSprite::Master &&
        (!s.mouseTransparent || s.maptext) && s.mouseOpacity > 0 && s.mouseOpacity <= 2 && !hidden.count(s.plane) &&
        s.renderSource.isEmpty() && !s.renderTarget.startsWith('*') && s.rect().contains(pixel) &&
        ((s.kind != MapSprite::Screen && s.kind != MapSprite::Image && s.kind != MapSprite::Vis) || s.screenInteractive);
}
void nameSprite(MapPointer& out, const MapSprite& s, QPoint pixel, std::optional<QPointF> exact = {}) {
    out.planeMousePath.reset();
    out.filteredPlane = false;
    out.masterFallback = false;
    auto& c = out.click;
    c.over = mapSpriteRef(s);
    out.screen = s.kind == MapSprite::Screen;
    c.loc = out.screen ? proto::enc::nullRef() : proto::enc::turfRef(s.cell.x(), s.cell.y());
    c.iconX = pixel.x() - s.iconBottom.x() + 1;
    c.iconY = s.iconBottom.y() - pixel.y();
    c.hasVis = false;
    if (s.affine) {
        // A point on a cell boundary after the inverse (the diagonal of an eighth turn) belongs to the next cell.
        const QPointF icon = s.affine->inverse.map((exact ? *exact : QPointF(pixel)) - s.position) + QPointF(1e-6, 1e-6);
        c.iconX = int(std::floor(icon.x())) + 1;
        c.iconY = s.image.height() - int(std::floor(icon.y()));
        c.hasVis = s.affine->visualCoordinates;
        c.visX = pixel.x() - s.rect().x() + 1;
        c.visY = s.rect().y() + s.rect().height() - pixel.y();
    }
    if (s.rootAffine) {
        const QPoint rootTop = s.iconBottom - QPoint(0, s.rootSize.height());
        const QPointF icon = s.rootAffine->inverse.map(QPointF(pixel - rootTop));
        c.iconX = int(std::floor(icon.x())) + 1;
        c.iconY = s.rootSize.height() - int(std::floor(icon.y()));
        c.hasVis = s.rootAffine->visualCoordinates;
        const QRect bounds = s.rootAffine->bounds.translated(rootTop);
        c.visX = pixel.x() - bounds.x() + 1;
        c.visY = bounds.y() + bounds.height() - pixel.y();
    }
    out.backgroundHidden = false;
    out.atomKey = s.key; out.haveAtomKey = s.haveKey;
    out.visHit = s.visHit;
    out.imageId = s.imageId; out.imageIdentity = s.imageIdentity;
    out.targetIdentity = s.targetIdentity; out.hudIdentity = s.hudIdentity;
    out.turfDimensions = s.turfDimensions;
}
void namePlaneSprite(MapPointer& out, const MapSprite& s, QPoint pixel, QRect source,
                     const std::map<int, StaticAffine>& transforms) {
    nameSprite(out, s, planePixel(pixel,s.plane,source,transforms,false), turnedPartPoint(s,pixel,source,transforms));
    const auto transform = transforms.find(s.plane);
    if (transform == transforms.end() || !transform->second.visualCoordinates) return;
    // Keep source icon coordinates separate from the pointer's visual coordinates.
    // Pure translation has no vis fields; the retained stock quarter-turn does.
    const QRect bounds = transform->second.forward.mapRect(
        QRectF(s.rect().translated(-source.topLeft()))).toAlignedRect().translated(source.topLeft());
    out.click.hasVis = true;
    out.click.visX = pixel.x() - bounds.x() + 1;
    out.click.visY = bounds.y() + bounds.height() - pixel.y();
}
// Relay ownership is source-owned only for the independently recorded PASS_MOUSE
// bit. Source/consumer mouse modes do not replace the captured alpha mask.
std::optional<MapPointer> relayAt(const MapPointer& pointer, const MapSprite& consumer,
                                   QPoint pixel, const LocalTargets& targets, int index, bool menu) {
    if (index < 0 || !targets.groups[index].valid || !consumer.inkInteractive || !LocalTargets::materialSupported(consumer)) return {};
    const auto& group = targets.groups[index];
    if (!LocalTargets::placed(consumer,group).contains(pixel)) return {};
    const QPoint mapped = pixel-consumer.position+group.origin;
    bool covered = menu;
    for (auto part : group.parts) {
        const auto& s = pointer.composition->sprites[part];
        // Chained composited input needs independent ownership evidence.
        if (!s.renderSource.isEmpty()) return {};
        if (s.rect().contains(mapped) && !s.image.isNull()) {
            const auto p = mapped-s.position;
            if (s.image.pixelColor(p.x()*s.image.width()/s.rect().width(),
                                   p.y()*s.image.height()/s.rect().height()).alpha()) covered = true;
        }
    }
    if (!covered) return {};
    const auto& producer = pointer.composition->sprites[group.parts.front()];
    if (!consumer.passMouse && (consumer.mouseOpacity != 1 || consumer.mouseTransparent)) return {};
    auto hit = pointer;
    hit.source.reset(); hit.relayHit.reset();
    auto evidence = std::make_shared<RelayHit>();
    evidence->producer = hit; evidence->consumer = hit; evidence->name = group.name;
    evidence->producerAppearance = producer.appearanceId;
    evidence->consumerAppearance = consumer.appearanceId;
    evidence->consumerPath = consumer.sourcePath;
    nameSprite(evidence->producer,producer,mapped);
    nameSprite(evidence->consumer,consumer,pixel);
    // The retained endpoint tokens need no second scene copy or nested gesture.
    evidence->producer.renderedScene.clear(); evidence->consumer.renderedScene.clear();
    evidence->producer.composition.reset(); evidence->consumer.composition.reset();
    nameSprite(hit,consumer.passMouse ? producer : consumer,consumer.passMouse ? mapped : pixel);
    if (consumer.passMouse) {
        hit.click.hasVis = true;
        hit.click.visX = pixel.x()-producer.relayMouseBottom.x()+1;
        hit.click.visY = producer.relayMouseBottom.y()-pixel.y();
    }
    hit.relayHit = std::move(evidence);
    return hit;
}
// Where a pointer over a relay of a plane's surface is in the plane's own picture: back by the relay's offset from 1,1,
// then through the relay's matrix about the view's centre, to the pixel whose corner it lands in, as for a master's
// matrix. Input never takes the displacement stock draws a neutral plane with (stock 516.1689, plane-relay-world:
// rturn90 at 114,139 names part y=18; nopass-loc23 names the relay with the picture's own icon-x/y).
std::optional<QPoint> relayPixel(const MapSprite& relay, const CompositionPlan::Binding& binding, QPoint pixel, QRect source) {
    QPoint local = pixel - CompositionPlan::relayOffset(relay, source);
    if (relay.planeEffects && relay.planeEffects->matrix) {
        if (!binding.mouseAffine) return {};
        const auto p = binding.mouseAffine->inverse.map(QPointF(local - source.topLeft()));
        local = QPoint(int(std::floor(p.x())), int(std::floor(p.y()))) + source.topLeft();
    }
    if (!source.contains(local)) return {};
    return local;
}
std::optional<MapPointer> planeRelayHit(const MapPointer& pointer, const MapSprite& consumer,
                                      const MapSprite& ink, const MapSprite& master, QPoint pixel,
                                      const CompositionPlan::Binding& binding, std::optional<QPoint> picture = {},
                                      bool own = false) {
    // own: the consumer's own ink (an overlay of the relay), named as the relay whether or not it passes the mouse.
    if (consumer.kind != MapSprite::Screen || (consumer.passMouse && !own) || consumer.mouseTransparent ||
        consumer.mouseOpacity != 1 || consumer.maptext || consumer.tint[3] <= 0 || consumer.blend > 4 ||
        (consumer.planeEffects && !consumer.planeEffects->relayFilters())) return {};
    auto hit=pointer;
    hit.source.reset(); hit.relayHit.reset();
    nameSprite(hit,consumer,pixel);
    const QRect source = pointer.renderedSource;
    if (picture) {  // a relay of a plane names the picture's pixel, from its bottom left
        hit.click.iconX = picture->x() - source.left() + 1;
        hit.click.iconY = source.bottom() + 1 - picture->y();
    }
    if (consumer.planeEffects && consumer.planeEffects->matrix) {
        const auto& affine = binding.mouseAffine;
        if (!affine) return {};
        if (!picture) {
            const auto p=affine->inverse.map(QPointF(pixel-source.topLeft()));
            hit.click.iconX=int(std::floor(p.x()))+1;
            hit.click.iconY=source.height()-int(std::floor(p.y()));
        }
        hit.click.hasVis=affine->visualCoordinates;
        const auto bounds=affine->bounds.translated(source.topLeft()+
                                                    (picture ? CompositionPlan::relayOffset(consumer,source) : QPoint()));
        hit.click.visX=pixel.x()-bounds.x()+1;
        hit.click.visY=bounds.bottom()+1-pixel.y();
    }
    auto evidence=std::make_shared<RelayHit>();
    evidence->name=consumer.renderSource.isEmpty() ? consumer.planeSourceDependency : consumer.renderSource;
    evidence->plane=master.plane;
    // Copy only the small endpoint envelope, not the retained frame three times.
    auto endpoint=hit; endpoint.renderedScene.clear(); endpoint.composition.reset();
    evidence->consumer=endpoint;
    nameSprite(endpoint,master,pixel); endpoint.screen=master.hudIdentity!=0;
    if (endpoint.screen) endpoint.click.loc=proto::enc::nullRef();
    evidence->producer=endpoint;
    nameSprite(endpoint,ink,pixel);
    evidence->ink=std::make_shared<const MapPointer>(std::move(endpoint));
    evidence->producerAppearance=master.appearanceId;
    evidence->consumerAppearance=consumer.appearanceId;
    evidence->inkAppearance=ink.appearanceId;
    hit.relayHit=std::move(evidence);
    return hit;
}
// Viewport PASS_MOUSE captures preserve original world/HUD ownership and
// coordinates, including opacity-2 holes. Admitted source filters do not add
// generated ink to this traversal; affine/translated relays remain separate.
std::optional<MapPointer> planePassAt(const MapPointer& pointer, qsizetype consumerIndex,
                                    QPoint pixel, const CompositionPlan& plan, uint64_t& work) {
    if (!pointer.boundedFilterInput || !pointer.renderedSource.contains(pixel)) return {};
    auto path = std::make_shared<PlaneMousePath>();
    std::set<int> visiting;
    auto material = [](const MapSprite& s) {
        return PlaneSources::material(s) && !s.imageIdentity && !s.visHit && !s.localGroup;
    };
    auto unfiltered = [](const MapSprite& s) {
        return !s.planeEffects || (s.planeEffects->supported && !s.planeEffects->ignoredFilters &&
            s.planeEffects->filters.empty() && !s.planeEffects->matrix);
    };
    // The relay may stand anywhere and carry a matrix of its own; the master may carry one too. The pointer goes back
    // through the relay's, then the master's (stock 516.1689, plane-relay-world loc23, rturn90, rscalex2, mturn90).
    // The relay's own filters add and take away no ink: a blurred, outlined or recoloured picture is clicked where
    // its atoms' own pixels are (fblur-loc23 names part in the faded edge of its box, and floor in the blur beyond it;
    // foutline-loc23, foutline-rturn90, fcolor-loc23).
    auto neutral = [&](const MapSprite& s) {
        return material(s) && s.tint == std::array<float,4>{1,1,1,1} && s.color == identityColor &&
            (!s.planeEffects || s.planeEffects->relayFilters());
    };
    auto endpoint = [&](const MapSprite& s) {
        auto p = pointer;
        p.source.reset(); p.relayHit.reset(); p.planeMousePath.reset();
        p.renderedScene.clear(); p.composition.reset();
        nameSprite(p,s,pixel); p.screen = true; p.click.loc = proto::enc::nullRef();
        return p;
    };
    auto walk = [&](auto&& self, qsizetype index, QPoint at) -> std::optional<MapPointer> {
        if (!work || path->hops.size() >= 8) return {};
        --work;
        const auto& relay = plan.sprites[index]; const auto& binding = plan.bindings[size_t(index)];
        if (binding.refused || binding.localName || binding.planeSource < 0 ||
            relay.kind != MapSprite::Screen || !relay.passMouse || relay.mouseTransparent || relay.mouseOpacity != 1 ||
            !relay.haveKey || !relay.hudIdentity || !neutral(relay) || !relay.renderTarget.isEmpty()) return {};
        const auto local = relayPixel(relay, binding, at, plan.source);
        if (!local) return {};
        const int source = binding.planeSource;
        if (!visiting.insert(source).second) return {};
        const auto& surface = plan.planeSources.surfaces[source];
        const auto& master = plan.sprites[surface.master];
        // A transparent master does NOT suppress source-owned hits through a
        // mouse-opaque relay. Its own fallback rectangle is irrelevant here.
        if (!material(master) || !master.haveKey || !master.hudIdentity || !master.renderSource.isEmpty() ||
            (master.planeEffects && !PlaneSources::coverage(*master.planeEffects, plan.source.size())) ||
            plan.masterDisposition.at(master.plane) == CompositionPlan::MasterDisposition::Refused) {
            visiting.erase(source);
            return {};
        }
        const QPoint sample = planePixel(*local, master.plane, plan.source, plan.transforms, false);
        path->hops.push_back({endpoint(master),endpoint(relay),relay.renderSource,master.plane,
                             master.appearanceId,relay.appearanceId});
        for (auto it = surface.parts.rbegin(); it != surface.parts.rend(); ++it) {
            if (!work) break;
            --work;
            const auto& s = plan.sprites[*it];
            if (plan.bindings[size_t(*it)].refused || s.mouseTransparent || s.mouseOpacity == 0 || s.footprint) continue;
            if (!s.renderSource.isEmpty()) {
                if (auto hit = self(self,*it,sample)) return hit;
                continue;
            }
            if (!s.rect().contains(sample)) continue;
            const bool hud = s.kind == MapSprite::Screen && s.screenInteractive;
            const bool world = s.kind == MapSprite::Turf ||
                (s.kind == MapSprite::Entity && s.haveKey && s.targetIdentity);
            // A client image is clicked as its atom and a vis_contents member as itself, through the relay as where
            // they stand (stock 516.1689, plane-relay-world iv-loc23; plane-click-world gestures.json relay).
            const bool attached = s.screenInteractive && PlaneSources::material(s) && !s.localGroup &&
                ((s.kind == MapSprite::Image && s.imageIdentity) || (s.kind == MapSprite::Vis && s.visHit));
            if ((!hud && !world && !attached) || !s.inkInteractive || (!attached && !material(s)) || !unfiltered(s) ||
                (s.kind != MapSprite::Turf && s.atom == master.atom) || s.image.isNull() || s.mouseOpacity > 2 ||
                !s.renderTarget.isEmpty() || !s.planeSourceDependency.isEmpty() ||
                (!s.size.isEmpty() && s.size != s.image.size())) continue;
            if (s.mouseOpacity == 1 && s.image.pixelColor(sample-s.position).alpha() == 0) continue;
            auto hit = pointer; nameSprite(hit,s,sample);
            // An image or a member is checked by its own identity when the click is sent, not by the relay route.
            if (attached) return hit;
            path->leafAppearance = s.appearanceId; hit.planeMousePath = path;
            return hit;
        }
        path->hops.pop_back(); visiting.erase(source);
        return {};
    };
    return walk(walk,consumerIndex,pixel);
}
// The atom whose ink is at a point of a plane's own picture. Where a PASS_MOUSE relay of another plane stands on it, the
// ink there is that plane's, found the same way (stock 516.1689, plane-relay-world chain-bopaque-loc23: a relay without
// PASS_MOUSE of a plane that holds the first relay is clicked where the first plane's atoms are drawn).
qsizetype relayedInk(const CompositionPlan& plan, int index, QPoint sample, uint64_t& work, int depth) {
    const auto& surface = plan.planeSources.surfaces[size_t(index)];
    if (surface.input) return PlaneSources::ink(surface, plan.sprites, sample, work);
    if (!surface.relayInput || depth >= 8) return -1;
    for (auto it = surface.parts.rbegin(); it != surface.parts.rend(); ++it) {
        if (!work) return -1;
        --work;
        const auto& s = plan.sprites[*it];
        if (s.renderSource.isEmpty()) {
            if (s.rect().contains(sample) && s.image.pixelColor(sample - s.position).alpha()) return *it;
            continue;
        }
        const auto& b = plan.bindings[size_t(*it)];
        if (b.refused || b.localName || b.planeSource < 0 || !s.passMouse || s.mouseTransparent || s.mouseOpacity != 1)
            return -1;
        const auto local = relayPixel(s, b, sample, plan.source);
        if (!local) continue;
        const auto& inner = plan.sprites[plan.planeSources.surfaces[size_t(b.planeSource)].master];
        const auto ink = relayedInk(plan, b.planeSource, planePixel(*local, inner.plane, plan.source, plan.transforms, false),
                                    work, depth + 1);
        if (ink >= 0) return ink;
    }
    return -1;
}
// A relay without PASS_MOUSE is clicked itself where the plane's picture has ink, its matrix and its master's taken
// back (stock 516.1689, plane-relay-world nopass-loc23, nopass-rturn90, nopass-mturn90).
std::optional<MapPointer> planeRelayAt(const MapPointer& pointer, const MapSprite& consumer,
                                     QPoint pixel, const CompositionPlan& plan,
                                     const CompositionPlan::Binding& binding, uint64_t& work) {
    if (binding.planeSource < 0) return {};
    const auto* surface = &plan.planeSources.surfaces[binding.planeSource];
    const auto local = relayPixel(consumer, binding, pixel, pointer.renderedSource);
    if (!local) return {};
    const auto& master = plan.sprites[surface->master];
    const QPoint sample = planePixel(*local, master.plane, plan.source, plan.transforms, false);
    const auto ink=relayedInk(plan,binding.planeSource,sample,work,0);
    if (ink<0) return {};
    return planeRelayHit(pointer,consumer,plan.sprites[ink],master,pixel,binding,local);
}
}
proto::enc::AtomRef mapSpriteRef(const MapSprite& s) {
    return s.kind == MapSprite::Turf ? proto::enc::turfRef(s.cell.x(), s.cell.y()) :
        s.visHit && s.visHit->turf ? proto::enc::turfRef(s.visHit->cell[0] + 1, s.visHit->cell[1] + 1) :
        (s.atom & 0x80000000u) ? proto::enc::mobRef(s.atom & 0x7fffffffu) : proto::enc::objRef(s.atom);
}
QVector<MapPointer> mapPointerStack(const MapPointer& pointer) {
    QVector<MapPointer> result;
    if (!pointer.insideScene || pointer.outside || !pointer.composition || !pointer.composition->supported) return result;
    const auto& plan = *pointer.composition;
    const auto& hidden = pointer.boundedFilterInput ? plan.hiddenTree : plan.hidden;
    const auto& outlines = plan.outlines;
    const auto& transforms = plan.transforms;
    const auto& targets = plan.local;
    const QRect source = pointer.renderedSource;
    // A plane hidden because a relay draws it is still listed where its atoms stand (stock 516.1689, plane-relay-world
    // loc23 and nopass-loc23 at 139,109), as well as through a PASS_MOUSE relay where the relay draws them (loc23 at
    // 171,45, rturn90, mturn90-loc23). A relay without PASS_MOUSE is listed itself wherever its picture's rectangle is,
    // and lists nothing through it (nopass-loc23 at 171,45 and 100,150). With a matrix that rectangle is the view taken
    // through the matrix about the view's centre, without the move stock draws a neutral plane's picture with, and not
    // its bounding box: nopass-rhalf lists the relay in the view's middle half and not where it draws the boxes,
    // nopass-rt_45 in the turned square only (at 112,10 but not at 5,5 or 5,218). A relay standing on a plane that
    // another relay draws is listed where it stands, and lists through it when it passes the mouse (chain-bopaque-loc23).
    std::set<int> menuHidden;
    std::set_difference(hidden.begin(), hidden.end(), plan.captured.begin(), plan.captured.end(),
                        std::inserter(menuHidden, menuHidden.end()));
    const std::set<int> none;
    std::set<std::tuple<int,uint32_t,int,int>> seen;
    // at: the scene pixel, or the point in a plane's picture a PASS_MOUSE relay of it takes the pointer to (through).
    auto consider = [&](auto&& self, qsizetype i, QPoint at, int depth, bool through) -> void {
        if (result.size() == 128) return;
        const auto* it = &plan.sprites[i];
        const auto& binding = plan.bindings[size_t(i)];
        if (binding.refused) return;
        const auto& hiddenHere = through ? none : menuHidden;
        const auto scenePixel = groupPixel(at, binding);
        // The menu takes the pointer back through a plane's matrix as a left click does: the pixel whose corner the
        // inverse lands in, and for a part with a matrix of its own the unrounded point; such a part is listed where
        // its turned icon is, not over the whole of its bounds (stock 516.1689, plane-click-world turn90, tree45,
        // tree45-scalex2 right clicks). An alpha mask hides nothing from the menu, from an icon or a render target
        // (iconalpha, turn90-rtalpha).
        const auto pixel = planePixel(scenePixel, it->plane, source, transforms, false);
        auto hit = pointer;
        if (outlines.count(it->plane) && !outlinedInputPart(*it)) return;
        // A KEEP_TOGETHER relay is the root of its own appearance: it is listed like any relay. The relay's own overlays
        // are listed as the relay where their icons are, under a master's matrix too (stock 516.1689, plane-relay-world
        // nokt-rov, nokt-rov-mturn90). Another relay among its overlays is not established.
        if (!it->planeSourceDependency.isEmpty() && !it->renderSource.isEmpty() && it->renderSource != it->planeSourceDependency)
            return;
        if (!it->renderSource.isEmpty()) {
            // A relay standing on a turned plane that another relay draws is reached through that plane's matrix
            // (chain-m5turn90); one drawn on a turned plane directly is not established.
            if (!it->renderTarget.isEmpty() || hiddenHere.count(it->plane) || (transforms.count(it->plane) && !through))
                return;
            if (!binding.localName && binding.planeSource >= 0) {
                if (it->kind != MapSprite::Screen || depth >= 8) return;
                const auto local = relayPixel(*it, binding, pixel, source);
                if (!local) return;
                if (it->passMouse) {
                    // Every atom of the plane, an iconless one by its rectangle too: a vis_contents holder is listed
                    // after its member (stock 516.1689, plane-relay-world iv-loc23 at 207,15).
                    const auto& surface = plan.planeSources.surfaces[size_t(binding.planeSource)];
                    const int plane = plan.sprites[surface.master].plane;
                    for (qsizetype part = plan.sprites.size(); part-- > 0;)
                        if (plan.sprites[part].plane == plane && plan.sprites[part].kind != MapSprite::Master)
                            self(self, part, *local, depth + 1, true);
                    return;
                }
                if (it->mouseTransparent || it->mouseOpacity != 1) return;
                nameSprite(hit, *it, pixel);
            } else {
                const auto relay = relayAt(pointer,*it,pixel,targets,binding.localSource,true);
                if (!relay) return;
                hit = *relay;
            }
        } else {
            if (outsideWorldView(*it, pointer) ||
                (transforms.count(it->plane) && (!source.contains(pixel) || (!planeInputPart(*it) && !turnedPlanePart(*it)))) ||
                !binding.localVisible || !mouseCandidate(*it, pixel, hiddenHere) ||
                (it->affine && !it->image.isNull() &&
                 !turnedIconPixel(*it, scenePixel, pixel, source, transforms))) return;
            namePlaneSprite(hit, *it, scenePixel, source, transforms);
            hit.filteredPlane = outlines.count(it->plane) != 0;
        }
        hit.source.reset(); // a menu entry is not a gesture endpoint pair
        hit.kind = MapPointer::Click;
        const auto& ref = hit.click.over;
        if (!seen.emplace(ref.kind,ref.ref,ref.x,ref.y).second) return;
        result.push_back(std::move(hit));
    };
    for (qsizetype i = plan.sprites.size(); i-- > 0 && result.size() < 128;) consider(consider, i, pointer.scenePixel, 0, false);
    return result;
}
std::optional<MapPointer> mapPointerAt(QPointF p, QSizeF size, QSize view, QRect source,
                                     const MapGeometry& g, const QString& control, bool defaultMap, const MapSprites& sprites) {
    return mapPointerAtPresentation(p, mapPresentation(size, source.size(), g.tile).destination,
                        view, source, g, control, defaultMap, sprites);
}
std::optional<MapPointer> mapPointerAtPresentation(QPointF p, QRectF destination, QSize view, QRect source,
                                     const MapGeometry& g, const QString& control, bool defaultMap, const MapSprites& scene,
                                     std::shared_ptr<const CompositionPlan> composition) {
    if (!composition) composition = CompositionPlan::build(scene,source,{true});
    if (!composition->supported || composition->source != source) return {};
    const auto& plan = *composition;
    const auto& sprites = plan.sprites;
    if (view.isEmpty() || destination.isEmpty() || source.isEmpty() || g.tile <= 0 || control.isEmpty()) return {};
    const double x = (p.x() - destination.x()) * source.width() / destination.width();
    const double y = (p.y() - destination.y()) * source.height() / destination.height();
    // Tiny explicit zooms can put an outside pointer arbitrarily far from the
    // scene. Do not convert unbounded coordinates to wire/integer fields.
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > INT_MAX / 2.0 || std::abs(y) > INT_MAX / 2.0) return {};
    const int col = int(std::floor(x / g.tile)), row = int(std::floor(y / g.tile));
    const MapTurn turn(g.dir);
    const QRect worldSource = turn.rect(source - g.hud * g.tile);
    const QPoint worldPixel = turn.turned() ? turn.back(QPoint(int(std::floor(source.x() + x)), int(std::floor(source.y() + y))), worldSource)
                                            : QPoint();
    const int tx = turn.turned() ? int(std::floor(double(worldPixel.x()) / g.tile)) : int(std::floor((source.x() + x) / g.tile));
    const int ty = turn.turned() ? int(std::floor(double(worldPixel.y()) / g.tile)) : int(std::floor((source.y() + y) / g.tile));
    const int wx = g.region.x() + tx, wy = g.region.y() + g.region.height() - 1 - ty;
    MapPointer out;
    out.z = g.z; out.session = g.session;
    out.boundedFilterInput = out.fromDefaultMap = defaultMap;
    out.visibility=g.visibility;
    out.renderedScene=sprites; out.renderedSource=source; out.composition=std::move(composition);
    if (!g.hud.isNull()) out.worldView = worldSource;
    const bool inView = out.worldView.isNull() ||
        out.worldView.contains(QPoint(int(std::floor(source.x() + x)), int(std::floor(source.y() + y))));
    auto& c = out.click;
    c.control = control.toStdString();
    c.screenTileSize = g.tile;
    const std::string shortId = control.section('.', -1).toStdString();
    if (!defaultMap) { c.locStr = shortId; c.screenLocMap = shortId; }
    c.screenX = col + 1 - g.hud.left(); c.screenY = view.height() - g.hud.bottom() - row;
    c.screenPX = int(std::floor(x)) - col * g.tile + 1;
    c.screenPY = g.tile - (int(std::floor(y)) - row * g.tile);
    c.over = c.loc = proto::enc::nullRef();
    bool hiddenBackground=false, picked=false;
    if (x >= 0 && y >= 0 && x < view.width() * g.tile && y < view.height() * g.tile && inView &&
        wx >= 0 && wy >= 0 && wx < g.world.width() && wy < g.world.height()) {
        // A worker visibility frame accompanies the unified scene, including
        // admitted turf parts. Lighting/LOS admission is not evidence of a hit:
        // stock does not fall back through transparent/iconless/captured turfs.
        // Keep cell fallback only for the legacy flattened-terrain path.
        hiddenBackground=bool(g.visibility);
        c.over = c.loc = proto::enc::turfRef(wx + 1, wy + 1);
        c.iconX = int(std::floor(source.x() + x)) - tx * g.tile + 1;
        c.iconY = g.tile - (int(std::floor(source.y() + y)) - ty * g.tile);
    }
    if (x >= 0 && y >= 0 && x < view.width() * g.tile && y < view.height() * g.tile) {
        const QPoint pixel(int(std::floor(source.x() + x)), int(std::floor(source.y() + y)));
        out.scenePixel = pixel; out.insideScene = true;
        // Source-alpha/rectangle picking, not composited-surface hit testing.
        const auto& hidden = defaultMap ? plan.hiddenTree : plan.hidden;
        const auto& outlines = plan.outlines;
        const auto& transforms = plan.transforms;
        const auto& targets = plan.local;
        uint64_t sourceWork=65536;
        const auto* fallback = defaultMap && source.contains(pixel) && plan.fallback >= 0 ? &sprites[plan.fallback] : nullptr;
        for (qsizetype i = sprites.size(); i-- > 0;) {
            const auto* it = &sprites[i];
            const auto& binding = plan.bindings[size_t(i)];
            if (binding.refused) continue;
            // Members on the selected plane get first refusal. The master's
            // rectangle sits behind them, but ahead of every lower plane.
            if (fallback && it->plane < fallback->plane) break;
            // Stock's quarter-turned source edge tests the inverse integer
            // corner, not the rendered pixel center (which can still show ink).
            const QPoint partPixel = groupPixel(pixel, binding);
            const QPoint samplePixel = planePixel(partPixel,it->plane,source,transforms,false);
            // Only the recorded ordinary 32px source-alpha members are admitted.
            // No filtered relays, attachment masks, opacity-2 or halo geometry.
            if (outlines.count(it->plane) && !outlinedInputPart(*it)) continue;
            if (transforms.count(it->plane) && (!source.contains(samplePixel) || (!planeInputPart(*it) && !turnedPlanePart(*it)))) continue;
            if (!it->renderSource.isEmpty()) {
                if (!it->renderTarget.isEmpty() || hidden.count(it->plane) || transforms.count(it->plane)) continue;
                if (!it->planeSourceDependency.isEmpty() && (binding.localDependency || binding.planeDependency < 0)) continue;
                auto relay = relayAt(out,*it,samplePixel,targets,binding.localSource,false);
                if (!relay && !binding.localName) {
                    if (it->passMouse) relay = planePassAt(out,i,samplePixel,plan,sourceWork);
                    else relay = planeRelayAt(out,*it,samplePixel,plan,binding,sourceWork);
                }
                if (relay) { out = *relay; picked = true; break; }
                continue;
            }
            // Stock blackness is presentation-only: covered admitted atoms still hit.
            if (outsideWorldView(*it, out) ||
                !binding.localVisible || !mouseCandidate(*it, samplePixel, hidden) || it->footprint || it->image.isNull() ||
                plan.maskedOut(*it, partPixel, samplePixel) || renderMaskedOut(plan, *it, samplePixel) ||
                ((!it->inkInteractive || it->maptext) && it->mouseOpacity < 2) ||
                // Images extending a parent's opacity-2 rectangle are unsupported.
                (it->mouseOpacity == 2 && (it->imageIdentity || it->kind == MapSprite::Image))) continue;
            const QPoint local = samplePixel - it->position;
            QPoint sample(local.x() * it->image.width() / it->rect().width(),
                          local.y() * it->image.height() / it->rect().height());
            if (it->affine) {  // opacity 2 is the turned icon rectangle, not its bounds
                const auto icon = turnedIconPixel(*it,partPixel,samplePixel,source,transforms);
                if (!icon) continue;
                sample = *icon;
            }
            if (it->mouseOpacity < 2 && it->image.pixelColor(sample).alpha() == 0) continue;
            if (!it->planeSourceDependency.isEmpty()) {
                if (binding.planeDependency < 0 || binding.localDependency) continue;
                const auto* surface=&plan.planeSources.surfaces[binding.planeDependency];
                // The relay's own overlay is clicked as the relay, PASS_MOUSE or not, and under a master's matrix
                // (stock 516.1689, plane-relay-world nokt-rov, nokt-rov-mturn90).
                if (!surface->input || outlines.count(it->plane) || transforms.count(it->plane)) continue;
                const auto hit=planeRelayHit(out,*it,*it,sprites[surface->master],pixel,binding,{},true);
                if (!hit) continue;
                out=*hit;
            } else namePlaneSprite(out, *it, partPixel, source, transforms);
            out.filteredPlane = outlines.count(it->plane) != 0;
            picked=true;
            break;
        }
        if (!picked && fallback) {
            nameSprite(out,*fallback,pixel);
            out.screen = true; out.masterFallback = true;
            c.loc = proto::enc::nullRef();
            c.iconX = pixel.x()-source.x()+1;
            c.iconY = source.y()+source.height()-pixel.y();
            picked = true;
        }
    }
    // An unmatched unified-scene pixel has no target, even in a lit cell. Test
    // after sprite picking: admitted oversized appearances and HUD can still hit.
    if (hiddenBackground && !picked) {
        out.backgroundHidden=true;
        c.over=c.loc=proto::enc::nullRef();
    }
    return out;
}
bool mapPointerWanted(MapPointer::Kind kind, const proto::enc::MouseHooks& h, bool wheel) {
    switch (kind) {
    case MapPointer::Down: return h.down;
    case MapPointer::Up: return h.up;
    case MapPointer::Click: return h.click;
    case MapPointer::DoubleClick: return h.dblClick;
    case MapPointer::Wheel: return wheel;
    case MapPointer::Drag: return h.drag;
    case MapPointer::Drop: return h.drop;
    case MapPointer::Hover: case MapPointer::Leave: case MapPointer::Released: return false; // stateful subscription path
    }
    return false;
}
MapPopupAction mapPopupAction(const MapPointer& p, bool popupMenus, bool pressSent) {
    if (p.click.button != "right" || p.kind == MapPointer::Wheel || p.kind == MapPointer::Hover ||
        p.kind == MapPointer::Leave || p.kind == MapPointer::Released) return MapPopupAction::None;
    // Stock 516.1684 (tests/data/popupmenu-world, parts A and C): flag off at the press and on at the release still
    // sends MouseUp and Click and opens no menu; on then off sends them with no MouseDown. The press that closes an open
    // verb menu sends no MouseDown either way.
    if (p.kind == MapPointer::Down && p.closedPopup) return MapPopupAction::Consume;
    if (!popupMenus || (pressSent && p.kind != MapPointer::Down)) return MapPopupAction::None;
    // Independent held-button captures: stock consumes the press but opens on
    // release/Click, not Down. Other right hooks must not also reach the server.
    return p.kind == MapPointer::Click ? MapPopupAction::Open : MapPopupAction::Consume;
}
bool mapPointerCurrent(const MapPointer& p, const model::World& w, uint64_t session) {
    if (p.session != session || (p.surface && p.surface->expired())) return false;
    if (p.kind == MapPointer::Leave) return true;
    if (p.masterFallback && p.kind != MapPointer::Down && p.kind != MapPointer::Up && p.kind != MapPointer::Click) return false;
    // An unrendered pixel names nothing: a hover there (no atom, as over the letterbox: stock 516.1689 names no turf
    // without an icon, tests/data/hover-world extra void1), or a gesture's destination.
    if (p.backgroundHidden && p.kind != MapPointer::Hover &&
        (!p.source || (p.kind!=MapPointer::Drag && p.kind!=MapPointer::Drop && p.kind!=MapPointer::Up))) return false;
    if (p.z != w.viewZ) return false;
    const bool dragging = p.kind == MapPointer::Drag || p.kind == MapPointer::Drop;
    // Stock 516.1689 drags and drops a client image's atom, a vis_contents member and what a PASS_MOUSE plane relay
    // shows like any other atom, naming what a click at each point names (plane-click-world gestures.json). A relay
    // that takes the pointer itself is not measured and still takes no part in a drag.
    auto unmeasured = [](const MapPointer& end) { return bool(end.relayHit); };
    if (dragging && (!p.source || p.source->masterFallback || unmeasured(p) || unmeasured(*p.source) ||
        p.source->click.over.kind == proto::enc::RefNull)) return false;
    if (p.source && (p.source->source || p.source->session != p.session || p.source->z != p.z ||
        !mapPointerCurrent(*p.source, w, session))) return false;
    const auto& c = p.click;
    auto scopeCurrent = [&](uint32_t root) {
        const auto hud = w.hudAppid.find(root);
        if (hud == w.hudAppid.end()) return false;
        const auto ap = w.appearances.find(hud->second);
        if (ap == w.appearances.end()) return c.screenLocMap.empty();
        const auto target = ScreenLoc::target(QString::fromStdString(ap->second.fields.screenLoc));
        return target && target->map == QString::fromStdString(c.screenLocMap);
    };
    // A scope-only update need not change a HUD's membership/key. Old rendered
    // hits must not remain usable after that owner moves to a different MAP.
    if (p.screen && !scopeCurrent(p.visHit ? p.visHit->hudRoot : c.over.ref)) return false;
    if (p.planeMousePath) {
        const auto& path = *p.planeMousePath;
        if (p.kind != MapPointer::Down && p.kind != MapPointer::Up && p.kind != MapPointer::Click && p.kind != MapPointer::Hover &&
            !dragging) return false;
        if (path.hops.empty() || path.hops.size() > 8 || p.relayHit || p.imageIdentity || p.visHit) return false;
        auto current = [&](const MapPointer& endpoint, uint32_t appearance) {
            const auto ap = w.hudAppid.find(endpoint.click.over.ref);
            return !endpoint.planeMousePath && !endpoint.relayHit && !endpoint.source && endpoint.screen &&
                endpoint.click.over.kind == proto::enc::RefObj && ap != w.hudAppid.end() && ap->second == appearance && mapPointerCurrent(endpoint,w,session);
        };
        if (p.screen) {
            const auto leaf = w.hudAppid.find(c.over.ref);
            if (c.over.kind != proto::enc::RefObj || leaf == w.hudAppid.end() || leaf->second != path.leafAppearance) return false;
        } else if (c.over.kind == proto::enc::RefTurf) {
            const auto leaf = w.tiles.find({c.over.x,c.over.y,uint16_t(p.z)});
            if (leaf == w.tiles.end() || leaf->second != path.leafAppearance) return false;
        } else if (c.over.kind == proto::enc::RefObj || c.over.kind == proto::enc::RefMob) {
            const auto key = c.over.ref | (c.over.kind == proto::enc::RefMob ? model::kRegistryA : 0);
            const auto leaf = w.atoms.find(key);
            if (!p.haveAtomKey || !p.targetIdentity || leaf == w.atoms.end() || leaf->second.appid != path.leafAppearance) return false;
        } else return false;
        for (const auto& hop : path.hops) {
            if (!current(hop.master,hop.masterAppearance) || !current(hop.relay,hop.relayAppearance)) return false;
            try {
                const proto::Appearance& m = w.appearances.at(hop.masterAppearance).fields;
                const proto::Appearance& r = w.appearances.at(hop.relayAppearance).fields;
                // The relay is the screen object itself or one of its overlays or underlays (stock 516.1689,
                // plane-relay-world ovrelay-*: an overlay with a render source draws and passes the pointer like a relay).
                auto relays = [&](const proto::Appearance& a) {
                    return QString::fromStdString(a.renderSource) == hop.name && (a.appearanceFlags & 0x80000000u);
                };
                bool relayed = relays(r);
                for (const auto* children : {&r.overlays, &r.underlays})
                    for (const uint32_t id : *children) {
                        const auto child = w.appearances.find(id);
                        relayed = relayed || (child != w.appearances.end() && relays(child->second.fields));
                    }
                if (!SceneAppearance(m).master || resolvePlane(m.plane) != hop.plane ||
                    QString::fromStdString(m.renderTarget) != hop.name || !relayed) return false;
            } catch (const std::exception&) { return false; }
        }
    }
    if (p.relayHit) {
        const auto& relay = *p.relayHit;
        if (relay.plane && p.kind != MapPointer::Down && p.kind != MapPointer::Up && p.kind != MapPointer::Click) return false;
        if (relay.producer.relayHit || relay.consumer.relayHit ||
            !mapPointerCurrent(relay.producer,w,session) || !mapPointerCurrent(relay.consumer,w,session)) return false;
        auto appearanceId = [&](const MapPointer& endpoint) -> uint32_t {
            uint32_t appid = 0;
            if (endpoint.imageIdentity) appid = w.imageInstances.at(endpoint.imageId).appid;
            else if (endpoint.click.over.kind == proto::enc::RefTurf) {
                const auto tile=w.tiles.find({endpoint.click.over.x,endpoint.click.over.y,uint16_t(endpoint.z)});
                return tile==w.tiles.end() ? 0 : tile->second;
            }
            else if (endpoint.screen) appid = w.hudAppid.at(endpoint.click.over.ref);
            else {
                const uint32_t key = endpoint.click.over.ref | (endpoint.click.over.kind == proto::enc::RefMob ? model::kRegistryA : 0);
                const auto atom = w.atoms.find(key);
                if (atom == w.atoms.end()) return 0;
                appid = atom->second.appid;
            }
            return appid;
        };
        static const proto::Appearance none;
        auto fields = [&](const MapPointer& endpoint) -> const proto::Appearance& {
            const auto ap = w.appearances.find(appearanceId(endpoint));
            return ap == w.appearances.end() ? none : ap->second.fields;
        };
        try {
            const proto::Appearance* consumerFields = &fields(relay.consumer);
            if (!relay.plane) {
                if ((relay.producerAppearance && appearanceId(relay.producer) != relay.producerAppearance) ||
                    (relay.consumerAppearance && appearanceId(relay.consumer) != relay.consumerAppearance)) return false;
                if (relay.consumerPath) {
                    const auto& path = *relay.consumerPath;
                    if (path.empty() || path.size() > 32 || path.front() != appearanceId(relay.consumer)) return false;
                    for (size_t i=1;i<path.size();++i) {
                        auto includes = [&](const std::vector<uint32_t>& children) {
                            return std::find(children.begin(),children.end(),path[i])!=children.end();
                        };
                        if (!includes(consumerFields->underlays) && !includes(consumerFields->overlays)) return false;
                        const auto ap=w.appearances.find(path[i]);
                        if (ap==w.appearances.end()) return false;
                        consumerFields=&ap->second.fields;
                    }
                }
            }
            if (QString::fromStdString(fields(relay.producer).renderTarget) != relay.name ||
                QString::fromStdString(consumerFields->renderSource) != relay.name) return false;
            if (relay.plane) {
                if (!relay.ink || relay.ink->relayHit || !mapPointerCurrent(*relay.ink,w,session) ||
                    appearanceId(relay.producer)!=relay.producerAppearance ||
                    appearanceId(relay.consumer)!=relay.consumerAppearance ||
                    appearanceId(*relay.ink)!=relay.inkAppearance) return false;
                const proto::Appearance& master=fields(relay.producer);
                if (!SceneAppearance(master).master || resolvePlane(master.plane)!=*relay.plane) return false;
            }
        } catch (const std::exception&) { return false; }
    }
    if (p.visHit) {
        // A mob member is clicked as that mob, like an obj member as that obj; a copied turf as that turf (stock
        // 516.1688, tests/data/click-world mob-member, turf-member).
        const bool mob = (p.visHit->target & model::kRegistryA) != 0;
        const auto& hitCell = p.visHit->cell;
        // Hover reaches a member like a click does (stock 516.1689: MouseEntered on a turf shown from another z).
        if (p.visHit->path.empty() || p.visHit->path.size() >= 16) return false;
        if (p.visHit->turf ? c.over.kind != proto::enc::RefTurf || c.over.x != hitCell[0] || c.over.y != hitCell[1] :
            c.over.kind != (mob ? proto::enc::RefMob : proto::enc::RefObj) ||
            c.over.ref != (p.visHit->target & ~model::kRegistryA)) return false;
        auto turfRevision = [&](const std::array<uint16_t,3>& cell) {
            const auto revision = w.tileRevision.find(cell);
            return revision == w.tileRevision.end() ? uint64_t(0) : revision->second;
        };
        for (size_t at = 0; at < p.visHit->path.size(); ++at) {
            const auto& edge = p.visHit->path[at];
            if (!edge.membership) {
                // An atom standing on a copied turf: still there, still the same atom.
                if (!(edge.holder & model::VisContents::turfBit)) return false;
                const auto child = w.atoms.find(edge.child);
                if (child == w.atoms.end() || child->second.renderIdentity != edge.childIdentity || w.resync.freed.count(edge.child) ||
                    !child->second.hasPos || child->second.x != uint16_t(edge.holder) + 1 ||
                    child->second.y != uint16_t(edge.holder >> 16) + 1 || child->second.z != uint16_t(edge.holder >> 32)) return false;
                continue;
            }
            if (!edge.child) {
                // A turf member: the list still holds that turf, unchanged since it was drawn.
                const auto list = w.vis.lists.find(edge.holder);
                const auto cell = at + 1 < p.visHit->path.size() ?
                    std::array<uint16_t,3>{uint16_t(p.visHit->path[at+1].holder), uint16_t(p.visHit->path[at+1].holder >> 16),
                                           uint16_t(p.visHit->path[at+1].holder >> 32)} : hitCell;
                const uint32_t flat = (uint32_t(cell[2]) * uint32_t(w.bounds.maxY) + cell[1]) * uint32_t(w.bounds.maxX) + cell[0];
                if (list == w.vis.lists.end() || list->second.identity != edge.membership ||
                    edge.index >= list->second.members.size() || list->second.members[edge.index].reference.kind != 1 ||
                    list->second.members[edge.index].reference.ref != flat || !w.tiles.count(cell) ||
                    turfRevision(cell) != edge.childIdentity) return false;
                continue;
            }
            const auto list = w.vis.lists.find(edge.holder);
            const auto holder = w.atoms.find(uint32_t(edge.holder));
            const auto child = w.atoms.find(edge.child);
            if (list == w.vis.lists.end() || list->second.identity != edge.membership ||
                edge.index >= list->second.members.size() ||
                child == w.atoms.end() || child->second.renderIdentity != edge.childIdentity || w.resync.freed.count(edge.child)) return false;
            if (edge.holder & model::VisContents::turfBit) {
                const uint16_t x = uint16_t(edge.holder), y = uint16_t(edge.holder >> 16), z = uint16_t(edge.holder >> 32);
                if (z != p.z || !w.tiles.count({x,y,z})) return false;
            } else if (edge.holder & model::VisContents::imageBit) {
                const uint32_t id = uint32_t(edge.holder);
                const auto im = w.imageInstances.find(id);
                if (im == w.imageInstances.end() || im->second.targetHud || im->second.identity != list->second.holderIdentity ||
                    !w.imageTargetCurrent(id, im->second.identity, im->second.target, im->second.targetIdentity)) return false;
                const auto& root = w.atoms.at(im->second.target);
                if (!root.hasPos || root.z != p.z) return false;
            } else if (holder == w.atoms.end() || holder->second.renderIdentity != list->second.holderIdentity ||
                w.resync.freed.count(uint32_t(edge.holder))) return false;
            const auto& member = list->second.members[edge.index];
            if (!model::VisContents::movable(member.reference) || model::VisContents::atom(member.reference) != edge.child ||
                member.childIdentity != edge.childIdentity) return false;
        }
        const auto rootKey = p.visHit->path.front().holder;
        const auto root = w.atoms.find(uint32_t(rootKey));
        const bool special = (rootKey & (model::VisContents::turfBit | model::VisContents::imageBit)) != 0;
        const auto target = w.atoms.find(p.visHit->target);
        const bool hud=p.visHit->hudMembership && p.screen && p.visHit->hudRoot==rootKey &&
            w.hudObjectCurrent(p.visHit->hudRoot,p.visHit->hudKey,p.visHit->hudMembership);
        if (p.screen && !hud) return false;
        return (hud || special || (root != w.atoms.end() && root->second.hasPos && root->second.z == p.z)) &&
            (p.visHit->turf ? w.tiles.count(hitCell) && turfRevision(hitCell) == p.visHit->targetIdentity :
             target != w.atoms.end() && target->second.renderIdentity == p.visHit->targetIdentity);
    }
    const uint32_t key = c.over.ref | (c.over.kind == proto::enc::RefMob ? model::kRegistryA : 0);
    if (p.imageIdentity) {
        if (c.over.kind == proto::enc::RefTurf) {
            if (!w.imageTurfCurrent(p.imageId,p.imageIdentity,{uint16_t(c.over.x),uint16_t(c.over.y),uint16_t(p.z)})) return false;
        } else if ((c.over.kind != proto::enc::RefObj && c.over.kind != proto::enc::RefMob) ||
            !w.imageTargetCurrent(p.imageId, p.imageIdentity, key, p.targetIdentity)) return false;
    }
    if (c.over.kind == proto::enc::RefTurf) {
        if (!p.turfDimensions.isEmpty()) {
            if (p.turfDimensions != QSize(w.bounds.maxX,w.bounds.maxY)) return false;
            const std::array<uint16_t,3> cell{uint16_t(c.over.x),uint16_t(c.over.y),uint16_t(p.z)};
            if (!w.tiles.count(cell)) return false;
            const auto revision=w.tileRevision.find(cell);
            if (p.targetIdentity != (revision==w.tileRevision.end() ? 0 : revision->second)) return false;
        }
        return c.over.x < w.bounds.maxX && c.over.y < w.bounds.maxY;
    }
    if (p.screen) return c.over.kind == proto::enc::RefObj && p.haveAtomKey &&
        w.hudObjectCurrent(c.over.ref, p.atomKey, p.hudIdentity ? p.hudIdentity : p.targetIdentity);
    if (c.over.kind != proto::enc::RefObj && c.over.kind != proto::enc::RefMob) return c.over.kind == proto::enc::RefNull;
    const auto atom = w.atoms.find(key);
    return atom != w.atoms.end() && atom->second.hasPos && atom->second.z == p.z && !w.resync.freed.count(key) &&
        (!p.haveAtomKey || (atom->second.haveKey && atom->second.key == p.atomKey)) &&
        (!p.targetIdentity || atom->second.renderIdentity == p.targetIdentity);
}
std::optional<MapPointerTarget> mapPointerTarget(const MapPointer& p, const model::World& w) {
    const auto& over = p.click.over;
    MapPointerTarget out;
    auto atom = [&](uint32_t key) {
        out.kind = MapPointerTarget::Atom;
        out.atom = key;
        if (const auto a = w.atoms.find(key); a != w.atoms.end()) out.appid = a->second.appid;
    };
    auto turf = [&](std::array<uint16_t,3> cell) {
        out.kind = MapPointerTarget::Turf;
        out.cell = cell;
        if (const auto t = w.tiles.find(cell); t != w.tiles.end()) out.appid = t->second;
    };
    if (p.visHit) {
        if (p.visHit->turf) turf(p.visHit->cell); else atom(p.visHit->target);
    } else if (over.kind == proto::enc::RefTurf) {
        if (p.screen) return std::nullopt;
        turf({uint16_t(over.x), uint16_t(over.y), uint16_t(p.z)});
    } else if (over.kind != proto::enc::RefObj && over.kind != proto::enc::RefMob) {
        return std::nullopt;
    } else if (p.screen) {
        out.kind = MapPointerTarget::Hud;
        out.atom = over.ref;
        if (const auto h = w.hudAppid.find(over.ref); h != w.hudAppid.end()) out.appid = h->second;
    } else {
        atom(over.ref | (over.kind == proto::enc::RefMob ? model::kRegistryA : 0));
    }
    return out;
}
std::string mapHoverName(const MapPointer& p, const model::World& w) {
    if (p.kind != MapPointer::Hover) return {};
    const auto named = mapPointerTarget(p, w);
    if (!named || !named->appid) return {};
    const auto a = w.appearances.find(*named->appid);
    return a == w.appearances.end() ? std::string() : model::plainAtomName(a->second.fields.name);
}
bool sameMapTarget(const MapPointer& a, const MapPointer& b) {
    const auto& x = a.click.over; const auto& y = b.click.over;
    if (bool(a.planeMousePath) != bool(b.planeMousePath)) return false;
    if (a.planeMousePath) {
        const auto& xh = a.planeMousePath->hops; const auto& yh = b.planeMousePath->hops;
        if (xh.size() != yh.size()) return false;
        for (size_t i = 0; i < xh.size(); ++i)
            if (xh[i].name != yh[i].name || xh[i].plane != yh[i].plane ||
                !sameMapTarget(xh[i].master,yh[i].master) || !sameMapTarget(xh[i].relay,yh[i].relay)) return false;
    }
    if (bool(a.relayHit) != bool(b.relayHit) || (a.relayHit &&
        (a.relayHit->name != b.relayHit->name || a.relayHit->plane != b.relayHit->plane || !sameMapTarget(a.relayHit->producer,b.relayHit->producer) ||
         !sameMapTarget(a.relayHit->consumer,b.relayHit->consumer)))) return false;
    if (bool(a.surface) != bool(b.surface) || (a.surface &&
        (a.surface->owner_before(*b.surface) || b.surface->owner_before(*a.surface)))) return false;
    return a.session == b.session && a.z == b.z && a.click.control == b.click.control && a.outside == b.outside &&
        x.kind == y.kind && x.ref == y.ref && x.x == y.x && x.y == y.y &&
        a.screen == b.screen && a.masterFallback == b.masterFallback && a.haveAtomKey == b.haveAtomKey && a.atomKey == b.atomKey &&
        a.visHit == b.visHit && a.imageId == b.imageId && a.imageIdentity == b.imageIdentity && a.targetIdentity == b.targetIdentity && a.hudIdentity == b.hudIdentity;
}
void MapHover::update(const MapPointer& p, bool current, const Gate& g, int64_t nowNs) {
    // Stock 516.1689 (tests/data/hover-world): appearance flag 0x800 comes with MouseEntered, MouseExited, MouseMove or
    // MouseWheel, 0x80000 with MouseMove alone (0x100000 with MouseMove or MouseWheel).
    constexpr uint32_t kHoverProcs = 0x800, kMoveProc = 0x80000;
    if (p.kind == MapPointer::Leave) {
        run_.reset();
        // Off the map: an over report naming nothing, null control and no params, then the record is gone. With no
        // record (nothing picked since a release) nothing is sent. It goes out after a report still waiting.
        if (!record_) return;
        record_.reset();
        if (g.clientHover || (g.sentFlags & kHoverProcs)) {
            auto leave = p;
            leave.click.over = leave.click.loc = proto::enc::nullRef();
            queue(leave, nowNs);
        }
        return;
    }
    if (!current || p.kind != MapPointer::Hover) return;
    // Within one tick Windows hands stock the pointer's last position alone: the picks before it in the tick never
    // happened, and neither did their report.
    Gate gate = g;
    const int64_t tick = nowNs / kPeriodNs;
    if (run_ && run_->tick == tick) {
        record_ = run_->record;
        gate.sentFlags = run_->sentFlags;
        if (run_->queued) {
            waiting_.pop_back();
            dueNs_.pop_back();
        }
        run_->queued = false;
    } else {
        run_ = Run{tick, record_, g.sentFlags, false};
    }
    const auto& c = p.click;
    auto pixel = [&](int v, int fallback) {
        const int tile = c.screenTileSize > 0 ? c.screenTileSize : 32;
        return std::clamp(v ? v : fallback, 1, tile);
    };
    Record r{c.over, c.control, c.locStr, pixel(c.screenPX, c.iconX), pixel(c.screenPY, c.iconY)};
    const bool over = !record_ || record_->over.kind != r.over.kind || record_->over.ref != r.over.ref ||
                      record_->over.x != r.over.x || record_->over.y != r.over.y || record_->control != r.control ||
                      record_->map != r.map;
    const bool moved = !over && !p.info && r.over.kind != proto::enc::RefNull &&
                       (record_->px != r.px || record_->py != r.py);
    record_ = std::move(r);
    if (over ? gate.clientHover || (gate.flags & kHoverProcs) || (gate.sentFlags & kHoverProcs)
             : moved && (gate.clientMove || (gate.flags & kMoveProc))) {
        queue(p, nowNs);
        run_->queued = true;
    }
}
void MapHover::queue(const MapPointer& p, int64_t nowNs) {
    dueNs_.push_back((nowNs / kPeriodNs + 1) * kPeriodNs);
    waiting_.push_back(p);
    waiting_.back().source.reset();
}
Bytes MapHover::due(int64_t nowNs) {
    if (waiting_.empty() || nowNs < dueNs_.front()) return {};
    MapPointer next = std::move(waiting_.front());
    waiting_.erase(waiting_.begin());
    dueNs_.erase(dueNs_.begin());
    auto body = mapPointerBody(next);
    if (next.kind == MapPointer::Hover && next.click.over.kind != proto::enc::RefNull) sent_ = std::move(next);
    else sent_.reset();
    return body;
}
const MapPointer* MapHover::last() const {
    if (waiting_.empty()) return sent();
    const auto& newest = waiting_.back();
    return newest.kind == MapPointer::Hover && newest.click.over.kind != proto::enc::RefNull ? &newest : nullptr;
}
Bytes mapPointerBody(const MapPointer& pointer) {
    // A turf shown from another z (a vis_contents member) is named by its id, since a cell names the turf on the map's
    // own z (stock 516.1689: a click on a z 2 turf shown that way reaches the z 2 turf).
    auto wire = [](MapPointer q) {
        if (q.visHit && q.visHit->turf && q.visHit->cell[2] != q.z && q.click.over.kind == proto::enc::RefTurf) {
            q.click.over = {};
            q.click.over.kind = proto::enc::RefTurfId;
            q.click.over.ref = q.visHit->turfId;
        }
        return q;
    };
    const MapPointer p = wire(pointer);
    auto c = p.click;
    if (p.kind == MapPointer::Leave) return proto::enc::encodeHoverLeave();
    if (p.info) {  // the stat panel: the INFO click's shape (proto::enc::infoHoverParams)
        if (p.kind != MapPointer::Hover) return {};
        const bool atom = c.over.kind != proto::enc::RefNull;
        return proto::enc::encodeInfoClick(proto::enc::kMouseHover, c.over, c.locStr, c.control,
                                           atom ? proto::enc::infoHoverParams(c.mods, p.infoIcon, c.iconX, c.iconY)
                                                : std::string());
    }
    // A second map is named by its short id when it is hovered or pressed (as the pointer carries it), and by its full
    // id in a release, a click, a double click and a drag step (stock 516.1689: DOWN "map2", UP and CLICK
    // "main.map2").
    const bool secondary = !p.outside && !c.locStr.empty();
    if (secondary && (p.kind == MapPointer::Up || p.kind == MapPointer::Click || p.kind == MapPointer::DoubleClick ||
                      p.kind == MapPointer::Drag))
        c.locStr = c.screenLocMap = c.control;
    if (p.kind == MapPointer::Drag || p.kind == MapPointer::Drop) {
        if (!p.source) return {};
        const auto src = wire(*p.source).click;
        proto::enc::DragCmd d;
        d.drop = p.kind == MapPointer::Drop;
        d.src = src.over; d.srcLoc = src.loc; d.srcLocStr = src.locStr; d.srcControl = src.control;
        d.over = c.over; d.overLoc = c.loc; d.overLocStr = c.locStr; d.overControl = c.control;
        d.overLocBlank = c.over.kind == proto::enc::RefNull && c.locStr.empty();
        if (secondary) {
            // Over a second map: a turf named by its cell is its own location. A drop's over_location is otherwise the
            // source's short id on the source's own map, a turf named by id itself, and else null (stock 516.1689).
            // A drop's screen-loc carries the source map's short id.
            if (c.over.kind == proto::enc::RefTurf) {
                d.overLoc = c.over;
                d.overLocStr.clear();
            } else if (d.drop) {
                d.overLoc = c.over.kind == proto::enc::RefTurfId && c.control != src.control ? c.over : proto::enc::nullRef();
                d.overLocStr = c.control == src.control ? src.locStr : std::string();
                d.overLocBlank = false;
            }
        }
        // On a map, whichever it is, a drop's screen-loc carries the source map's short id; off every map none.
        if (d.drop) c.screenLocMap = p.outside ? std::string() : src.locStr;
        // Released on a map's empty area, the drop carries the press's icon-x/icon-y.
        if (d.drop && !p.outside && c.over.kind == proto::enc::RefNull) {
            c.iconX = src.iconX;
            c.iconY = src.iconY;
        }
        // Another map's empty area (MapWidget::gesturePointer sets its id as the location): the drag step names the map
        // as over_location, the drop only as over_control (stock 516.1689).
        const bool otherMap = p.outside && c.over.kind == proto::enc::RefNull && !c.locStr.empty();
        if (otherMap && d.drop) {  // released there, the source itself is what the drop is over, at its own location
            d.over = src.over; d.overLoc = src.loc; d.overLocStr = src.locStr;
            d.overLocBlank = false;
        }
        d.overControlNull = p.outside && d.drop && !otherMap;
        d.params = proto::enc::clickParams(c);
        // A drag step over a map's empty area carries no icon-x/icon-y, only what is held and the cell (the same
        // world: "left=1;button=left;screen-loc=7:17,14:9" on the letterbox).
        if (!p.outside && !d.drop && c.over.kind == proto::enc::RefNull)
            if (const auto button = d.params.find(c.button + "=1"); button != std::string::npos) d.params.erase(0, button);
        // Stock outside motion has no icon/screen coordinates. Outside release
        // retains the press coordinates, while naming no destination atom/control.
        if (p.outside && !d.drop) {
            const auto button = d.params.find(c.button + "=1");
            d.params = p.dragCell ? d.params.substr(button) : d.params.substr(button, d.params.find(";screen-loc=") - button);
        }
        // Outside every window: the drag step names the source's own map, and the drop carries the press's cell
        // without icon-x/icon-y (the same world: "main.map main.map left=1;button=left", then
        // "left=1;button=left;screen-loc=9:16,9:16").
        if (p.outside && c.control.empty()) {
            if (!d.drop) {
                d.overControl = src.control;
                d.overLocStr = src.locStr.empty() ? std::string() : src.control;
                d.overLocBlank = d.overLocStr.empty();
            } else if (const auto button = d.params.find(c.button + "=1"); button != std::string::npos)
                d.params.erase(0, button);
        }
        return proto::enc::encodeDragCmd(d);
    }
    if (p.kind == MapPointer::Hover) {
        auto params = proto::enc::hoverParams(c);
        if (c.over.kind == proto::enc::RefNull) params.erase(0, params.find("screen-loc="));
        return proto::enc::encodeHover(c.over, c.loc, c.control, params, c.locStr);
    }
    if (p.kind == MapPointer::Wheel) {
        proto::enc::WheelCmd w;
        w.over = c.over; w.dx = p.dx; w.dy = p.dy;
        w.control = c.control;
        w.location = c.control.substr(c.control.find_last_of('.') + 1);
        c.screenLocMap = w.location;
        w.params = c.button.empty() ? proto::enc::hoverParams(c) : proto::enc::clickParams(c);
        if (c.over.kind == proto::enc::RefNull) {
            // No icon coordinates when the pointer is over a letterbox/unknown tile.
            auto end = w.params.find(';', w.params.find(';') + 1);
            w.params.erase(0, end + 1);
        }
        return proto::enc::encodeWheelCmd(w);
    }
    switch (p.kind) {
    case MapPointer::Down: c.sub = proto::enc::kMouseDown; break;
    case MapPointer::Up: c.sub = proto::enc::kMouseUp; break;
    case MapPointer::DoubleClick: c.sub = proto::enc::kMouseDblClick; break;
    default: c.sub = proto::enc::kMouseClick; break;
    }
    // Released over another map's empty area: MouseUp goes to the pressed atom, with that map as its location (stock
    // 516.1689: "UP the thing main.map2 main.map2 ...screen-loc=main.map2:9:16,9:16").
    if (p.kind == MapPointer::Up && p.outside && p.source && c.over.kind == proto::enc::RefNull && !c.locStr.empty() &&
        p.source->click.over.kind != proto::enc::RefNull) {
        c.over = wire(*p.source).click.over;
        c.screenLocMap = c.locStr;
    }
    if (c.over.kind != proto::enc::RefNull) return proto::enc::encodeMouseCmd(c);
    if (p.outside && !c.control.empty())
        return proto::enc::encodeMouseNone(c.sub, c.locStr, c.control, proto::enc::clickParams(c));
    if (p.kind == MapPointer::Up && p.source) {  // released on a map's empty area: the press's icon-x/icon-y, this cell
        c.iconX = p.source->click.iconX;
        c.iconY = p.source->click.iconY;
        return proto::enc::encodeMouseNone(c.sub, c.locStr, c.control, proto::enc::clickParams(c));
    }
    std::string params = proto::enc::clickParams(c);
    auto end = params.find(';', params.find(';') + 1);
    params.erase(0, end + 1);
    return proto::enc::encodeMouseNone(c.sub, c.locStr, c.control, params);
}
}
