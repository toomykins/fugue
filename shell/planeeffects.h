// Immutable plane-surface operations. No model or GPU resources are retained.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include <optional>
#include "shell/staticaffine.h"
#include "shell/sceneappearance.h"

namespace fugue::proto { struct Appearance; }
namespace fugue::shell {
struct PlaneFilter {
    // Kinds are the 516.1688 wire record types. y is screen-down (the negated DM y).
    enum Kind { Blur = 1, Outline = 2, Shadow = 3, Wave = 5, Ripple = 6, AlphaMask = 7, Color = 9, Bloom = 14 } kind = Blur;
    double size = 0, grow = 0, x = 0, y = 0;
    // Wave/bloom offset, ripple radius/repeat/falloff (DM parameter names).
    double offset = 0, radius = 0, repeat = 0, falloff = 0;
    int flags = 0;
    std::array<float,4> color{0,0,0,1};
    QString source; // alpha mask: a render target, or
    uint32_t icon = UINT32_MAX; // an icon resource ordinal, whose first frame the template builder puts in `image`
    std::shared_ptr<const QImage> image;
    ColorMatrix material = identityColor;
    bool operator==(const PlaneFilter&) const = default;
    auto operator<=>(const PlaneFilter&) const = default;
};
struct PlaneEffects {
    // Structural/transform admission. Unsupported filters are omitted instead
    // of turning this into a whole-plane refusal.
    bool supported = true;
    size_t ignoredFilters = 0;
    std::vector<PlaneFilter> filters;
    std::optional<std::array<double,6>> matrix;
    bool operator==(const PlaneEffects&) const = default;
    bool valid(QSize viewport) const;
    void ignoreFilters();
    bool boundedTreeFilters() const;
    // Reverse transitions require ordinary 32px normal-blend sources, not
    // binary alpha. Generated filter ink never becomes input provenance.
    bool requiresSimpleSources() const;
    // Strict consumer envelope: no ignored records, using the shared bounded
    // stock filter contract, not master degradation.
    bool relayFilters() const;
    // Original contributor ink survives a direct plane capture's admitted stock
    // filters. Ignored records and source geometry are not coverage proofs.
    bool sourceCoverage(QSize viewport) const;
    // A relay's matrix over a plane's picture, in view pixels. Only neutral-source visuals add stock's displacement;
    // processed-source visuals and all input use the matrix about the view's centre (stock 516.1689,
    // plane-relay-world: rhalf and rt_45 are clicked where an unmoved picture would be).
    std::optional<StaticAffine> relayAffine(QSize viewport, bool displacedCapture) const;
};
// Worker-only decoding. Unknown/malformed filter records are skipped; an
// unsupported remaining combination or over-budget filter list is omitted as a
// whole. Other appearance/transform/structural refusals remain fail-closed.
// Group flags are structural, not surface effects. Only EntityMap's bounded
// master-tree admission may opt in; other callers retain fail-closed decoding.
PlaneEffects decodePlaneEffects(const proto::Appearance& fields, bool masterTreeGrouping = false);
// Reusable filter decoding, independent of master structure/material/affine.
// Omission counts are retained; consumers MUST select their own admission policy.
PlaneEffects decodeSurfaceEffects(const proto::Appearance& fields);
// Complete decoding, not fallback omission: only a supported zero blur is
// currently established as an identity surface operation.
bool identitySurfaceFilters(const proto::Appearance& fields);
// How far an atom's filter chain can reach past its parts, for the capture it runs on.
inline int filterReach(const PlaneEffects& effects) {
    double reach = 0;
    for (const auto& f : effects.filters) {
        if (f.kind == PlaneFilter::Outline) reach += std::ceil(f.size) + 1;
        else if (f.kind == PlaneFilter::Blur) reach += std::ceil(3 * f.size) + 1;
        else if (f.kind == PlaneFilter::Shadow)
            reach += std::ceil(std::abs(f.x)) + std::ceil(std::abs(f.y)) + std::ceil(3 * f.size) + std::ceil(std::max(0.0, f.grow)) + 1;
        // A wave or ripple pass grows by its summed amplitude, at most 32 px; bloom stays inside its surface.
        else if (f.kind == PlaneFilter::Wave || f.kind == PlaneFilter::Ripple)
            reach += std::min(32.0, std::ceil(std::abs(f.size))) + 1;
    }
    return int(std::min(reach, 512.0));
}
}
