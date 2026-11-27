#pragma once
#include "proto/enc_c2s_0002_input.h"
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <set>
#include <tuple>

namespace fugue::testinput {

// The retained DreamSeeker command, pick, dialog and completion observations.
struct RecordedVerb {
    std::string line, pick, what;  // send / listbox / dialog / completes / refuses
    uint16_t verb = 0;
    uint8_t kind = 0;
    std::string ref;  // self / client / a number
    std::vector<proto::enc::VerbArg> args;
    std::string title;
    std::vector<std::string> names;  // listbox entries or printed lines
};

inline bool readRecordedVerbs(const QString& path, std::vector<RecordedVerb>& out, QString& error) {
    out.clear();
    error.clear();
    qsizetype lineNumber = 0;
    auto fail = [&](const QString& reason) {
        error = QString("%1:%2: %3").arg(path).arg(lineNumber).arg(reason);
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail("Cannot read verb observations: " + file.errorString());
    static const QRegularExpression send(
        R"(^verb=([0-9]+) src=([0-9]+):(self|client|[0-9]+)(?: args=(null|num:[^ \t]+|obj:[0-9]+|text:[^\t]*))?$)");
    static const QRegularExpression quoted(
        R"re(^(listbox|dialog|completes) "((?:[^"\\]|\\.)*)"(?:: ([^\t]+))?$)re");
    static const QRegularExpression escape(R"(\\(.))");
    std::vector<RecordedVerb> records;
    std::set<std::tuple<std::string, std::string, bool>> identities;
    while (!file.atEnd()) {
        ++lineNumber;
        auto line = QString::fromUtf8(file.readLine());
        if (file.error() != QFileDevice::NoError) return fail("Cannot read verb observations: " + file.errorString());
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        if (line.trimmed().isEmpty() || line.trimmed().startsWith('#')) continue;
        const auto tab = line.indexOf('\t');
        if (tab <= 0) return fail("Expected a typed command and a tab-separated observation");
        const auto typed = line.left(tab);
        auto body = line.mid(tab + 1);
        const auto comment = body.indexOf('\t');
        if (comment >= 0) {
            if (!body.mid(comment).trimmed().startsWith('#')) return fail("Unexpected trailing observation fields");
            body = body.left(comment);
        }
        body = body.trimmed();
        RecordedVerb record;
        const auto arrow = typed.indexOf(" <- ");
        const auto command = arrow < 0 ? typed : typed.left(arrow);
        if (command.trimmed().isEmpty()) return fail("Typed command is empty");
        record.line = command.toStdString();
        if (arrow >= 0) {
            const auto pick = typed.mid(arrow + 4);
            if (pick.trimmed().isEmpty() || pick.contains(" <- ")) return fail("Expected one nonempty pick");
            record.pick = pick.toStdString();
        }
        auto match = send.match(body);
        if (match.hasMatch()) {
            record.what = "send";
            bool valid = false;
            const auto verb = match.captured(1).toUInt(&valid);
            if (!valid || verb > UINT16_MAX) return fail("Verb ID is outside uint16 range");
            record.verb = uint16_t(verb);
            const auto kind = match.captured(2).toUInt(&valid);
            if (!valid || kind > UINT8_MAX) return fail("Source kind is outside uint8 range");
            record.kind = uint8_t(kind);
            record.ref = match.captured(3).toStdString();
            if (record.ref != "self" && record.ref != "client") {
                const auto ref = match.captured(3).toULongLong(&valid);
                if (!valid || ref > UINT32_MAX) return fail("Source reference is outside uint32 range");
            }
            if ((record.ref == "self" && kind != proto::enc::kVerbSrcMob) ||
                (record.ref == "client" && kind != proto::enc::kVerbSrcClient))
                return fail("Symbolic source reference does not match its kind");
            if (match.hasCaptured(4)) {
                const auto arg = match.captured(4);
                proto::enc::VerbArg value;
                if (arg == "null") {
                    value.tag = 0;
                    value.kind = 0;
                    value.ref = 0xffff;
                } else if (arg.startsWith("num:")) {
                    value.tag = 1;
                    value.num = arg.mid(4).toFloat(&valid);
                    if (!valid || !std::isfinite(value.num)) return fail("Expected a finite numeric argument");
                } else if (arg.startsWith("obj:")) {
                    value.tag = 0;
                    value.kind = 1;
                    const auto ref = arg.mid(4).toULongLong(&valid);
                    if (!valid || ref > UINT32_MAX) return fail("Object argument is outside uint32 range");
                    value.ref = uint32_t(ref);
                } else {
                    value.tag = 2;
                    value.str = arg.mid(5).toStdString();
                }
                record.args.push_back(value);
            }
        } else if ((match = quoted.match(body)).hasMatch()) {
            record.what = match.captured(1).toStdString();
            record.title = match.captured(2).replace(escape, "\\1").toStdString();
            if (record.what == "listbox") {
                if (!match.hasCaptured(3)) return fail("Listbox has no entries");
                std::set<std::string> names;
                for (const auto& name : match.captured(3).split('|')) {
                    if (name.trimmed().isEmpty() || !names.insert(name.toStdString()).second)
                        return fail("Listbox entries must be nonempty and unique");
                    record.names.push_back(name.toStdString());
                }
            } else if (match.hasCaptured(3)) {
                return fail("Unexpected entries after a dialog or completion");
            }
        } else if (body.startsWith("refuses ")) {
            record.what = "refuses";
            for (const auto& message : body.mid(8).split(" | ")) {
                if (message.trimmed().isEmpty()) return fail("Refusal contains an empty message");
                record.names.push_back(message.toStdString());
            }
        } else {
            return fail("Unrecognized or malformed verb observation");
        }
        if (!record.pick.empty() && record.what != "send") return fail("A pick must have a send observation");
        // Executing a command and pressing space on the same text are distinct inputs.
        if (!identities.emplace(record.line, record.pick, record.what == "completes").second)
            return fail("Duplicate command/pick observation");
        records.push_back(std::move(record));
    }
    if (records.empty()) return fail("No verb observations");
    out = std::move(records);
    return true;
}

}  // namespace fugue::testinput
