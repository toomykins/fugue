// Stock 516.1688 narrow/wide advertisements, appearance membership and commands.
#include <QtTest>
#include "tests/testmain.h"
#include "tests/recordedinput.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "net/handshake.h"
#include "net/input.h"
#include "model/world.h"
#include "model/verbmenu.h"
#include "model/statpanel.h"

using namespace fugue;
namespace {
const QString corpusPath = QStringLiteral(FUGUE_TEST_DATA_DIR "/verb-lookup-world/stock-observations.json");
struct Row { bool server; testinput::RecordedFrame frame; };
using Corpora = std::map<QString, std::vector<Row>>;
bool corpus(Corpora& out, QString& error) {
    out.clear();
    QJsonDocument document;
    if (!testinput::readRecordedJson(corpusPath, document, error)) return false;
    auto fail = [&](const QString& field, const QString& reason) {
        error = corpusPath + ": " + field + ": " + reason;
        return false;
    };
    if (!document.isObject() || document.object().size() != 4)
        return fail("root", "Expected the four retained width corpora");
    Corpora recorded;
    static const QRegularExpression frameType("^[0-9a-fA-F]{4}$");
    for (const char* name : {"small", "mid", "wide_low", "wide_high"}) {
        const auto rows = document.object()[name].toObject()["rows"].toArray();
        if (rows.size() != 23) return fail(name, "Expected 23 retained rows");
        int serverRows = 0, clientRows = 0, headers = 0;
        bool wide = false;
        for (qsizetype i = 0; i < rows.size(); ++i) {
            const auto row = rows[i].toObject();
            const QString context = QString("%1.rows[%2]").arg(name).arg(i);
            const auto direction = row["dir"].toString(), type = row["type"].toString();
            if (direction != "s2c" && direction != "c2s") return fail(context + ".dir", "Expected s2c or c2s");
            const bool server = direction == "s2c";
            auto body = hexDecode(row["body"].toString().toStdString());
            if (!row["body"].isString() || !body || body->empty())
                return fail(context + ".body", "Expected nonempty, even-length hexadecimal bytes");
            uint16_t id = S2C::Handshake;
            if (type == "0001-header") {
                net::HandshakeReply reply;
                if (!server || i != 0 || body->size() != 11 || !net::parseHandshakeReply(*body, reply))
                    return fail(context, "Expected one 11-byte s2c handshake header before all other input");
                ++headers;
                wide = reply.wideVerbIds();
            } else {
                if (headers != 1) return fail(context, "Missing handshake header before input");
                if (!frameType.match(type).hasMatch()) return fail(context + ".type", "Expected a four-digit hex type");
                id = type.toUShort(nullptr, 16);
                if (!row["size"].isDouble() || row["size"].toDouble(-1) != double(body->size()))
                    return fail(context + ".size", "Recorded size must equal the decoded body length");
                if (server) {
                    if (id != S2C::MouseSubs && id != S2C::SelfRef && id != S2C::Appearance &&
                        id != S2C::MobBind && id != S2C::Verb && id != S2C::ClientAppearance)
                        return fail(context + ".type", "Unexpected server record in the verb-width corpus");
                } else {
                    const size_t ref = 2 + (wide ? 4 : 2) + 1;
                    const size_t base = ref + 5;
                    if (id != C2S::Command || body->size() < base || (*body)[0] != 2 || (*body)[1] != 0 ||
                        (*body)[ref - 1] != proto::enc::kVerbSrcClient)
                        return fail(context, "Expected a negotiated-width verb command with a client source");
                    const auto argc = (*body)[base - 1];
                    if (argc == 0) {
                        if (body->size() != base) return fail(context + ".body", "Trailing bytes after a no-argument command");
                    } else if (argc == 1) {
                        if (body->size() < base + 2 || (*body)[base] != 2 ||
                            std::find(body->begin() + base + 1, body->end(), 0) != body->end() - 1)
                            return fail(context + ".body", "Expected one complete NUL-terminated text argument");
                    } else return fail(context + ".body", "Expected zero arguments or one text argument");
                }
            }
            if (server) ++serverRows; else ++clientRows;
            recorded[name].push_back({server, {id, std::move(*body)}});
        }
        if (headers != 1 || serverRows != 18 || clientRows != 5)
            return fail(name, "Expected 18 s2c rows including one header, and five c2s commands");
    }
    out = std::move(recorded);
    return true;
}
struct Replay {
    net::InputModel input;
    model::Session model;
    bool wide = false;
    std::vector<Bytes> commands;
    bool load(const std::vector<Row>& rows, const QString& name, QString& error) {
        error.clear();
        auto fail = [&](size_t row, const QString& reason) {
            error = QString("%1: %2.rows[%3]: %4").arg(corpusPath, name).arg(row).arg(reason);
            return false;
        };
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const auto& [type, body] = row.frame;
            if (type == S2C::Handshake) {
                net::HandshakeReply reply;
                if (!net::parseHandshakeReply(body, reply)) return fail(i, "Cannot decode the retained handshake header");
                wide = reply.wideVerbIds();
                input.setWideVerbIds(wide);
                model.setWideVerbIds(wide);
            } else if (row.server) {
                input.onFrame(type, body);
                const auto errors = model.ingestErrors();
                model.ingest(type, body);
                if (model.ingestErrors() != errors) return fail(i, "Model decoder rejected the retained server frame");
            } else {
                auto normalized = body;
                // Stock's /client src-ref is unused; compare all other bytes exactly.
                const size_t ref = 2 + (wide ? 4 : 2) + 1;
                if (normalized.size() < ref + 4) return fail(i, "Truncated negotiated-width command source");
                std::fill(normalized.begin() + ref, normalized.begin() + ref + 4, 0);
                commands.push_back(std::move(normalized));
            }
        }
        return true;
    }
    net::CommandResult command(const char* text) {
        return input.verbCommand(text, model.world().haveSelf, [&](uint32_t id) {
            std::vector<net::SrcCandidate> out;
            for (const auto& candidate : model::verbSrcCandidates(model.world(), id, true))
                out.push_back({{candidate.src.kind, candidate.src.ref}, candidate.name, false});
            return out;
        });
    }
};
}
class VerbWidthTest : public QObject {
    Q_OBJECT
private slots:
    void stock_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"small", "mid", "wide_low", "wide_high"})
            QTest::newRow(name) << QString(name);
    }
    void stock() {
        QFETCH(QString, name);
        Corpora data; QString error;
        QVERIFY2(corpus(data, error), qPrintable(error));
        Replay replay;
        QVERIFY2(replay.load(data.at(name), name, error), qPrintable(error));
        QCOMPARE(replay.wide, name.startsWith("wide_"));
        QCOMPARE(replay.model.ingestErrors(), 0);
        QCOMPARE(replay.input.verbs().size(), size_t(3));
        const auto* action = replay.input.verbByName("Probe Action");
        QVERIFY(action);
        QCOMPARE(action->wideId, replay.wide);
        QCOMPARE(action->id, name == "small" || name == "wide_low" ? uint32_t(3) : name == "mid" ? uint32_t(33003) : uint32_t(66004));
        const auto& world = replay.model.world();
        QVERIFY(world.haveClientAppid && world.haveSelf);
        QVERIFY(world.appearances.contains(world.clientAppid));
        const auto& appearance = world.appearances.at(world.clientAppid);
        const auto& verbs = appearance.fields.verbs;
        QVERIFY(std::find(verbs.begin(), verbs.end(), action->id) != verbs.end());
        const auto candidates = model::verbSrcCandidates(world, action->id, true);
        QCOMPARE(candidates.size(), size_t(1));
        QCOMPARE(candidates[0].src.kind, uint8_t(8));
        QVERIFY(world.verbById.contains(action->id));
        const model::VerbDef def = model::verbDefOf(world.verbById.at(action->id));
        QCOMPARE(def.id, action->id);
        std::vector<uint32_t> scope;
        QVERIFY(model::verbScope(world, scope));
        QVERIFY(std::find(scope.begin(), scope.end(), action->id) != scope.end());
        QCOMPARE(replay.commands.size(), size_t(5));
        if (name == "wide_high") {
            // Stock truncated these high IDs and invoked nothing. Do not alias to
            // a different low-ID verb, or silently claim high-ID dispatch support.
            QVERIFY(model::verbSrcCandidates(world, uint16_t(action->id), true).empty());
            QVERIFY(!replay.input.verbs().count(uint16_t(action->id)));
            const auto result = replay.command("Probe-Action");
            QVERIFY(result.frames.empty());
            QVERIFY(result.error.find("Unsupported verb ID") != std::string::npos);
        } else {
            size_t index = 0;
            for (const char* cmd : {"Probe-Action", "Probe-Action", "Probe-Action", "Late-Probe", "Probe-Text hello"}) {
                const auto result = replay.command(cmd);
                QVERIFY2(result.error.empty(), result.error.c_str());
                QCOMPARE(result.frames.size(), size_t(1));
                QCOMPARE(result.frames[0].body, replay.commands[index++]);
            }
            const auto pending = replay.command("Probe-Text");
            QVERIFY(pending.missing.has_value());
            const auto completed = net::InputModel::completeArg(*pending.missing, "hello", true);
            QCOMPARE(completed.frames.size(), size_t(1));
            QCOMPARE(completed.frames[0].body, replay.commands.back());
        }
        QVERIFY(replay.command("Missing-Probe").frames.empty());
    }
    void negotiatedMode() {
        Corpora data; QString error;
        QVERIFY2(corpus(data, error), qPrintable(error));
        Replay replay;
        // The same consumer can be reconfigured; no width guessed from the verb
        // ID, empty-name prefix, or the unrelated 0x003b high flag.
        for (const char* name : {"wide_low", "small", "wide_low", "mid"}) {
            QVERIFY2(replay.load(data.at(name), name, error), qPrintable(error));
            const auto result = replay.command("Probe-Action");
            QVERIFY2(result.error.empty(), result.error.c_str());
            const size_t expected = replay.wide ? 12 : 10;
            QCOMPARE(result.frames.size(), size_t(1));
            QCOMPARE(result.frames.at(0).body.size(), expected);
        }
    }
    void malformedFrames() {
        net::InputModel input;
        input.setWideVerbIds(true);
        const auto good = hexBytes("0300000050726f626520416374696f6e000000ff000400");
        for (size_t size = 0; size < good.size(); ++size) {
            input.onFrame(0x11, ByteSpan(good).first(size));
            QVERIFY(input.verbs().empty());
        }
        auto trailing = good;
        trailing.push_back(0);
        input.onFrame(0x11, trailing);
        QVERIFY(input.verbs().empty());
        input.onFrame(0x11, good);
        QCOMPARE(input.verbs().size(), size_t(1));
    }
};
FUGUE_TEST_GUILESS_MAIN(VerbWidthTest)
#include "verbwidth_test.moc"
