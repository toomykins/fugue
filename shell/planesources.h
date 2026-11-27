// Model-free plane-source bindings and original-ink coverage of a rendered frame.
#pragma once
#include "shell/mapsprite.h"
#include <map>
#include <algorithm>
#include <cmath>

namespace fugue::shell {
struct PlaneSources {
    struct Surface {
        qsizetype master = -1;
        std::vector<qsizetype> parts;
        bool input = true;
        // As input, but some parts may be relays of other planes, whose ink is asked of those planes (mapinput.cpp).
        bool relayInput = true;
    };
    static bool masterInput(const MapSprite& m, QRect viewport) {
        return material(m) && m.haveKey && (m.hudIdentity || m.targetIdentity) &&
            m.renderSource.isEmpty() && (!m.planeEffects || coverage(*m.planeEffects, viewport.size()));
    }
    std::vector<Surface> surfaces;
    std::map<QString,int> names;
    PlaneSources(const MapSprites& sprites, QRect viewport, const std::map<int,qsizetype>& masters) {
        if (sprites.size() > 65536 || masters.size() > 4096) return;
        std::map<int,int> planes;
        // Selected by CompositionPlan, not independently by the picker. Ambiguous
        // names do not authorize input; do not guess duplicate-target ownership.
        for (const auto& [plane,index] : masters) {
            const auto& m=sprites[index];
            if (m.renderTarget.isEmpty()) continue;
            const int next=int(surfaces.size());
            auto [it,unique]=names.emplace(m.renderTarget,next);
            if (!unique) it->second=-1;
            planes[plane]=next;
            Surface s; s.master=index;
            s.input = masterInput(m, viewport);
            surfaces.push_back(std::move(s));
        }
        for (qsizetype i=0;i<sprites.size();++i) {
            const auto& s=sprites[i];
            if (s.kind == MapSprite::Master || s.footprint) continue;
            const auto p=planes.find(s.plane);
            if (p==planes.end()) continue;
            auto& surface=surfaces[p->second];
            surface.parts.push_back(i);
            // Stock carries original contributor coverage across this capture,
            // NOT the materialized/composited alpha. Zero/partial scalar alpha,
            // finite matrices and admitted master filters neither erase original
            // ink nor add generated ink. Keep the whole-surface structural proof:
            // an unsupported contributor must never yield a partial guessed mask.
            const bool own = material(s) && !s.image.isNull() &&
                (s.size.isEmpty() || s.size==s.image.size()) &&
                (s.kind==MapSprite::Entity || s.kind==MapSprite::Screen || s.kind==MapSprite::Turf) &&
                !s.imageIdentity && !s.visHit && !s.localGroup && !s.planeEffects &&
                s.renderSource.isEmpty() && s.renderTarget.isEmpty() && s.planeSourceDependency.isEmpty();
            // A screen object relaying another plane's surface onto this one: its ink is that plane's.
            const bool relay = s.kind==MapSprite::Screen && !s.renderSource.isEmpty() && s.renderTarget.isEmpty() &&
                (s.planeSourceDependency.isEmpty() || s.planeSourceDependency==s.renderSource) && !s.localGroup;
            surface.input &= own;
            surface.relayInput &= own || relay;
        }
        for (auto& s : surfaces) s.relayInput &= s.master >= 0 && masterInput(sprites[s.master], viewport);
    }
    // A master's matrix moves its plane's original ink without adding or removing any: the picker takes the pointer
    // back through it before asking ink() (stock 516.1689, plane-relay-world mturn90, nopass-mturn90).
    static bool coverage(const PlaneEffects& effects, QSize viewport) {
        auto unturned = effects; unturned.matrix.reset();
        return unturned.sourceCoverage(viewport) && (!effects.matrix || StaticAffine::make(*effects.matrix, viewport));
    }
    static bool material(const MapSprite& s) {
        return !s.affine && !s.rootAffine && !s.maptext && s.blend>=0 && s.blend<=1 &&
            std::all_of(s.tint.begin(),s.tint.end(),[](float v) { return std::isfinite(v) && v>=0 && v<=1; }) &&
            std::all_of(s.color.begin(),s.color.end(),[](float v) { return std::isfinite(v); });
    }
    const Surface* find(const QString& name) const {
        const auto it=names.find(name);
        return it==names.end() || it->second<0 ? nullptr : &surfaces[size_t(it->second)];
    }
    static qsizetype ink(const Surface& surface, const MapSprites& sprites, QPoint pixel, uint64_t& work) {
        if (!surface.input) return -1;
        for (auto i=surface.parts.rbegin();i!=surface.parts.rend();++i) {
            if (!work) return -1;
            --work;
            const auto& s=sprites[*i];
            if (s.rect().contains(pixel) && s.image.pixelColor(pixel-s.position).alpha()) return *i;
        }
        return -1;
    }
};
}
