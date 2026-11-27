#include "shell/statthumbnails.h"
#include "shell/sceneappearance.h"
#include <QDataStream>
#include <QPainter>
#include <QIODevice>
#include <QBuffer>
#include <QImageReader>
#include <set>

namespace fugue::shell {
void StatThumbnails::begin(const model::World& world, const IconCache::Read& read, int64_t nowNs) {
    world_ = &world; nowNs_ = nowNs; ++epoch_; work_ = reads_ = 0; pixels_ = resourcePixels_ = 0;
    std::erase_if(entries_, [&](const auto& pair) { return pair.second.used + 1 < epoch_; });
    icons_.begin(world.resources, [this, read](uint32_t id, QByteArray& bytes) {
        if (reads_++ >= 32) { icons_.readLimited(); return false; }
        if (!read || !read(id, bytes)) return false;
        if (bytes.size() > 4 * 1024 * 1024) { icons_.readLimited(); return false; }
        if (!bytes.startsWith("DDMI")) {
            QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
            const QSize size = QImageReader(&buffer).size();
            const uint64_t pixels = uint64_t(std::max(0, size.width())) * std::max(0, size.height());
            // Bound peak decoding too, not just retention after a whole tree.
            if (pixels > 8 * 1024 * 1024 - resourcePixels_) { icons_.readLimited(); return false; }
            resourcePixels_ += pixels;
        }
        return true;
    }, 8 * 1024 * 1024); // include every GIF/legacy frame, not only QImageReader's header size
}
QImage StatThumbnails::resolve(uint32_t appid, int64_t startNs, AtomTrace::Info* diagnostic) {
    std::vector<uint32_t> chain;
    auto reject = [&](TraceReason reason, uint32_t id) {
        if (diagnostic && diagnostic->first.reason == TraceReason::Unobserved) {
            const auto parent = chain.empty() ? 0 : chain.back() != id ? chain.back() : chain.size() > 1 ? chain[chain.size()-2] : 0;
            diagnostic->first = {reason, id, parent}; diagnostic->outcome = reason;
            diagnostic->tree.firstPath = chain;
            if (chain.empty() || chain.back() != id) diagnostic->tree.firstPath.push_back(id);
            if (diagnostic->tree.firstPath.size() > 32) { diagnostic->tree.firstPath.resize(32); diagnostic->tree.truncated = true; }
        }
    };
    if (!world_) { reject(TraceReason::Unobserved, appid); return {}; }
    const auto& w = *world_;
    const int tile = model::iconSizeOf(w);
    if (tile <= 0 || tile > 128) { reject(TraceReason::InfoWork, appid); return {}; }
    struct Part {
        uint32_t resource; int index; QPoint at; int plane; float layer;
        std::array<float, 4> tint;
    };
    std::vector<Part> parts;
    QByteArray signature;
    QDataStream stamp(&signature, QIODevice::WriteOnly);
    stamp << tile;
    std::set<uint32_t> path;
    bool complete = true, transformed = false, simpleTransformTree = true;
    unsigned visits = 0;
    auto expand = [&](auto&& self, uint32_t id, uint32_t inheritedIcon, int dir, QPoint at,
                      int plane, float layer, std::array<float, 4> tint) -> void {
        if (!complete) return;
        if (++work_ > 65536 || ++visits > 512 || path.size() >= 32 || !path.insert(id).second) {
            reject(TraceReason::TraversalLimit, id); complete = false; return;
        }
        const size_t nodeIndex = diagnostic ? diagnostic->tree.nodes.size() : 16;
        if (diagnostic) {
            chain.push_back(id);
            if (nodeIndex < 16) { TraceNode n; n.path = chain; diagnostic->tree.nodes.push_back(std::move(n)); }
            else diagnostic->tree.truncated = true;
        }
        const bool root = path.size() == 1;
        auto found = w.appearances.find(id);
        if (found == w.appearances.end()) { reject(TraceReason::MissingAppearance, id); complete = false; return; }
        const proto::Appearance& f = found->second.fields;
        const proto::Appearance& ap = f;
        if (signature.size() + ap.iconState.size() > 65536) {
            reject(TraceReason::InfoWork, id); complete = false; return;
        }
        const SceneAppearance semantics(f);
        if (diagnostic && nodeIndex < 16) {
            auto& n = diagnostic->tree.nodes[nodeIndex]; n.invisibility = semantics.invisibility;
            n.transformed = f.hasTransform; n.transformIgnored = n.transformed; // independently verified INFO subset
        }
        // Fail the entire tree closed, not a misleading base-icon approximation.
        // These dependencies require MAP composition/context, or unverified INFO semantics.
        transformed |= f.hasTransform;
        // Stock evidence covers a root alone and a root with one leaf overlay,
        // independently transformed. Do not extend that observation to arbitrary trees.
        if (!f.underlays.empty() || f.overlays.size() > 1 ||
            path.size() > 2 || (!root && !f.overlays.empty())) simpleTransformTree = false;
        if (semantics.master ||
            semantics.keepTogether || semantics.keepApart || semantics.color != identityColor ||
            semantics.blend > 1 || semantics.invisibility || !f.filters.empty() ||
            !f.maptext.empty() || !f.renderSource.empty() || !f.renderTarget.empty()) {
            reject(TraceReason::InfoUnsupported, id); complete = false; return;
        }
        const uint32_t ordinal = ap.iconResID != UINT32_MAX && ap.iconResID != 0xffffu ? ap.iconResID :
                                 ap.iconState.empty() ? UINT32_MAX : inheritedIcon; // as on the map
        if (root || semantics.fixedDir) dir = int(f.dir);
        if (root && !dir) dir = 2;
        at += QPoint(f.pixelX + f.pixelW, -(f.pixelY + f.pixelZ));
        const float ownLayer = std::bit_cast<float>(ap.layerBits);
        if (!std::isfinite(ownLayer)) { reject(TraceReason::InfoUnsupported, id); complete = false; return; }
        if (root || ownLayer >= 0) layer = ownLayer;
        if (f.hasPlane) plane = resolvePlane(f.plane, plane);
        if (semantics.resetColor) tint[0] = tint[1] = tint[2] = 1;
        if (semantics.resetAlpha) tint[3] = 1;
        if (f.hasColor)
            for (int i = 0; i < 4; ++i) tint[i] *= float(f.color[i]) / 255;
        if (diagnostic && nodeIndex < 16) diagnostic->tree.nodes[nodeIndex].alpha = tint[3];
        for (uint32_t child : f.underlays) self(self, child, ordinal, dir, at, plane, layer, tint);
        if (complete && ordinal != UINT32_MAX && ordinal != 0xffffu) {
            const auto resource = icons_.lookup(ordinal);
            if (diagnostic && nodeIndex < 16) {
                auto& n = diagnostic->tree.nodes[nodeIndex]; n.ordinal = ordinal;
                n.direction = dir; n.assets = icons_.observations(ordinal, &resource);
            }
            if (!resource.ready) { reject(TraceReason::AssetUnavailable, id); complete = false; return; }
            const auto& icon = icons_.icon(resource.id);
            const auto* state = icon.state(QString::fromStdString(ap.iconState));
            if (icon.sheet.isNull() || !state) { reject(icon.sheet.isNull() ? TraceReason::EmptyAsset : TraceReason::MissingState, id); complete = false; return; }
            const int index = icon.index(*state, dir, state->frameAt(nowNs_, startNs, world_->iconClockNs));
            if (diagnostic && nodeIndex < 16) {
                auto& n = diagnostic->tree.nodes[nodeIndex]; n.requested = QString::fromStdString(ap.iconState).left(64);
                n.fallback = !icon.states.count(QString::fromStdString(ap.iconState));
                n.selected = n.fallback ? QString() : n.requested;
                n.cell = icon.index(*state, dir); n.directionSlot = n.cell - state->first;
                n.frame = state->frameAt(nowNs_, startNs, world_->iconClockNs);
            }
            const QPoint position = at + QPoint(0, tile - icon.cell.height());
            // Visual signature, not appearance IDs/raw transform bytes. Ignored
            // transforms must not regenerate an unchanged thumbnail.
            stamp << resource.id << quint64(resource.revision) << index << position << plane << layer;
            for (float value : tint) stamp << value;
            parts.push_back({resource.id, index, position, plane, layer, tint});
        }
        for (uint32_t child : f.overlays) self(self, child, ordinal, dir, at, plane, layer, tint);
        path.erase(id);
        if (diagnostic) chain.pop_back();
    };
    expand(expand, appid, UINT32_MAX, 2, {}, 0, 0, {1,1,1,1});
    if (transformed && !simpleTransformTree) { reject(TraceReason::InfoTransformTree, appid); complete = false; }
    QImage result;
    if (complete && !parts.empty()) {
        auto found = entries_.find(signature);
        if (found != entries_.end()) {
            found->second.used = epoch_;
            result = found->second.image;
        } else {
            std::stable_sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
                return a.plane != b.plane ? a.plane < b.plane : a.layer < b.layer;
            });
            result = QImage(tile, tile, QImage::Format_ARGB32_Premultiplied);
            result.fill(Qt::transparent);
            QPainter painter(&result);
            for (const auto& part : parts) {
                const auto& icon = icons_.icon(part.resource);
                pixels_ += uint64_t(icon.cell.width()) * icon.cell.height();
                if (pixels_ > 4 * 1024 * 1024) { reject(TraceReason::InfoWork, appid); complete = false; break; }
                QImage frame = icon.frame(part.index).convertToFormat(QImage::Format_ARGB32);
                for (int y = 0; y < frame.height(); ++y) {
                    auto* row = reinterpret_cast<QRgb*>(frame.scanLine(y));
                    for (int x = 0; x < frame.width(); ++x) {
                        const auto p = row[x];
                        row[x] = qRgba(qRound(qRed(p)*part.tint[0]), qRound(qGreen(p)*part.tint[1]),
                                       qRound(qBlue(p)*part.tint[2]), qRound(qAlpha(p)*part.tint[3]));
                    }
                }
                painter.drawImage(part.at, frame);
            }
            painter.end();
            if (!complete) result = {};
            else {
                if (entries_.size() >= 512) {
                    const auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
                        return a.second.used < b.second.used;
                    });
                    entries_.erase(oldest);
                }
                entries_[signature] = {result, epoch_};
            }
        }
    }
    // Never return a previously successful image for a missing/changed dependency.
    icons_.trim(128, 32 * 1024 * 1024);
    if (diagnostic) {
        if (!result.isNull()) diagnostic->outcome = TraceReason::Composed;
        else if (diagnostic->first.reason == TraceReason::Unobserved) reject(TraceReason::EmptyBounds, appid);
    }
    return result;
}
}
