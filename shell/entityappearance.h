// Deterministic worker appearance-field decoding; no model/cache/playback state.
#pragma once
#include "shell/mapsprite.h"
#include "proto/messages.h"
#include "shell/screenloc.h"

namespace fugue::shell {
struct EntityAppearanceFields {
    bool hasPlane = false;
    int dir = 0, plane = 0;
    uint16_t visFlags = 0;
    QPoint offset;
    std::array<float, 4> tint{1, 1, 1, 1};
    std::vector<uint32_t> underlays, overlays;
    ColorMatrix color = identityColor;
    bool colorValid = true;
    int blend = 0, invisibility = 0;
    bool master = false, resetColor = false, resetAlpha = false;
    bool keepTogether = false, keepApart = false, fixedDir = false;
    int mouseOpacity = 1;
    bool named = false, passMouse = false;
    bool imageUnsupported = false, hasTransform = false, imageOwnDir = false, imageOverride = false;
    bool noClientColor = false; // NO_CLIENT_COLOR
    bool imageTransformFlags = false, imageTargetTransformFlags = false, imageParentUnsupported = false, resetTransform = false;
    QString renderSource, renderTarget, screenLoc, maptext;
    QSize maptextSize;
    QPoint maptextOffset;
    bool maptextEffects = false, animationEffects = false, surfaceFilters = false, identityFilters = true;
    std::optional<ScreenLoc::Target> screenTarget;
    std::optional<ScreenLoc> screenPlacement;
    bool screenMalformed = false; // screen_loc is not coordinates at all
    float scaleX = 1, scaleY = 1;
    std::array<double, 6> matrix{1,0,0,0,1,0};
    bool affine = false, transformValid = true, transformEffects = false;
    // PIXEL_SCALE: stock samples transformed and scaled icons bilinearly unless it is set (static-transform-world).
    bool pixelScale = false;
    bool matrixValid = true; // geometry validity, independent of effect-combination admission
    std::shared_ptr<const PlaneEffects> planeEffects;
    std::shared_ptr<const PlaneEffects> atomFilters; // an ordinary atom's decoded filters (supported records only)
};
struct DecodedEntityAppearance {
    EntityAppearanceFields fields;
    bool valid = false;
};
// Previous fields are passed by ownership so the child lists are reused. No revisions, resources or cache mutation
// here.
DecodedEntityAppearance decodeEntityAppearance(const proto::Appearance& fields, int tile,
                                               EntityAppearanceFields previous = {});
}
