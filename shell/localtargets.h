// Bounded local capture geometry, resolved once in the immutable CompositionPlan.
#pragma once
#include "shell/mapsprite.h"
#include <map>
#include <set>
#include <tuple>
namespace fugue::shell {
struct LocalTargets {
    struct Group {
        QString name;
        std::vector<qsizetype> parts;
        QRect bounds;
        QRect icon; // the producer's own icon, which a turned relay pivots about
        QPoint origin;
        int state = 0;
        bool valid = true;
        bool cyclic = false, depthRefused = false;
    };
    std::vector<Group> groups;
    std::map<QString, int> names;
    std::vector<int> owner; // per sprite: the group it is a part of, or -1
    // A producer's own colour and alpha are part of what is captured (stock 516.1689, stock-parity-world overlay-relays).
    // A hidden target's turned icon and its maptext are captured too (stock 516.1689).
    static bool materialSupported(const MapSprite& s) {
        return !s.rootAffine && s.blend <= 1 && ((!s.affine && !s.maptext) || s.renderTarget.startsWith('*'));
    }
    // Whether a part is in a surface of its own target's (its filters, matrix or KEEP_TOGETHER material), not in the
    // surface of an atom it is drawn in (a vis_contents member in its holder's group, an overlay target in its atom's).
    static bool ownSurface(const MapSprite& s) {
        return s.appearanceGroup && s.localGroup && s.groupOccurrence == s.localGroup;
    }
    // Where a part lands in what is captured: its groups' filters reach past it and their matrices move it.
    static QRect extent(const MapSprite& s) {
        QRectF rect = s.rect();
        if (!ownSurface(s)) return rect.toAlignedRect();
        for (auto g = s.appearanceGroup; g; g = g->parent) {
            if (g->effects && !g->effects->filters.empty()) {
                const int reach = filterReach(*g->effects);
                rect.adjust(-reach,-reach,reach,reach);
            }
            if (!g->transform.isIdentity())
                rect = g->transform.mapRect(rect.translated(-QPointF(s.groupOrigin))).translated(QPointF(s.groupOrigin));
        }
        return rect.toAlignedRect();
    }
    explicit LocalTargets(const MapSprites& sprites) {
        std::map<std::pair<QString,uint64_t>,int> occurrences;
        std::set<QString> planeNames;
        owner.assign(size_t(sprites.size()), -1);
        for (const auto& s : sprites) if (s.kind == MapSprite::Master) {
            planeNames.insert(s.renderTarget);
        }
        for (qsizetype i = 0; i < sprites.size(); ++i) {
            const auto& s = sprites[i];
            if (s.kind == MapSprite::Master || s.renderTarget.isEmpty()) continue;
            if (groups.size() >= 4096) { names[s.renderTarget] = -1; continue; }
            const auto key = std::make_pair(s.renderTarget, s.localGroup ? s.localGroup : uint64_t(i)+1);
            auto [it, fresh] = occurrences.try_emplace(key, int(groups.size()));
            if (fresh) {
                Group g; g.name = s.renderTarget; g.origin = s.captureOrigin;
                // Of several atoms with one name a relay shows the southmost, then the westmost, then the one made
                // first, whatever their layers; each still draws itself (stock 516.1689, stock-parity-world shared-render-target).
                const auto [slot, first] = names.emplace(g.name, int(groups.size()));
                if (!first && slot->second >= 0) {
                    const auto& other = sprites[groups[slot->second].parts.front()];
                    if (std::tuple(s.cell.y(), s.cell.x(), s.atom) < std::tuple(other.cell.y(), other.cell.x(), other.atom))
                        slot->second = int(groups.size());
                }
                groups.push_back(std::move(g));
            }
            auto& g = groups[it->second]; g.parts.push_back(i);
            owner[size_t(i)] = it->second;
            if (g.icon.isNull() && !s.maptext && !s.image.isNull() && s.rect().bottomLeft() + QPoint(0,1) == s.iconBottom)
                g.icon = s.rect();
            // A vis_contents member is a producer like any atom (tgstation's eyelids).
            if (!s.localGroup || (s.kind != MapSprite::Entity && s.kind != MapSprite::Screen && s.kind != MapSprite::Turf &&
                                  s.kind != MapSprite::Vis) || s.footprint ||
                !materialSupported(s) || s.imageIdentity ||
                planeNames.count(s.renderTarget) ||
                s.plane != sprites[g.parts.front()].plane) g.valid = false;
        }
        auto visit = [&](auto&& self, int index, int depth) -> bool {
            auto& g = groups[index];
            if (g.state == 2) return g.valid;
            if (g.state == 1) { g.cyclic = true; return false; }
            if (depth >= 16) { g.depthRefused = true; return false; }
            g.state = 1;
            for (auto i : g.parts) {
                const auto& s = sprites[i]; QRect rect = extent(s);
                if (!s.renderSource.isEmpty()) {
                    const int dependency = find(s.renderSource);
                    if (dependency < 0 || !self(self,dependency,depth+1)) { g.valid = false; continue; }
                    rect = placed(s,groups[dependency]);
                } else if (s.image.isNull()) { g.valid = false; continue; }
                g.bounds = g.bounds.united(rect);
            }
            const auto b = g.bounds.translated(-g.origin);
            if (b.isEmpty() || b.width() > 8192 || b.height() > 8192 ||
                uint64_t(b.width())*b.height() > 4ull*1024*1024 ||
                std::max({std::abs(b.left()),std::abs(b.right()),std::abs(b.top()),std::abs(b.bottom())}) > 8192) g.valid = false;
            g.state = 2; return g.valid;
        };
        for (int i = 0; i < int(groups.size()); ++i) visit(visit,i,0);
    }
    int find(const QString& name) const {
        const auto it = names.find(name); return it == names.end() ? -1 : it->second;
    }
    static QRect placed(const MapSprite& consumer, const Group& producer) {
        return producer.bounds.translated(consumer.position-producer.origin);
    }
};
}
