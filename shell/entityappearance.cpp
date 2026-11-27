#include "shell/entityappearance.h"
#include "net/textmacro.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace fugue::shell {
DecodedEntityAppearance decodeEntityAppearance(const proto::Appearance& fields, int tile, EntityAppearanceFields previous) {
    DecodedEntityAppearance result{std::move(previous)};
    auto& out = result.fields;
    out.underlays.clear(); out.overlays.clear(); out.tint = {1,1,1,1};
    try {
        {
            result.valid = true;
            const SceneAppearance semantics(fields);
            out.master = semantics.master; out.blend = semantics.blend;
            out.planeEffects = out.master ? std::make_shared<const PlaneEffects>(decodePlaneEffects(fields, true)) :
                !fields.renderSource.empty() ? std::make_shared<const PlaneEffects>(decodeSurfaceEffects(fields)) : nullptr;
            out.resetColor = semantics.resetColor; out.resetAlpha = semantics.resetAlpha;
            out.keepTogether = semantics.keepTogether; out.keepApart = semantics.keepApart;
            out.fixedDir = semantics.fixedDir;
            out.visFlags = semantics.visFlags;
            out.hasTransform = fields.hasTransform;
            // Ordinary-tree RESET_TRANSFORM probe, captured 516.1688 wire flag.
            out.resetTransform = (fields.appearanceFlags & 0x01000000u) != 0;
            out.noClientColor = (fields.appearanceFlags & 0x02000000u) != 0;
            // Image-root assignment probe: implicit SOUTH is 0x4 and follows
            // target EAST; assigned SOUTH is 0x204 and stays SOUTH. This is NOT
            // the ordinary-child fixed-direction flag.
            out.imageOwnDir = (fields.appearanceFlags & 0x200u) != 0;
            // Captured override wire bit, not a DM appearance_flags value.
            out.imageOverride = (fields.appearanceFlags & 0x40000u) != 0;
            // Atom filters are not rendered on ordinary atoms; images and their
            // targets degrade the same way (drawn unfiltered) instead of vanishing.
            out.imageParentUnsupported = semantics.master || semantics.keepTogether ||
                !fields.renderSource.empty() || !fields.renderTarget.empty();
            // An image's own matrix composes like an overlay's (stock 516.1688, image-tree-transform-world).
            out.imageUnsupported = semantics.master || semantics.keepTogether ||
                !fields.renderTarget.empty();
            // PASS_MOUSE is 0x80000000 in the 516.1688 relay wire fixture.
            out.passMouse = (fields.appearanceFlags & 0x80000000u) != 0;
            out.pixelScale = (fields.appearanceFlags & 0x40000000u) != 0;   // DM bit 9 at 0x200000 << 9
            // Nonidentity attachment inheritance admits direction/mouse-opacity
            // flags plus the non-geometric flags below; no group or transform
            // resets (RESET_TRANSFORM images take the untransformed path).
            // Packed wire flags are DM appearance_flags bit k at 0x200000 << k.
            // LONG_GLIDE, RESET_COLOR, RESET_ALPHA, NO_CLIENT_COLOR and TILE_BOUND
            // change motion, material or bounds, never the inherited geometry;
            // PIXEL_SCALE only picks the image's own sampling (static-transform-world).
            constexpr uint32_t nonGeometric = 0x00200000u | 0x00400000u | 0x00800000u | 0x02000000u | 0x20000000u | 0x40000000u;
            out.imageTransformFlags = (fields.appearanceFlags & ~(0x3204u | nonGeometric | 0x08000000u)) != 0;
            // Mob leaf probe captures 0x6, rather than the object leaf's 0x4.
            // Ordinary atoms also carry per-atom low bits and motion/bounds flags
            // (vg mobs: 0x20304a44 = TILE_BOUND|TILE_MOVER|LONG_GLIDE). Refuse only
            // override, KEEP_TOGETHER/KEEP_APART and PLANE_MASTER, which change how
            // attachments group. A target's PIXEL_SCALE samples only the target itself.
            out.imageTargetTransformFlags = (fields.appearanceFlags & 0x10040000u) != 0;
            // Captured independently with mouse_opacity=0/1/2 in the HUD fixture.
            out.named = !net::plainText(fields.name).empty();
            out.mouseOpacity = int(((fields.appearanceFlags >> 12) + 1) & 3);
            out.invisibility = semantics.invisibility; out.color = semantics.color; out.colorValid = semantics.colorValid;
            out.renderSource = QString::fromStdString(fields.renderSource);
            out.renderTarget = QString::fromStdString(fields.renderTarget);
            out.screenLoc = QString::fromStdString(fields.screenLoc);
            out.screenTarget = ScreenLoc::target(out.screenLoc);
            out.screenPlacement = out.screenTarget ? ScreenLoc::parse(out.screenTarget->location, &out.screenMalformed) : std::nullopt;
            out.maptext = QString::fromStdString(net::plainText(fields.maptext));
            out.maptextSize = QSize(fields.maptextW, fields.maptextH);
            out.maptextOffset = QPoint(fields.maptextX, -fields.maptextY);
            out.identityFilters = identitySurfaceFilters(fields);
            out.surfaceFilters = out.master ? !out.planeEffects->filters.empty() : !fields.filters.empty();
            out.atomFilters = nullptr;
            if (out.surfaceFilters && !out.master && fields.renderSource.empty()) {
                auto filters = decodeSurfaceEffects(fields);
                if (!filters.filters.empty()) out.atomFilters = std::make_shared<const PlaneEffects>(std::move(filters));
            }
            // Animation-chain admission remains conservative. Static capture and
            // ordinary occurrence admission must classify vis membership separately.
            out.animationEffects = out.surfaceFilters || semantics.visFlags != 0;
            // Transforms are not a text effect: maptext takes the composed matrix.
            out.maptextEffects = semantics.master || semantics.keepTogether || semantics.keepApart ||
                !fields.filters.empty() || !out.renderSource.isEmpty() || !out.renderTarget.isEmpty();
            out.scaleX = out.scaleY = 1;
            out.matrix = {1,0,0,0,1,0};
            out.affine = false; out.transformValid = true; out.transformEffects = false; out.matrixValid = true;
            if (out.hasTransform) {
                for (int i = 0; i < 6; ++i) out.matrix[i] = std::bit_cast<float>(fields.transformBits[i]);
                out.transformValid = StaticAffine::make(out.matrix, {tile,tile}).has_value();
                out.matrixValid = out.transformValid;
                const auto& m = out.matrix;
                out.affine = !out.transformValid || m[1] != 0 || m[2] != 0 || m[3] != 0 || m[5] != 0 || m[0] <= 0 || m[4] <= 0;
                if (!out.affine) { out.scaleX = float(m[0]); out.scaleY = float(m[4]); }
                // Filters run on the atom's untransformed surface and KEEP_TOGETHER groups take the matrix on their
                // surface; both are drawn by the group path (stock 516.1688, tests/data/transform-group-world). The
                // flat path draws a filtered matrix unfiltered rather than not at all, and KEEP_APART outside a group
                // changes nothing there.
                if (out.affine && (semantics.master || semantics.keepTogether ||
                    !fields.renderSource.empty() || !fields.renderTarget.empty())) {
                    out.transformValid = false; out.transformEffects = true;
                }
            }
            out.dir = int(fields.dir);
            out.hasPlane = fields.hasPlane;
            out.plane = int(fields.plane);
            out.offset = QPoint(fields.pixelX + fields.pixelW, -(fields.pixelY + fields.pixelZ));
            if (fields.hasColor) {
                for (int i = 0; i < 4; ++i) out.tint[i] = float(fields.color[i]) / 255;
                // A colour matrix set over a plain colour or alpha leaves the old RGBA on the wire; stock draws the matrix
                // alone, and alpha set later is in the matrix. Stock 516.1688, tests/data/color-tween-world
                // static-plain-then-m1; 516.1689, tests/data/stock-parity-world color-matrix-alpha.
                if (semantics.hasColorMatrix && !out.master) out.tint = {1, 1, 1, 1};
            }
            // A master's matrix replaces its scalar color/alpha sentinel;
            // ordinary sprite tint/matrix semantics are unchanged.
            if (out.master) for (const auto& var : fields.vars)
                if (var.bit == 0x400) out.tint = {1,1,1,1};
            if (!out.master && !out.renderSource.isEmpty()) {
                auto effects = std::make_shared<PlaneEffects>(*out.planeEffects);
                if (out.hasTransform) {
                    effects->matrix = out.matrix;
                    // StaticAffine validates finite/invertible geometry; the
                    // relay envelope applies its separate placement contract.
                    effects->supported &= StaticAffine::make(out.matrix, {tile,tile}).has_value();
                }
                for (const auto& var : fields.vars) if (var.bit == 0x400) {
                    const auto& bytes = var.data;
                    if (bytes.size() != 80) { effects->supported = false; continue; }
                    for (size_t i = 0; i < 20; ++i) {
                        uint32_t raw = 0;
                        for (size_t b = 0; b < 4; ++b) raw |= uint32_t(bytes[4*i+b]) << (8*b);
                        effects->supported &= std::isfinite(std::bit_cast<float>(raw));
                    }
                }
                out.planeEffects = std::move(effects);
            }
            out.underlays.assign(fields.underlays.begin(), fields.underlays.end());
            out.overlays.assign(fields.overlays.begin(), fields.overlays.end());
        }
    } catch (const std::exception&) { /* preserve partial decoding of object fields */ }
    return result;
}
}
