#include "shell/atomdiagnostic.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <charconv>

namespace fugue::shell {
const char* traceReasonName(TraceReason reason) {
    switch (reason) {
#define R(x) case TraceReason::x: return #x
    R(Unobserved); R(AbsentModel); R(Freed); R(NoTerrain); R(NotVisited); R(Placement); R(SceneWork);
    R(MissingAppearance); R(InvalidAppearance); R(TraversalLimit); R(InvalidTransform); R(TransformTree);
    R(EffectCombination); R(AnimationUnavailable); R(MaptextUnsupported); R(MaptextWork); R(Invisible); R(ZeroAlpha); R(AssetUnavailable); R(EmptyAsset); R(MissingState);
    R(FrameWork); R(EmptyFrame); R(EmptyBounds); R(OffView); R(Prepared);
    R(TemplateComplete); R(TemplateRefused); R(TemplateDeferred); R(NoMatchingRow); R(NotAtomRow);
    R(InfoUnsupported); R(InfoTransformTree); R(InfoWork); R(Composed);
    R(Occluded); R(VisibilityUnknown); R(VisibilityUnsupported); R(VisibilityBudget);
#undef R
    }
    return "Unobserved";
}
std::optional<AtomTrace> AtomTrace::parse(const QByteArray& selector, const QByteArray& row) {
    const bool mob = selector.startsWith("mob:");
    if (!mob && !selector.startsWith("obj:")) return {};
    auto number = [](const QByteArray& s, uint32_t& out) {
        if (s.isEmpty()) return false;
        const auto r = std::from_chars(s.constData(), s.constData()+s.size(), out);
        return r.ec == std::errc{} && r.ptr == s.constData()+s.size();
    };
    uint32_t id = 0, ordinal = 0;
    if (!number(selector.mid(4), id) || id >= 0x80000000u) return {};
    AtomTrace t; t.key = id | (mob ? 0x80000000u : 0u);
    if (!row.isEmpty()) {
        if (!number(row, ordinal) || ordinal >= 65536) return {};
        t.infoRow = ordinal;
    }
    return t;
}
namespace {
QJsonArray rect(const QRect& r) { return {r.x(), r.y(), r.width(), r.height()}; }
QJsonObject failure(const TraceFailure& f) {
    return {{"reason", traceReasonName(f.reason)}, {"appearance", qint64(f.appearance)}, {"parent", qint64(f.parent)}};
}
QJsonObject tree(const TraceTree& t) {
    QJsonArray nodes;
    for (const auto& n : t.nodes) {
        QJsonArray path, assets;
        for (auto id : n.path) path.append(qint64(id));
        for (const auto& a : n.assets) assets.append(QJsonObject{
            {"ordinal", qint64(a.ordinal)}, {"id", qint64(a.result.id)},
            {"revision", QString::number(a.result.revision)}, {"mapped", a.result.mapped},
            {"ready", a.result.ready}, {"status", IconCache::statusName(a.result.status)},
            {"depth", int(a.depth)}, {"offset", a.failure.offset}, {"operation", a.failure.operation}});
        nodes.append(QJsonObject{{"path", path}, {"ordinal", qint64(n.ordinal)}, {"assets", assets},
            {"requested", n.requested}, {"selected", n.selected}, {"fallback", n.fallback},
            {"direction", n.direction}, {"directionSlot", n.directionSlot}, {"baseCell", n.cell}, {"frame", n.frame},
            {"invisibility", n.invisibility}, {"alpha", n.alpha},
            {"transformed", n.transformed}, {"transformIgnored", n.transformIgnored},
            {"transformValid", n.transformIgnored ? QJsonValue() : QJsonValue(n.transformValid)}});
    }
    QJsonArray firstPath;
    for (auto id : t.firstPath) firstPath.append(qint64(id));
    return {{"nodes", nodes}, {"firstPath", firstPath}, {"truncated", t.truncated}};
}
}
QByteArray AtomTraceOutput::sample(const AtomTrace& t, uint64_t session, uint64_t mapPublication,
                                 int publishedSprites, uint64_t infoPublication, int64_t nowMs,
                                 const MapSprites* published, const QImage* thumbnail) {
    if (emitted_ >= 256 || nowMs < nextMs_) return {};
    nextMs_ = nowMs + 1000;
    const auto& m = t.map;
    QJsonObject map{{"present", m.present}, {"freed", m.freed}, {"positioned", m.positioned},
        {"appearance", qint64(m.appearance)}, {"lifetime", QString::number(m.lifetime)},
        {"wireKey", m.haveWireKey ? QJsonValue(m.wireKey) : QJsonValue()},
        {"position", QJsonArray{m.x, m.y, m.z}}, {"viewZ", m.viewZ}, {"seeInvisible", m.seeInvisible},
        {"visibility", QJsonObject{{"outcome",m.visibility}, {"scope","whole-appearance-bounds"},
            {"eye",QJsonArray{m.visibilityEye.x(),m.visibilityEye.y()}}, {"cells",rect(m.visibilityCells)},
            {"testedBounds",rect(m.visibilityBounds)}, {"fieldStatus",m.fieldStatus},
            {"haveEye",m.haveEye}, {"haveSight",m.haveSight},
            {"lightingScope","tile-anchor"}, {"anchorIllumination",m.anchorIllumination},
            {"seeInDark",m.haveSeeInDark ? QJsonValue(int(m.seeInDark)) : QJsonValue()},
            {"narrowSight",m.haveNarrowSight ? QJsonValue(qint64(m.narrowSight)) : QJsonValue()},
            {"wideSight",m.haveWideSight ? QJsonValue(qint64(m.wideSight)) : QJsonValue()},
            {"selfRef",m.haveSelf ? QJsonValue(qint64(m.selfRef)) : QJsonValue()},
            {"source",rect(m.source)}, {"terrainRegion",rect(m.terrainRegion)},
            {"scroll",QJsonArray{m.scroll.x(),m.scroll.y()}},
            {"sight",qint64(m.sight)}, {"unknownCells",qint64(m.unknownCells)}, {"deferredCells",qint64(m.deferredCells)},
            {"reused",m.visibilityReused}, {"anchorOpacity",m.anchorOpacity}, {"blockerAttribution","unobserved"}}},
        {"placement", traceReasonName(m.placement)}, {"first", failure(m.first)},
        {"templateStatus", traceReasonName(m.templateStatus)}, {"templateComplete", m.complete}, {"templateRevision", QString::number(m.revision)},
        {"parts", qint64(m.parts)}, {"affine", m.affine}, {"bounds", rect(m.bounds)},
        {"placedBounds", rect(m.placedBounds)}, {"view", rect(m.view)},
        {"preparedSprites", m.prepared}, {"publishedSprites", publishedSprites},
        {"detailsObserved", m.detailsObserved}, {"tree", tree(m.tree)}};
    if (m.validatedTransform) {
        const auto& v = *m.validatedTransform;
        map["validatedForward"] = QJsonArray{v.m11(),v.m12(),v.m21(),v.m22(),v.dx(),v.dy()};
    }
    QJsonObject out{{"schema", 1}, {"scope", "direct-world"}, {"session", QString::number(session)},
        {"registry", t.key & 0x80000000u ? "mob" : "obj"}, {"id", qint64(t.key & 0x7fffffffu)},
        {"map", map}, {"rendered", "unobserved"}, {"compositedMapPixels", "unobserved"}};
    if (t.infoRow) out["info"] = QJsonObject{{"ordinal", int(*t.infoRow)}, {"tab", t.info.tab},
        {"lifetime", QString::number(t.info.lifetime)}, {"appearance", qint64(t.info.appearance)},
        {"atomKey", t.info.hasAtomKey ? QJsonValue(qint64(t.info.atomKey)) : QJsonValue()},
        {"outcome", traceReasonName(t.info.outcome)}, {"first", failure(t.info.first)}, {"tree", tree(t.info.tree)}};
    if (t.inspectPixels) {
        auto scan = [](const QImage& image, uint64_t& budget) -> const char* {
            if (image.isNull()) return "unobserved";
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
                if (!budget) return "budget";
                --budget;
                if (image.pixelColor(x,y).alpha()) return "nontransparent";
            }
            return "transparent";
        };
        uint64_t budget = 65536;
        const char* sourceAlpha = "unobserved";
        if (published) {
            unsigned visits = 0;
            for (const auto& sprite : *published) {
                if (++visits > 65536) { sourceAlpha = "budget"; break; }
                if (sprite.atom != t.key || sprite.targetIdentity != m.lifetime || sprite.kind != MapSprite::Entity) continue;
                sourceAlpha = scan(sprite.image, budget);
                if (qstrcmp(sourceAlpha, "transparent") != 0) break;
            }
        }
        out["publishedSourceAlpha"] = sourceAlpha;
        budget = 65536;
        out["thumbnailAlpha"] = thumbnail ? scan(*thumbnail, budget) : "unobserved";
    }
    // Publication/rebuild counters and repeated not-ready retries alone are not transitions.
    auto comparison = out;
    for (const char* surface : {"map", "info"}) if (comparison.contains(surface)) {
        auto object = comparison[surface].toObject(); object.remove("templateRevision");
        auto detail = object["tree"].toObject(); auto nodes = detail["nodes"].toArray();
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            auto node = nodes[i].toObject(); auto assets = node["assets"].toArray();
            for (qsizetype j = 0; j < assets.size(); ++j) {
                auto asset = assets[j].toObject();
                if (!asset["ready"].toBool()) asset.remove("revision");
                assets[j] = asset;
            }
            node["assets"] = assets; nodes[i] = node;
        }
        detail["nodes"] = nodes; object["tree"] = detail; comparison[surface] = object;
    }
    auto bytes = QJsonDocument(comparison).toJson(QJsonDocument::Compact);
    if (bytes == previous_) return {};
    previous_ = bytes; // bounded by 2 trees * 16 nodes * 16 assets, never world data
    out["mapPublication"] = QString::number(mapPublication);
    out["infoPublication"] = QString::number(infoPublication);
    out["sequence"] = int(++emitted_);
    out["lastAllowedRecord"] = emitted_ == 256;
    bytes = QJsonDocument(out).toJson(QJsonDocument::Compact);
    if (bytes.size() > 16384) {
        map.remove("tree"); out["map"] = map;
        if (t.infoRow) { auto info = out["info"].toObject(); info.remove("tree"); out["info"] = info; }
        out["outputTruncated"] = true;
        bytes = QJsonDocument(out).toJson(QJsonDocument::Compact);
    }
    return bytes + '\n';
}
}
