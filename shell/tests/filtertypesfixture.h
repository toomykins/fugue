// Wave, ripple and bloom cases of tests/data/filter-types-world (stock 516.1688): the fixture's icons, its stock.json
// rows and one filtered sprite per case. Requires FUGUE_TEST_DATA_DIR.
#pragma once
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <memory>
#include "tests/recordedinput.h"
#include "model/json.h"
#include "shell/mapsprite.h"
#include "shell/planeeffects.h"

namespace fugue::test::filtertypes {
// prepare.py's icons, pixel for pixel.
inline QImage icon(bool shape) {
    QImage im(32, 32, QImage::Format_RGBA8888); im.fill(Qt::transparent);
    if (shape) {
        for (int y = 8; y <= 23; ++y) for (int x = 8; x <= 23; ++x) im.setPixelColor(x, y, QColor(240, 220, 40));
        for (int y = 12; y <= 19; ++y) for (int x = 24; x <= 25; ++x) im.setPixelColor(x, y, QColor(240, 220, 40, 128));
    } else {
        for (int y = 6; y < 26; ++y) for (int x = 6; x < 26; ++x)
            im.setPixelColor(x, y, (x/2 + y/2) % 2 ? QColor(240, 220, 40) : QColor(40, 80, 230));
    }
    for (int y = shape ? 10 : 8; y <= (shape ? 13 : 11); ++y)
        for (int x = shape ? 10 : 8; x <= (shape ? 13 : 11); ++x) im.setPixelColor(x, y, Qt::white);
    return im;
}
inline bool shapeIcon(const QString& name) { return name.startsWith("bloom") && name != "bloom-grid"; }
// Every row with captured wire records: set a and the bar sign's seven waves in set c.
inline bool recordRows(QJsonArray& out, QString& error) {
    out = {};
    const QString path = QString(FUGUE_TEST_DATA_DIR) + "/filter-types-world/stock.json";
    QJsonDocument document;
    if (!::fugue::testinput::readRecordedJson(path, document, error)) return false;
    auto fail = [&](const QString& field, const QString& reason) {
        error = path + ": " + field + ": " + reason;
        return false;
    };
    if (!document.isObject() || !document.object()["sets"].isObject())
        return fail("sets", "Expected an object of retained capture sets");
    const auto sets = document.object()["sets"].toObject();
    QJsonArray recorded;
    QSet<QString> names;
    qsizetype sampleCount = 0;
    for (const auto* set : {"a", "c"}) {
        const QString field = "sets." + QString(set);
        if (!sets[set].isArray()) return fail(field, "Expected an array of retained cases");
        int selected = 0;
        for (const auto& value : sets[set].toArray()) {
            if (!value.isObject()) return fail(field, "Expected case objects");
            const auto row = value.toObject();
            if (!row.contains("records")) continue; // these captures have no retained replay input
            const auto name = row["case"].toString();
            const QString context = field + '.' + name;
            if (name.isEmpty() || name != name.trimmed() || names.contains(name))
                return fail(field, "Expected nonempty, unique case names");
            if (QString(set) == "c" && name != "sign-static")
                return fail(context, "Expected the retained sign-static wire case");
            names.insert(name);
            const auto records = row["records"].toArray();
            const int expectedRecords = name == "sign-static" ? 7 :
                name == "wave-two" || name == "wave-split" || name == "ripple-two" ? 2 : 1;
            if (records.size() != expectedRecords)
                return fail(context + ".records", QString("Expected %1 retained filter records").arg(expectedRecords));
            for (const auto& record : records) {
                const auto filter = record.toObject();
                if (!::fugue::testinput::isIntegerTuple(QJsonArray{filter["type"], filter["paramMask"]}, 2, 0, 65535) ||
                    !::fugue::testinput::isIntegerTuple(QJsonArray{filter["flags"]}, 1, 0, 255))
                    return fail(context + ".records", "Expected integer type, paramMask and flags fields");
                const auto bytes = hexDecode(filter["params"].toString().toStdString());
                if (!filter["params"].isString() || !bytes || bytes->empty())
                    return fail(context + ".records", "Expected nonempty, even-length hexadecimal filter bytes");
            }
            if (!row["samples"].isArray()) return fail(context + ".samples", "Expected an explicit sample array");
            const auto samples = row["samples"].toArray();
            // ripple-sign changed no stock pixels, so its retained sample list
            // is intentionally empty. It still exercises record decoding.
            const int expectedSamples = name == "ripple-sign" ? 0 : 16;
            if (samples.size() != expectedSamples)
                return fail(context + ".samples", QString("Expected %1 retained pixel samples").arg(expectedSamples));
            if (name == "ripple-sign" && (!row["changed"].isDouble() || row["changed"].toDouble(-1) != 0))
                return fail(context + ".changed", "Expected the retained zero changed-pixel observation");
            for (const auto& sample : samples) {
                const auto values = sample.toArray();
                if (values.size() != 5 ||
                    !::fugue::testinput::isIntegerTuple(QJsonArray{values[0], values[1]}, 2, -64, 95) ||
                    !::fugue::testinput::isIntegerTuple(QJsonArray{values[2], values[3], values[4]}, 3, 0, 255))
                    return fail(context + ".samples", "Expected [x,y,r,g,b] inside the 160x160 map relative to tile (64,64)");
            }
            if (!row["fugueDiffOver8"].isDouble() || row["fugueDiffOver8"].toDouble(-1) != 0)
                return fail(context + ".fugueDiffOver8", "Expected the retained zero-difference observation");
            recorded.append(row);
            sampleCount += samples.size();
            ++selected;
        }
        const int expected = QString(set) == "a" ? 20 : 1;
        if (selected != expected) return fail(field, QString("Expected %1 cases with retained filter records").arg(expected));
    }
    for (const char* name : {"ripple-sign", "wave-diag", "ripple-xy", "bloom-soft", "wave-two", "wave-split", "ripple-two"})
        if (!names.contains(name)) return fail("sets", "Missing required case: " + QString(name));
    if (sampleCount != 320) return fail("sets", "Expected 320 retained pixel samples");
    out = recorded;
    return true;
}
inline shell::PlaneEffects effects(const QJsonObject& row) {
    QJsonObject fields{{"filters", row["records"].toArray()}};
    return shell::decodeSurfaceEffects(model::appearanceOf(QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString()));
}
// The case atom at `tile` (its icon's top-left), drawn through an atom-filter group like EntityMap's.
inline shell::MapSprite sprite(const QJsonObject& row, QPoint tile) {
    shell::MapSprite s;
    s.image = icon(shapeIcon(row["case"].toString()));
    s.position = tile; s.groupOrigin = tile; s.groupOccurrence = 1;
    auto group = std::make_shared<shell::AppearanceGroup>();
    group->effects = std::make_shared<const shell::PlaneEffects>(effects(row));
    group->filterOnly = true;
    s.appearanceGroup = group;
    return s;
}
}
