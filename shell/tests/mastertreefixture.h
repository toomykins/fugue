#pragma once
#include "tests/appearancejson.h"
#include "tests/recordedinput.h"
#include "shell/tests/replayfixture.h"
#include "shell/mapinput.h"
#include "model/verbmenu.h"
#include "model/statpanel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <array>
namespace fugue::shell::test {
struct MasterTreeFixture : ReplayFixture {
    static QString goldenFile(const QString& file) {
        // Byte-identical independent stock captures share one retained oracle.
        const QStringList aliases{"stock-g-overlay-group-apart-leafplane.png",
            "stock-g-overlay-group-apart-offset.png",
            "stock-g-overlay-group-apart-zero.png",
            "stock-g-overlay-group-apart.png",
            "stock-g-overlay-group-leafplane.png",
            "stock-g-overlay-leafplane.png",
            "stock-o-overlay-group-apart.png",
            "stock-q-overlay-group-apart-size2.png"};
        return aliases.contains(file) ? "stock-overlay.png" : file;
    }
private:
    struct ObservationFile { const char* name; int phases, points; };
    // Retained phase and Click inventories; selected tests validate the same
    // expectations as a full replay instead of accepting a shortened loop.
    static constexpr std::array<ObservationFile,5> observationFiles{{
        {"stock-observations.json",15,4}, {"stock-group-observations.json",32,9},
        {"stock-outline-observations.json",16,13}, {"stock-outline2-observations.json",16,19},
        {"stock-outline-wide-observations.json",22,19}}};
    static const QRegularExpression& clickPattern() {
        static const QRegularExpression pattern(
            "^CLICK the (.+?) loc=(.*?) icon-x=(-?\\d+);icon-y=(-?\\d+);left=1;button=left;"
            "screen-loc=((-?\\d+):(-?\\d+),(-?\\d+):(-?\\d+))$");
        return pattern;
    }
    static QStringList receipts(const QJsonObject& row) {
        QStringList out;
        for (const auto& raw:row["events"].toString().split('\n')) {
            const auto line=raw.trimmed();
            if (!line.isEmpty()) out.push_back(line);
        }
        return out;
    }
    static bool observations(const ObservationFile& file,QJsonArray& out,QString& error) {
        out={};
        const QString path=QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+file.name;
        QJsonDocument document;
        if (!::fugue::testinput::readRecordedJson(path,document,error)) return false;
        auto fail=[&](const QString& reason) {error=path+": "+reason;return false;};
        if (!document.isArray() || document.array().size()!=file.phases)
            return fail(QString("Expected %1 retained phases").arg(file.phases));
        const auto rows=document.array();
        QStringList names;
        for (const auto& value:rows) {
            const auto row=value.toObject();const auto name=row["phase"].toString();
            if (!value.isObject() || name.isEmpty() || name!=name.trimmed() || names.contains(name))
                return fail("Missing, invalid or duplicate phase name: "+name);
            names.push_back(name);
            const auto points=row["pixelsTopLeft"].toArray();
            if (points.size()!=file.points)
                return fail(QString("%1: expected %2 sample points").arg(name).arg(file.points));
            for (const auto& point:points) {
                if (!::fugue::testinput::isIntegerTuple(point,2,0,223))
                    return fail(name+": expected two integer sample coordinates inside the 224x224 source");
            }
            const auto clicks=receipts(row);
            if (!row["events"].isString() || clicks.size()!=file.points)
                return fail(QString("%1: expected %2 Click receipts").arg(name).arg(file.points));
            for (const auto& click:clicks) {
                const auto match=clickPattern().match(click);
                bool valid=match.hasMatch();
                for (int field:{3,4,6,7,8,9}) {
                    bool integer=false;match.captured(field).toInt(&integer);valid=valid && integer;
                }
                if (!valid)
                    return fail(name+": invalid retained Click receipt: "+click);
            }
        }
        out=rows;return true;
    }
public:
    static bool phases(QStringList& out,QString& error) {
        out.clear();error.clear();
        QStringList names;
        for (const auto& file:observationFiles) {
            QJsonArray rows;
            if (!observations(file,rows,error)) return false;
            for (const auto& value:rows) {
                const auto name=value.toObject()["phase"].toString();
                if (names.contains(name)) {error="Duplicate master-tree phase across observation files: "+name;return false;}
                names.push_back(name);
            }
        }
        out=names;return true;
    }
    static QPoint point(int i) { return std::array<QPoint,4>{{{104,104},{136,104},{40,168},{60,168}}}[i]; }
    QStringList callbacks;
    QList<QPoint> points;
    QPoint clickPoint(int i) const { return points.at(i); }
    bool load(const QString& phase) {
        points.clear();callbacks.clear();error.clear();
        const auto& file=observationFiles[phase.startsWith("w-") ? 4 : phase.startsWith("q-") ? 3 :
            phase.startsWith("o-") ? 2 : phase.startsWith("g-") ? 1 : 0];
        QJsonArray rows;
        if (!observations(file,rows,error)) return false;
        for (const auto& value:rows) {
            const auto row=value.toObject();
            if(row["phase"].toString()==phase) {
                if (!ReplayFixture::load("frames-"+phase+".txt","master-tree-world")) return false;
                for(const auto& point:row["pixelsTopLeft"].toArray()) {
                    const auto xy=point.toArray();points.push_back({xy[0].toInt(),xy[1].toInt()});
                }
                callbacks=receipts(row);
                return true;
            }
        }
        error=QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+file.name+": missing phase "+phase;
        return false;
    }
    QString checkClick(const MapPointer& p,int i) const {
        const auto& ref=p.click.over; const auto& loc=p.click.loc;
        auto target=model::targetFromHit(session.world(),ref.kind,ref.ref,int(loc.x)+1,int(loc.y)+1,p.screen);
        const auto ap=session.world().appearances.find(target.appid);
        const QString name=ap==session.world().appearances.end() ? "" :
            QString::fromStdString(model::plainAtomName(model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr()));
        const auto match=clickPattern().match(callbacks[i]);
        if(!match.hasMatch()) return "invalid retained callback";
        if(name!=match.captured(1)) return "target: "+name+" expected "+match.captured(1);
        if(p.screen!=match.captured(2).isEmpty()) return "location ownership";
        if(p.click.iconX!=match.captured(3).toInt() || p.click.iconY!=match.captured(4).toInt())
            return QString("coordinates %1,%2 expected %3,%4").arg(p.click.iconX).arg(p.click.iconY).arg(match.captured(3),match.captured(4));
        if(p.click.hasVis) return "unexpected vis coordinates";
        const auto screen=QString("%1:%2,%3:%4").arg(p.click.screenX).arg(p.click.screenPX).arg(p.click.screenY).arg(p.click.screenPY);
        return screen==match.captured(5) ? QString() : "screen-loc: "+screen;
    }
};
}
