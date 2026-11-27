#pragma once
#include "net/bytes.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <map>

namespace fugue::testinput {

struct RecordedFrame {
    uint16_t type;
    Bytes body;
};

struct TimedRecordedFrame {
    int64_t elapsedMs;
    RecordedFrame frame;
};

// Historical dumps count either body bytes or body bytes plus one in len=.
// A file must use one convention throughout. This reader preserves order and
// empty bodies; checkpoints and replay clocks belong to the calling scenario.
inline bool readRecordedFrames(const QString& path, std::vector<RecordedFrame>& out, QString& error) {
    out.clear();
    error.clear();
    qsizetype lineNumber = 0;
    auto fail = [&](const QString& reason) {
        error = QString("%1:%2: %3").arg(path).arg(lineNumber).arg(reason);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read recording: " + file.errorString());

    static const QRegularExpression record(
        R"(^0x([0-9a-fA-F]{4})[ \t]+len=([0-9]+)(?:[ \t]+(\S+))?$)");
    std::vector<RecordedFrame> frames;
    int lengthExtra = -1;
    while (!file.atEnd()) {
        ++lineNumber;
        const auto line = QString::fromUtf8(file.readLine()).trimmed();
        if (file.error() != QFileDevice::NoError) return fail("Cannot read recording: " + file.errorString());
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("===")) continue;
        const auto match = record.match(line);
        if (!match.hasMatch()) return fail("Expected 0xTTTT len=N followed by an optional hex body");

        bool validLength = false;
        const auto length = match.captured(2).toULongLong(&validLength);
        if (!validLength) return fail("Recorded length is out of range");
        const auto hex = match.captured(3).toLatin1();
        auto body = hexDecode(std::string_view(hex.constData(), size_t(hex.size())));
        if (!body) return fail("Body must contain an even number of hexadecimal digits");
        const auto bodyLength = qulonglong(body->size());
        if (length != bodyLength && length != bodyLength + 1)
            return fail(QString("Recorded length %1 does not match %2 body bytes").arg(length).arg(bodyLength));
        const int extra = int(length - bodyLength);
        if (lengthExtra >= 0 && extra != lengthExtra) return fail("Mixed len conventions in one recording");
        lengthExtra = extra;
        frames.push_back({match.captured(1).toUShort(nullptr, 16), std::move(*body)});
    }
    if (frames.empty()) {
        if (!lineNumber) lineNumber = 1;
        return fail("Recording contains no frames");
    }
    out = std::move(frames);
    return true;
}

// Elapsed milliseconds, a four-digit hex type (optionally 0x-prefixed), then
// an optional hex body. Equal timestamps retain wire order. Like untimed dumps,
// blank lines and whole-line comments/headers carry no frames.
inline bool readTimedRecordedFrames(const QString& path, std::vector<TimedRecordedFrame>& out, QString& error) {
    out.clear();
    error.clear();
    qsizetype lineNumber = 0;
    auto fail = [&](const QString& reason) {
        error = QString("%1:%2: %3").arg(path).arg(lineNumber).arg(reason);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read recording: " + file.errorString());

    static const QRegularExpression record(R"(^([0-9]+)[ \t]+(?:0x)?([0-9a-fA-F]{4})(?:[ \t]+(\S+))?$)");
    std::vector<TimedRecordedFrame> frames;
    while (!file.atEnd()) {
        ++lineNumber;
        const auto line = QString::fromUtf8(file.readLine()).trimmed();
        if (file.error() != QFileDevice::NoError) return fail("Cannot read recording: " + file.errorString());
        if (line.isEmpty() || line.startsWith('#') || line.startsWith("===")) continue;
        const auto match = record.match(line);
        if (!match.hasMatch())
            return fail("Expected nonnegative elapsed milliseconds, a four-digit hex type, and an optional hex body");

        bool validTime = false;
        const auto elapsedMs = match.captured(1).toLongLong(&validTime);
        if (!validTime) return fail("Elapsed milliseconds are out of range");
        if (!frames.empty() && elapsedMs < frames.back().elapsedMs)
            return fail("Elapsed milliseconds must not decrease");
        const auto hex = match.captured(3).toLatin1();
        auto body = hexDecode(std::string_view(hex.constData(), size_t(hex.size())));
        if (!body) return fail("Body must contain an even number of hexadecimal digits");
        frames.push_back({elapsedMs, {match.captured(2).toUShort(nullptr, 16), std::move(*body)}});
    }
    if (frames.empty()) {
        if (!lineNumber) lineNumber = 1;
        return fail("Recording contains no frames");
    }
    out = std::move(frames);
    return true;
}

// Resource IDs and bytes are retained input. The caller decides when to make
// them available to its session or asset cache.
inline bool readRecordedResources(const QString& path, std::map<uint32_t, QByteArray>& out, QString& error) {
    out.clear();
    error.clear();
    QDir dir(path);
    if (!dir.exists()) {
        error = path + ": Missing resource directory";
        return false;
    }
    const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (entries.isEmpty()) {
        error = path + ": Resource directory contains no files";
        return false;
    }
    static const QRegularExpression resourceName("^[0-9a-fA-F]{8}$");
    std::map<uint32_t, QByteArray> resources;
    for (const auto& entry : entries) {
        const auto filePath = entry.filePath();
        auto fail = [&](const QString& reason) {
            error = filePath + ": " + reason;
            return false;
        };
        if (!entry.isFile()) return fail("Expected a resource file");
        if (!resourceName.match(entry.fileName()).hasMatch()) return fail("Expected an eight-digit hexadecimal resource ID");
        const auto id = entry.fileName().toUInt(nullptr, 16);
        if (resources.contains(id)) return fail("Duplicate resource ID");
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read resource: " + file.errorString());
        auto data = file.readAll();
        if (file.error() != QFileDevice::NoError) return fail("Cannot read resource: " + file.errorString());
        if (data.isEmpty()) return fail("Resource file is empty");
        resources.emplace(id, std::move(data));
    }
    out = std::move(resources);
    return true;
}

// Parse without defaults. Each fixture validates its own required shape and
// observation inventory before replaying or iterating over expectations.
inline bool readRecordedJson(const QString& path, QJsonDocument& out, QString& error) {
    out = {};
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = path + ": Cannot read JSON: " + file.errorString();
        return false;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = path + ": Cannot read JSON: " + file.errorString();
        return false;
    }
    QJsonParseError parseError;
    auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QString("%1: Invalid JSON at byte %2: %3").arg(path).arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    out = std::move(document);
    return true;
}

// Shared by retained matrices, pixels and coordinates. The fixture supplies
// its tuple width, allowed range and diagnostic context.
inline bool isNumberTuple(const QJsonValue& value, qsizetype size) {
    if (!value.isArray() || value.toArray().size() != size) return false;
    for (const auto& item : value.toArray())
        if (!item.isDouble() || !std::isfinite(item.toDouble())) return false;
    return true;
}

inline bool isIntegerTuple(const QJsonValue& value, qsizetype size, int minimum, int maximum) {
    if (!isNumberTuple(value, size)) return false;
    for (const auto& item : value.toArray()) {
        const double number = item.toDouble();
        if (number < minimum || number > maximum || number != std::trunc(number)) return false;
    }
    return true;
}

}  // namespace fugue::testinput
