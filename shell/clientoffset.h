// The client's camera offset (client.pixel_x/y/w/z) and colour at a given time, animate() included. Called on the
// worker. Stock truncates an animated camera offset toward zero, where an animated movable's pixel offset rounds.
#pragma once
#include "shell/animation.h"
#include "shell/sceneappearance.h"
#include <QPoint>
#include <map>
#include <optional>

namespace fugue::shell {
inline std::optional<QPoint> clientOffset(const model::World& w, int64_t nowNs) {
    QPoint result(int(w.viewPixelX) + w.viewPixelW, int(w.viewPixelY) + w.viewPixelZ);
    const auto& chain = w.clientChain;
    if (chain.rejected) return {};
    if (chain.steps.empty()) return result;
    uint64_t work = 2'097'152;
    if (!animation::admit(chain, work)) return {};
    using Pixels = std::array<int, 4>;
    std::map<uint32_t, Pixels> values;
    bool valid = true;
    auto get = [&](uint32_t id) {
        auto [it, fresh] = values.try_emplace(id);
        if (fresh) {
            const auto ap = w.appearances.find(id);
            if (ap == w.appearances.end()) valid = false;
            else {
                const proto::Appearance& fields = ap->second.fields;
                it->second = {fields.pixelX, fields.pixelY, fields.pixelW, fields.pixelZ};
            }
        }
        return it->second;
    };
    auto lerp = [](const Pixels& from, const Pixels& to, double, double f) {
        Pixels out{};
        for (int i = 0; i < 4; ++i) out[i] = from[i] + int(float(to[i] - from[i]) * float(f));
        return out;
    };
    auto add = [&](Pixels& out, const Pixels& base, const Pixels& from, const Pixels& to, double raw, double f) {
        const auto step = lerp(from, to, raw, f);
        for (int i = 0; i < 4; ++i) out[i] += step[i] - base[i];
    };
    Pixels out{};
    uint32_t endId = 0;
    if (animation::composeChain(chain, nowNs, get, lerp, add, out, endId)) {
        const auto end = get(endId);
        result += QPoint(out[0] - end[0] + out[2] - end[2], out[1] - end[1] + out[3] - end[3]);
    }
    if (!valid || std::abs(result.x()) > 1'000'000 || std::abs(result.y()) > 1'000'000) return {};
    return result;
}
// client.color, sampled like the camera offset: a plain colour is its diagonal matrix, alpha included.
// Identity when the client has none or its animation cannot be sampled.
inline ColorMatrix clientColor(const model::World& w, int64_t nowNs) {
    auto of = [&](uint32_t id, bool& valid) {
        const auto ap = w.appearances.find(id);
        if (ap == w.appearances.end()) { valid = false; return identityColor; }
        const proto::Appearance& fields = ap->second.fields;
        const SceneAppearance scene(fields);
        if (scene.hasColorMatrix) return scene.colorValid ? scene.color : identityColor;
        ColorMatrix out = identityColor;
        if (fields.hasColor)
            for (int i = 0; i < 4; ++i) out[5 * i] = float(fields.color[i]) / 255;
        return out;
    };
    bool valid = true;
    ColorMatrix result = w.haveClientAppid ? of(w.clientAppid, valid) : identityColor;
    const auto& chain = w.clientChain;
    uint64_t work = 2'097'152;
    if (chain.rejected || chain.steps.empty() || !animation::admit(chain, work)) return valid ? result : identityColor;
    auto get = [&](uint32_t id) { return of(id, valid); };
    auto lerp = [](const ColorMatrix& from, const ColorMatrix& to, double, double f) {
        ColorMatrix out;
        for (size_t i = 0; i < out.size(); ++i) out[i] = float(from[i] + (double(to[i]) - from[i]) * f);
        return out;
    };
    auto add = [&](ColorMatrix& out, const ColorMatrix& base, const ColorMatrix& from, const ColorMatrix& to, double raw, double f) {
        const auto step = lerp(from, to, raw, f);
        for (size_t i = 0; i < out.size(); ++i) out[i] += step[i] - base[i];
    };
    ColorMatrix out = identityColor;
    uint32_t endId = 0;
    if (animation::composeChain(chain, nowNs, get, lerp, add, out, endId)) {
        const auto end = get(endId);
        for (size_t i = 0; i < out.size(); ++i) result[i] += out[i] - end[i];
    }
    return valid && finiteColor(result) ? result : identityColor;
}
}
