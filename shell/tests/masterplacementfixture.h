#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/tests/replayfixture.h"
#include <bit>

namespace fugue::shell::test {
// Existing independent wire/assets plus the declared placement fixture inputs.
// Expected pixels and callbacks come only from stock, not from these mutations.
struct MasterPlacementFixture : ReplayFixture {
    uint32_t rootId = 0;
    static bool rows(QJsonArray& out,QString& error) {
        out={};
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/stock-placement.json";
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& reason) {error=path+": "+reason;return false;};
        if (!document.isArray() || document.array().size()!=27) return fail("Expected 27 retained placements");
        const QStringList kinds{"leaf-blur0","leaf-blur1","overlay","overlay-blur1","overlay-group",
            "overlay-group-apart","overlay-group-leafplane","overlay-nested-group-apart-leafplane","underlay"};
        QStringList names;
        const auto rows=document.array();
        for (const auto& value:rows) {
            const auto row=value.toObject();const auto kind=row["kind"].toString(),placement=row["placement"].toString();
            const auto name=kind+'/'+placement;
            if (!kinds.contains(kind) || (placement!="corner" && placement!="center" && placement!="offset") || names.contains(name))
                return fail("Missing, invalid or duplicate placement: "+name);
            names.push_back(name);
            const QString location=placement=="corner" ? "1,1" : placement=="center" ? "CENTER,CENTER" : "2:7,2:5";
            if (row["screenLoc"].toString()!=location || row["golden"].toString().isEmpty())
                return fail(name+": missing or invalid screen location or stock image");
            const auto points=row["points"].toArray(),callbacks=row["callbacks"].toArray();
            if (points.size()!=5 || callbacks.size()!=5) return fail(name+": expected five points and five Click receipts");
            for (qsizetype i=0;i<points.size();++i) {
                if (!::fugue::testinput::isIntegerTuple(points[i],2,0,223))
                    return fail(QString("%1: invalid source point %2").arg(name).arg(i));
                const auto invalid=clickObservationError(callbacks[i].toObject());
                if (!invalid.isEmpty()) return fail(QString("%1: Click receipt %2: %3").arg(name).arg(i).arg(invalid));
            }
        }
        out=rows;return true;
    }
    void setLocation(const QString& location) {
        auto& ap=session.worldMut().appearances.at(rootId);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(ap))).object();
        fields["screenLoc"]=location;
        setFields(ap, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        refresh();
    }
    bool load(const QJsonObject& row) {
        if(!ReplayFixture::load("frames-overlay.txt","master-tree-world")) return false;
        auto& w=session.worldMut();
        if (w.hudAppid.empty()) {error="Master placement replay has no HUD root";return false;}
        rootId=w.hudAppid.begin()->second;
        if (!w.appearances.contains(rootId)) {error="Master placement replay has no root appearance";return false;}
        const auto original=w.appearances.at(rootId);
        auto root=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(original))).object();
        const auto overlays=root["overlays"].toArray();
        if (overlays.size()!=1) {error="Master placement replay must have one root overlay";return false;}
        const auto childId=uint32_t(overlays[0].toInt());
        if (!w.appearances.contains(childId)) {error="Master placement replay has no child appearance";return false;}
        auto& child=w.appearances.at(childId);
        auto leaf=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(child))).object();
        const auto kind=row["kind"].toString();
        if(kind.contains("apart")) leaf["appearanceFlags"]=double(uint32_t(leaf["appearanceFlags"].toDouble())|0x08000000u);
        if(kind.contains("leafplane")) {leaf["hasPlane"]=true;leaf["plane"]=2;}
        setFields(child, QJsonDocument(leaf).toJson(QJsonDocument::Compact).toStdString());
        root["screenLoc"]=row["screenLoc"];
        root["overlays"]=QJsonArray{};root["underlays"]=QJsonArray{};
        if(kind.contains("group")) root["appearanceFlags"]=double(uint32_t(root["appearanceFlags"].toDouble())|0x04000000u);
        if(kind.contains("overlay")) root["overlays"]=QJsonArray{int(childId)};
        if(kind.contains("underlay")) root["underlays"]=QJsonArray{int(childId)};
        if(kind.contains("nested")) {
            const auto nestedId=w.appearances.rbegin()->first+1;
            auto nested=original;
            auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(original))).object();
            fields["screenLoc"]="";fields["hasPlane"]=true;fields["plane"]=2;
            fields["overlays"]=QJsonArray{};fields["underlays"]=QJsonArray{};
            nested.fields.layerBits=std::bit_cast<uint32_t>(-1.0f);
            setFields(nested, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            w.appearances[nestedId]=nested;
            auto children=root["overlays"].toArray();children.append(int(nestedId));root["overlays"]=children;
        }
        root["filters"]=kind.contains("blur") ? QJsonArray{QJsonObject{{"type",1},{"flags",1},{"paramMask",1},
            {"params",kind.contains("blur1") ? "0000803f" : "00000000"}}} : QJsonArray{};
        setFields(w.appearances.at(rootId), QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString());
        refresh();return !sprites.empty();
    }
};
}
