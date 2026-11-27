// Immutable, model-free sprite values shared by the worker, presenter and picker.
#pragma once
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <array>
#include <cstdint>
#include "shell/sceneappearance.h"
#include "shell/staticaffine.h"
#include "shell/planeeffects.h"
#include <QString>
#include <memory>
#include <vector>

namespace fugue::shell {
// Anonymous appearance capture, independent of named render targets/planes.
// Coordinates are template-local; each occurrence supplies its own immutable origin.
struct AppearanceGroup {
    std::shared_ptr<const AppearanceGroup> parent;
    std::array<float,4> tint{1,1,1,1};
    ColorMatrix color = identityColor;
    int blend = 0;
    float layer = 0;
    QTransform transform;
    std::array<double,6> matrix{1,0,0,0,1,0}; // inherited by KEEP_APART about the detached icon's own pivot
    // An atom's own filters, run on the group's surface. A group made only for them (no KEEP_TOGETHER) keeps its parts'
    // ordinary input: the filter ink is not hit.
    std::shared_ptr<const PlaneEffects> effects;
    // Not a KEEP_TOGETHER group: the surface of one appearance's own icon and maptext, for its filters or because
    // maptext shares a surface with its icon. The icon counts whole in it, not by its ink.
    bool filterOnly = false;
    bool nearest = false; // PIXEL_SCALE: the matrix samples the surface unsmoothed
    bool operator==(const AppearanceGroup& other) const {
        return tint==other.tint && color==other.color && blend==other.blend && layer==other.layer &&
            transform==other.transform && matrix==other.matrix && filterOnly==other.filterOnly && nearest==other.nearest &&
            (effects==other.effects || (effects && other.effects && *effects==*other.effects)) &&
            (parent==other.parent || (parent && other.parent && *parent==*other.parent));
    }
};
struct VisHit {
    struct Edge {
        uint64_t holder = 0, membership = 0;
        uint32_t index = 0, child = 0;
        uint64_t childIdentity = 0;
        bool operator==(const Edge&) const = default;
    };
    std::vector<Edge> path;
    uint32_t target = 0;
    uint64_t targetIdentity = 0;
    uint32_t hudRoot = 0;
    uint16_t hudKey = 0;
    uint64_t hudMembership = 0;
    // A copied turf (a turf vis member): the click names that turf (zero-based cell), like stock 516.1688
    // (tests/data/click-world turf-member); target is 0. Its contents and their members name themselves.
    bool turf = false;
    std::array<uint16_t,3> cell{};
    uint32_t turfId = 0; // that turf's id in the world, for naming it when it is on another z
    bool operator==(const VisHit&) const = default;
};
struct MapSprite {
    QImage image; // cached, straight-alpha RGBA icon/text raster; never modified after publication
    QPoint position; // top-left in the padded terrain's pixel coordinates
    QPoint iconBottom; // root appearance's bottom-left, for icon-x/y over its overlays
    QPoint cell; // destination DM cell (1-based)
    uint32_t atom = 0; // model registry key (high bit denotes a mob)
    uint16_t key = 0; // record identity, not its recyclable table slot
    bool haveKey = false;
    int plane = 0;
    float layer = 0;
    std::array<float, 4> tint{1, 1, 1, 1};
    // Blackness is a solid presentation command, never an atom or input target.
    enum Kind { Turf, Entity, Image, Vis, Screen, Master, Blackness } kind = Entity;
    int blend = 0;
    ColorMatrix color = identityColor;
    QString renderSource, renderTarget;
    // All parts of an extended plane-source appearance retain its typed binding.
    // A local/ambiguous/missing target must not expose a partially admitted tree.
    QString planeSourceDependency;
    QSize size; // axis-aligned transformed size; empty means the icon's size
    uint32_t screenOrder = 0; // membership order, independent of recyclable registry ref
    uint32_t attachmentOrder = 0; // ordered copies within a HUD/world holder
    // VIS_UNDERLAY at the holder's layer: 1 draws before an atom holder, 2 before a turf holder (vis-world sets b, e).
    uint8_t visUnderlay = 0;
    bool screenInteractive = false; // admitted HUD/image attachment hits, not effect anchors
    bool mouseTransparent = false; // root opacity-0 or a non-clickable text raster
    bool inkInteractive = true; // multiply master-owned leaves do not contribute mouse ink; menus stay rectangular
    bool maptext = false; // never hit by ink; opacity-2 can cover the published box
    int textMargin = 0; // maptext: pixels round its box in the image, which hold what an outline or shadow draws past it
    std::shared_ptr<const VisHit> visHit;
    uint32_t imageId = 0;
    uint64_t imageIdentity = 0, targetIdentity = 0, hudIdentity = 0; // immutable local lifetime tokens
    std::optional<StaticAffine> affine;
    // Ordinary root-transform trees sample each part, but report root icon coordinates.
    // Root geometry is relative to iconBottom - (0, rootSize.height()).
    std::optional<StaticAffine> rootAffine;
    QSize rootSize;
    bool smooth = false; // stock-verified half-size straight-RGBA sampling
    // Master or relay: PIXEL_SCALE, so its matrix samples the plane unsmoothed; false keeps the bilinear default. The
    // plane's turned surface carries it too, which the renderer samples by stock's rule for a texel boundary.
    bool nearest = false;
    int mouseOpacity = 1; // root/target mode; child modes never override ownership
    bool footprint = false; // iconless menu rectangle, never drawn or clicked
    QPoint sortCell; // root cell adjusted west/south by negative root pixel offsets
    // Master operation, or root relay operation retained on all dependent parts
    // for atomic admission; only renderSource samples receive the relay effect.
    std::shared_ptr<const PlaneEffects> planeEffects;
    uint64_t localGroup = 0; // root occurrence, not an atom registry alias
    QPoint captureOrigin; // local target's root anchor in scene pixels
    QPoint relayMouseBottom; // source screen origin excludes appearance pixel offsets
    bool passMouse = false;
    bool masterFallback = false; // worker-admitted iconless HUD opacity-2 viewport fallback
    uint32_t appearanceId = 0; // root appearance of this rendered occurrence; source-input provenance
    QSize turfDimensions;
    std::shared_ptr<const std::vector<uint32_t>> sourcePath; // ordinary local-source occurrence
    std::shared_ptr<const AppearanceGroup> appearanceGroup;
    uint64_t groupOccurrence = 0;
    QPoint groupOrigin;
    float groupLayer = 0;
    QRect rect() const { return affine ? affine->bounds.translated(position) : QRect(position, size.isEmpty() ? image.size() : size); }
    bool operator==(const MapSprite& b) const {
        return image.cacheKey() == b.image.cacheKey() && position == b.position && iconBottom == b.iconBottom &&
               cell == b.cell && atom == b.atom && key == b.key && haveKey == b.haveKey &&
               plane == b.plane && layer == b.layer && tint == b.tint && kind == b.kind &&
               localGroup == b.localGroup && captureOrigin == b.captureOrigin && relayMouseBottom == b.relayMouseBottom && passMouse == b.passMouse &&
               blend == b.blend && color == b.color && inkInteractive == b.inkInteractive && renderSource == b.renderSource &&
               renderTarget == b.renderTarget && planeSourceDependency == b.planeSourceDependency && size == b.size && affine == b.affine &&
               rootAffine == b.rootAffine && rootSize == b.rootSize && smooth == b.smooth && nearest == b.nearest && mouseOpacity == b.mouseOpacity &&
               (appearanceGroup == b.appearanceGroup || (appearanceGroup && b.appearanceGroup && *appearanceGroup == *b.appearanceGroup)) &&
               groupOccurrence == b.groupOccurrence &&
               groupOrigin == b.groupOrigin && groupLayer == b.groupLayer &&
               appearanceId == b.appearanceId && turfDimensions == b.turfDimensions && sourcePath == b.sourcePath && masterFallback == b.masterFallback && footprint == b.footprint && sortCell == b.sortCell && (planeEffects == b.planeEffects || (planeEffects && b.planeEffects && *planeEffects == *b.planeEffects)) && screenOrder == b.screenOrder && attachmentOrder == b.attachmentOrder && visUnderlay == b.visUnderlay &&
               screenInteractive == b.screenInteractive && mouseTransparent == b.mouseTransparent && maptext == b.maptext && textMargin == b.textMargin && imageId == b.imageId &&
               imageIdentity == b.imageIdentity && targetIdentity == b.targetIdentity && hudIdentity == b.hudIdentity && visHit == b.visHit;
    }
};
using MapSprites = QVector<MapSprite>;
}
