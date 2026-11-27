#include "shell/planeeffects.h"
#include "proto/messages.h"
#include "shell/sceneappearance.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>

namespace fugue::shell {
namespace {
bool boundedOutline(const PlaneFilter& f) {
    return f.kind == PlaneFilter::Outline && std::isfinite(f.size) &&
        // Keep the generic parameter safety bound; surface budgets decide whether
        // the full stock ladder can execute. This is not a BYOND size clamp.
        f.size >= 0 && f.size <= 8192 && !(f.flags & ~3);
}
}
bool PlaneEffects::boundedTreeFilters() const {
    if (!supported || matrix || filters.empty() || !valid({32,32})) return false;
    for (const auto& f : filters)
        if (f.kind != PlaneFilter::Blur && f.kind != PlaneFilter::Shadow && !boundedOutline(f)) return false;
    return true;
}
void PlaneEffects::ignoreFilters() {
    ignoredFilters += filters.size();
    filters.clear();
}
bool PlaneEffects::requiresSimpleSources() const {
    auto surface = *this; surface.matrix.reset();
    return surface.boundedTreeFilters() && std::adjacent_find(filters.begin(),filters.end(),[](const auto& a,const auto& b) {
        return a.kind==PlaneFilter::Blur && b.kind==PlaneFilter::Outline;
    }) != filters.end();
}
bool PlaneEffects::valid(QSize viewport) const {
    if (!supported || filters.size() > 8 || (matrix && !StaticAffine::make(*matrix, viewport))) return false;
    for (const auto& f : filters) {
        if (f.kind == PlaneFilter::AlphaMask || f.kind == PlaneFilter::Color) {
            if (f.size || f.grow) return false; // (a matrix is fine: master-transform-world fx-*-swap, fx-*-mask)
            if (f.kind == PlaneFilter::AlphaMask) {
                if ((f.source.isEmpty() && f.icon == UINT32_MAX) || (f.flags & ~3) || !std::isfinite(f.x) || !std::isfinite(f.y) ||
                    std::abs(f.x) > 8192 || std::abs(f.y) > 8192) return false;
            } else if (f.x || f.y || f.flags || !finiteColor(f.material)) return false;
            continue;
        }
        if (f.kind == PlaneFilter::Wave || f.kind == PlaneFilter::Ripple || f.kind == PlaneFilter::Bloom) {
            if (f.flags & ~3) return false;
            for (double v : {f.size, f.x, f.y, f.offset, f.radius, f.repeat, f.falloff})
                if (!std::isfinite(v) || std::abs(v) > 8192) return false;
            // A negative bloom offset shrinks stock's spread surface; not modelled.
            if (f.kind == PlaneFilter::Bloom && (f.offset < 0 || f.size < 0 || f.flags)) return false;
            continue;
        }
        const bool wideOutline = boundedOutline(f);
        const bool standaloneBlur = filters.size() == 1 && f.kind == PlaneFilter::Blur;
        if (f.kind == PlaneFilter::Shadow) {
            if (!std::isfinite(f.size) || f.size < 0 || !std::isfinite(f.grow) ||
                std::abs(f.grow) > 8192 || !std::isfinite(f.x) || !std::isfinite(f.y) ||
                std::abs(f.x) > 8192 || std::abs(f.y) > 8192 || f.flags) return false;
            for (float c : f.color) if (!std::isfinite(c) || c < 0 || c > 1) return false;
            continue;
        }
        if (!wideOutline && f.kind != PlaneFilter::Blur) return false;
        if (!std::isfinite(f.size) ||
            !std::isfinite(f.grow) || f.size < 0 || (!standaloneBlur && f.size > (wideOutline ? 8192 : 4)) || f.grow < 0 || f.grow > 4 ||
            !std::isfinite(f.x) || !std::isfinite(f.y) || f.x != std::floor(f.x) || f.y != std::floor(f.y) ||
            std::abs(f.x) > 8192 || std::abs(f.y) > 8192 || (f.flags & ~3)) return false;
        for (float c : f.color) if (!std::isfinite(c) || c < 0 || c > 1) return false;
    }
    return true;
}
bool identitySurfaceFilters(const proto::Appearance& fields) {
    if (fields.filters.empty()) return true;
    if (fields.filters.size() > 8) return false;
    // Complete surface decoding, never unsupported-record fallback.
    const auto effects = decodeSurfaceEffects(fields);
    return effects.supported && !effects.ignoredFilters && !effects.filters.empty() &&
        std::all_of(effects.filters.begin(), effects.filters.end(), [](const PlaneFilter& f) {
            return f.kind == PlaneFilter::Blur && f.size == 0;
        });
}
bool PlaneEffects::relayFilters() const {
    auto surface = *this;
    surface.matrix.reset();
    if (!supported || ignoredFilters || !surface.valid({32,32})) return false;
    // A colour matrix runs on the relayed picture as on a master's (stock 516.1689, plane-relay-world fcolor-loc23).
    for (const auto& f : filters)
        if (f.kind != PlaneFilter::Blur && f.kind != PlaneFilter::Shadow && f.kind != PlaneFilter::Color &&
            !boundedOutline(f)) return false;
    return !matrix || StaticAffine::make(*matrix,{32,32}).has_value();
}
bool PlaneEffects::sourceCoverage(QSize viewport) const {
    if (matrix || ignoredFilters || !valid(viewport)) return false;
    if (filters.empty()) return true;
    // This is capture coverage, not direct master-tree gesture admission.
    // Worker reverse-chain provenance failures increment ignoredFilters, even
    // for childless masters.
    return boundedTreeFilters() ||
        std::all_of(filters.begin(), filters.end(), [](const PlaneFilter& f) {
            return f.kind == PlaneFilter::Blur && f.size == 0;
        });
}
std::optional<StaticAffine> PlaneEffects::relayAffine(QSize viewport, bool displacedCapture) const {
    if (!matrix || !relayFilters()) return {};
    auto affine = StaticAffine::make(*matrix,viewport);
    if (!affine || !displacedCapture) return affine;
    // Stock 516.1689 moves a neutral plane's picture, before the matrix (about the view's centre) applies, by u per
    // axis (Y down): half of what the view taken back through the matrix's linear part outgrows the view by, plus the
    // matrix's own move taken back the same way, rounded up to a whole pixel, and nothing where that is negative. So a
    // half scale lands in the view's bottom right quarter, a turn of 45 degrees lies 66 px lower than one about the
    // centre, and a positive move counts twice where a negative one counts once (plane-relay-world rhalf, rt_*, rm_*:
    // every grab within a pixel). A processed plane's picture is not moved (malpha-*).
    const auto& m = *matrix;
    const QTransform linear(m[0], -m[3], -m[1], m[4], 0, 0);
    const QTransform back = linear.inverted();
    const QRectF reach = back.mapRect(QRectF(QPointF(), viewport));
    const QPointF centre(viewport.width() / 2.0, viewport.height() / 2.0), move(m[2], -m[5]);
    const QPointF moved = back.map(move);
    auto shift = [](double outgrown, double back) { return std::max(0.0, std::ceil(outgrown / 2 + back - 1e-6)); };
    const QPointF u(shift(reach.width() - viewport.width(), moved.x()), shift(reach.height() - viewport.height(), moved.y()));
    affine->forward = QTransform::fromTranslate(u.x() - centre.x(), u.y() - centre.y()) * linear *
        QTransform::fromTranslate(centre.x() + move.x(), centre.y() + move.y());
    affine->inverse = affine->forward.inverted();
    affine->bounds = affine->forward.mapRect(QRectF(QPointF(),viewport)).toAlignedRect();
    if (std::max({std::abs(affine->bounds.left()),std::abs(affine->bounds.right()),
                  std::abs(affine->bounds.top()),std::abs(affine->bounds.bottom())}) > 65536) return {};
    return affine;
}
PlaneEffects decodePlaneEffects(const proto::Appearance& fields, bool masterTreeGrouping) try {
    auto out = decodeSurfaceEffects(fields);
    // Tree selection/admission belongs to EntityMap; this decodes only the
    // selected master's surface operation, never inherited child operations.
    const SceneAppearance appearance(fields);
    out.supported &= fields.renderSource.empty() && fields.maptext.empty() &&
        (masterTreeGrouping || (!appearance.keepTogether && !appearance.keepApart)) &&
        !appearance.visFlags && appearance.blend <= 4;
    for (const auto& var : fields.vars) if (var.bit == 0x400) {
        const auto& bytes = var.data;
        if (bytes.size() != 80) { out.supported = false; continue; }
        for (size_t i = 0; i < 20; ++i) {
            uint32_t raw = 0;
            for (size_t b = 0; b < 4; ++b) raw |= uint32_t(bytes[i*4+b]) << (8*b);
            if (!std::isfinite(std::bit_cast<float>(raw))) out.supported = false;
        }
    }
    if (fields.hasTransform) {
        std::array<double,6> m{};
        for (size_t i = 0; i < 6; ++i) m[i] = std::bit_cast<float>(fields.transformBits[i]);
        if (m != std::array<double,6>{1,0,0,0,1,0}) out.matrix = m;
    }
    // Master-specific degradation is deliberately not the relay policy.
    if (!out.valid({32,32}) || appearance.color[3] != 0 || appearance.color[7] != 0 ||
        appearance.color[11] != 0 || appearance.color[19] != 0) out.ignoreFilters();
    out.supported = out.valid({32,32});
    return out;
} catch (const std::exception&) {
    PlaneEffects refused; refused.supported = false; return refused;
}
PlaneEffects decodeSurfaceEffects(const proto::Appearance& fields) try {
    PlaneEffects out;
    const auto& chain = fields.filters;
    if (chain.size() > 8) {
        out.ignoredFilters = chain.size();out.supported = out.valid({32,32});return out;
    }
    auto decode = [](const proto::Appearance::Filter& record) -> std::optional<PlaneFilter> { try {
        const uint32_t type = record.type, mask = record.paramMask;
        const std::string bytes(record.params.begin(), record.params.end());
        // The record byte numbers the filter among those of its type, from 1 (a second blur is 2; a bar sign's
        // seven waves arrive as 1..7: stock 516.1689, stock-parity-world repeated-filters). A server never sends
        // 0, so such a record is malformed.
        if (!record.flags) return std::nullopt;
        // Stock 516.1688/516.1689 records: an alpha mask is x, y (f32), the two-byte flags, an icon resource (0xffff:
        // none) and a NUL-terminated source name; colour is a two-byte RGB-space selector followed by 20 float32s.
        // No colour spaces.
        if (type == 7 || type == 9) {
            PlaneFilter f; f.kind = PlaneFilter::Kind(type);
            if (type == 7) {
                const auto name = record.str;
                if (mask != 0x32c || bytes.size() != 15 + name.size() || uint8_t(bytes[9]) ||
                    bytes.substr(14,name.size()) != name || bytes.back()) return std::nullopt;
                uint32_t icon = 0;
                for (size_t i = 0; i < 4; ++i) icon |= uint32_t(uint8_t(bytes[10+i])) << (8*i);
                if (icon != 0xffff) f.icon = icon;
                auto number = [&](size_t at) {
                    uint32_t raw = 0;
                    for (size_t i = 0; i < 4; ++i) raw |= uint32_t(uint8_t(bytes[at+i])) << (8*i);
                    return double(std::bit_cast<float>(raw));
                };
                f.x = number(0); f.y = -number(4);
                f.flags = uint8_t(bytes[8]); f.source = QString::fromStdString(name);
            } else {
                if (mask != 0x420 || bytes.size() != 82 || bytes[0] || bytes[1]) return std::nullopt;
                for (int i = 0; i < 20; ++i) {
                    uint32_t raw = 0;
                    for (int b = 0; b < 4; ++b) raw |= uint32_t(uint8_t(bytes[2+4*i+b])) << (8*b);
                    f.material[i] = std::bit_cast<float>(raw);
                }
            }
            PlaneEffects single; single.filters = {f};
            return single.valid({32,32}) ? std::optional<PlaneFilter>(f) : std::nullopt;
        }
        // Stock 516.1688 records (filter-types-world): every parameter is sent, in mask bit order; floats are f32,
        // bit 0x02 is a colour (RGBA bytes), bit 0x20 the two-byte flags. wave: size, x, y, offset, flags.
        // ripple: size, x, y, radius, flags, repeat, falloff. bloom: size, threshold (its alpha is the bloom
        // alpha), offset.
        if (type == 5 || type == 6 || type == 14) {
            const uint32_t expected = type == 5 ? 0x3du : type == 6 ? 0x87du : 0x13u;
            if (mask != expected) return std::nullopt;
            PlaneFilter f; f.kind = PlaneFilter::Kind(type);
            size_t at = 0;
            for (uint32_t bit = 1; bit <= 0x800; bit <<= 1) {
                if (!(mask & bit)) continue;
                const size_t width = bit == 0x20 ? 2 : 4;
                if (bytes.size() - std::min(at, bytes.size()) < width) return std::nullopt;
                uint32_t raw = 0;
                for (size_t i = 0; i < width; ++i) raw |= uint32_t(uint8_t(bytes[at+i])) << (8*i);
                at += width;
                const double value = std::bit_cast<float>(raw);
                if (bit == 2) { for (int i = 0; i < 4; ++i) f.color[i] = float((raw >> (8*i)) & 255) / 255; continue; }
                if (bit == 0x20) { f.flags = int(raw); continue; }
                if (!std::isfinite(value) || std::abs(value) > 8192) return std::nullopt;
                if (bit == 1) f.size = value;
                else if (bit == 4) f.x = value;
                else if (bit == 8) f.y = -value;
                else if (bit == 0x10) (type == 6 ? f.radius : f.offset) = value;
                else if (bit == 0x40) f.repeat = value;
                else if (bit == 0x800) f.falloff = value;
            }
            if (at != bytes.size()) return std::nullopt;
            PlaneEffects single; single.filters = {f};
            return single.valid({32,32}) ? std::optional<PlaneFilter>(f) : std::nullopt;
        }
        // Blur (size), outline (size, colour, flags: all sent) and drop shadow (size, colour, x, y, offset: any
        // subset, the rest zero).
        if (type == 1 ? mask != 1 : type == 2 ? mask != 0x23 : type != 3 || (mask & ~0x1fu)) return std::nullopt;
        PlaneFilter f; f.kind = PlaneFilter::Kind(type);
        if (type == 3) f.color = {};
        size_t at = 0;
        for (uint32_t bit = 1; bit <= 0x20; bit <<= 1) {
            if (!(mask & bit)) continue;
            const size_t width = bit == 0x20 ? 2 : 4;
            if (bytes.size() - std::min(at, bytes.size()) < width) return std::nullopt;
            uint32_t raw = 0;
            for (size_t i = 0; i < width; ++i) raw |= uint32_t(uint8_t(bytes[at+i])) << (8*i);
            at += width;
            if (bit == 2) {
                for (int i = 0; i < 4; ++i) f.color[i] = float((raw >> (8*i)) & 255) / 255;
            } else if (bit == 0x20) f.flags = int(raw);
            else {
                const double value = std::bit_cast<float>(raw);
                const bool blurSize = type != 2 && bit == 1;
                if (!std::isfinite(value) || (!blurSize && std::abs(value) > 8192)) return std::nullopt;
                // Render-only saturation. Remaining chain admission below still
                // rejects wider combinations; omitted neighbors cannot mutate DM.
                if (bit == 1) f.size = blurSize ? std::min(value, 100.0) : value;
                if (bit == 0x10) f.grow = value;
                if (bit == 4) f.x = value;
                if (bit == 8) f.y = -value;
            }
        }
        if (at != bytes.size()) return std::nullopt;
        PlaneEffects single;single.filters={f};
        if (!single.valid({32,32})) return std::nullopt;
        return f;
    } catch (const std::exception&) { return std::nullopt; } };
    for (const auto& record : chain) {
        if (const auto filter = decode(record)) out.filters.push_back(*filter);
        else ++out.ignoredFilters;
    }
    // Keep supported records in order, but do not invent semantics for an
    // unsupported combination. Retain omission counts so each caller can choose
    // degradation (masters) or complete-decoding refusal (relay consumers).
    if (!out.valid({32,32})) out.ignoreFilters();
    out.supported = out.valid({32,32});
    return out;
} catch (const std::exception&) {
    PlaneEffects refused; refused.supported = false; return refused;
}
}
