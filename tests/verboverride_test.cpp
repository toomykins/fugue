// verboverride: a typed verb goes out under the id its src carries, graded against STOCK DreamSeeker.
//
// An override (/mob/living/whisper beside /mob/verb/whisper) is its own verb id under the same name, and a type's verb
// list holds only its own. vg's Whisper is exactly that: a client that has seen the /mob id (the lobby, a ghost) and
// picks it by name finds no atom carrying it on a human.
//
// The scene is a RECORDED session of tests/data/verboverride-world (login as a human, become-plain, become-human; stock DD
// 516.1684), so the dictionary holds both ids of Whisper and Poke. dreamseeker-verbs.txt is what stock DreamSeeker 516.1684
// sent for each typed line against the same DD: the override's id on the own mob, and for Use-It, carried by two obj
// types under two ids, a `Use-It word` listbox over both whose picks go out under each obj's own id.
#include <QtTest>
#include "tests/recordedinput.h"
#ifndef Q_MOC_RUN
#include "tests/recordedverbs.h"
#endif
#include "tests/testmain.h"
#include <algorithm>
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/bytes.h"
#include "net/input.h"
#include "proto/enc_c2s_0002_input.h"

using namespace fugue;

namespace {
using DsSend = testinput::RecordedVerb;

struct Recorded {
    model::Session sess;
    net::InputModel im;
    QString error;
    bool load() {
        const auto path = QString(FUGUE_TEST_DATA_DIR) + "/verboverride-world/frames.txt";
        std::vector<testinput::RecordedFrame> frames;
        if (!testinput::readRecordedFrames(path, frames, error)) return false;
        for (const auto& frame : frames) {
            im.onFrame(frame.type, frame.body);
            sess.ingest(frame.type, frame.body);
        }
        if (!sess.world().haveSelf) error = path + ": Recorded world has no player";
        return sess.world().haveSelf;
    }
    // The app's resolver (app/sessionworker.cpp srcFor).
    net::InputModel::SrcResolver srcFor() const {
        return [this](uint16_t id) {
            std::vector<net::SrcCandidate> out;
            const model::World& w = sess.world();
            for (const model::VerbSrcCandidate& c : model::verbSrcCandidates(w, id, true)) {
                bool own = c.src.kind == proto::enc::kVerbSrcClient ||
                           (c.src.kind == proto::enc::kVerbSrcMob && c.src.ref == w.selfRef);
                out.push_back(
                    {{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
            }
            return out;
        };
    }
};
}  // namespace

class VerbOverride : public QObject {
    Q_OBJECT
    std::vector<DsSend> sends_;
    std::vector<std::string> listbox_;

private slots:
    void initTestCase() {
        const auto path = QString(FUGUE_TEST_DATA_DIR) + "/verboverride-world/dreamseeker-verbs.txt";
        std::vector<DsSend> observations;
        QString error;
        QVERIFY2(testinput::readRecordedVerbs(path, observations, error), qPrintable(error));
        QCOMPARE(int(observations.size()), 9);
        for (const auto& observation : observations) {
            if (observation.what == "send") {
                QVERIFY(observation.ref != "client");
                QVERIFY(observation.args.empty() ||
                        (observation.args.size() == 1 && observation.args[0].tag == 2));
                sends_.push_back(observation);
            } else {
                QCOMPARE(observation.what, std::string("listbox"));
                QCOMPARE(observation.line, std::string("Use-It"));
                QCOMPARE(observation.title, std::string("Use-It word"));
                QVERIFY(listbox_.empty());
                listbox_ = observation.names;
            }
        }
        QCOMPARE(int(sends_.size()), 8);
        QCOMPARE(int(listbox_.size()), 3);
        int picks = 0;
        for (const auto& send : sends_) {
            if (send.pick.empty()) continue;
            QCOMPARE(send.line, std::string("Use-It"));
            QVERIFY(std::find(listbox_.begin(), listbox_.end(), send.pick) != listbox_.end());
            ++picks;
        }
        QCOMPARE(picks, 3);
    }

    // Both ids of each overridden name are in the dictionary: Whisper 2 (/mob, which the human does not carry) and 9
    // (/mob/living), Poke 8 and 13. Ids are DreamDaemon's own: this session learned 9 first, as a human.
    void theDictionaryHoldsBothIds() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        int whisper = 0, poke = 0;
        for (const auto& [id, e] : r.im.verbs()) {
            whisper += e.name == "Whisper";
            poke += e.name == "Poke";
        }
        QCOMPARE(whisper, 2);
        QCOMPARE(poke, 2);
        QVERIFY(model::verbSrcCandidates(r.sess.world(), 2).empty());  // /mob/verb/whisper
    }

    void eachTypedLineGoesOutAsDreamSeekerSentIt() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        const model::World& w = r.sess.world();
        for (const DsSend& d : sends_) {
            net::CommandResult res = r.im.verbCommand(d.line, true, r.srcFor());
            if (!d.pick.empty()) {
                QVERIFY2(res.srcChoice, qPrintable(QString::fromStdString(d.line + ": no listbox, " + res.error)));
                std::vector<std::string> names;
                std::vector<net::SrcCandidate> picked;
                for (const net::SrcCandidate& c : res.srcChoice->candidates) {
                    names.push_back(c.name);
                    if (c.name == d.pick) picked.push_back(c);
                }
                QVERIFY(names == listbox_);
                QCOMPARE(int(picked.size()), 1);
                QCOMPARE(QString::fromStdString(res.srcChoice->title), QString("Use-It word"));
                res = r.im.verbCommand(res.srcChoice->line, true, [&](uint16_t) { return picked; });
            }
            QVERIFY2(res.frames.size() == 1, qPrintable(QString::fromStdString(d.line + ": " + res.error)));
            const auto ref = d.ref == "self" ? w.selfRef : uint32_t(std::stoul(d.ref));
            Bytes want = proto::enc::encodeVerb(d.verb, d.kind, ref, d.args);
            QVERIFY2(res.frames[0].body == want,
                     qPrintable(QString::fromStdString(d.line + " " + d.pick + ": got " + hexEncode(res.frames[0].body) +
                                                       ", DreamSeeker " + hexEncode(want))));
        }
    }

    // A verb the dictionary knows but nothing carries: DreamSeeker sends nothing and prints nothing.
    void anUncarriedVerbIsDroppedSilently() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        net::CommandResult res =
            r.im.verbCommand("Whisper hi", true, [](uint16_t) { return std::vector<net::SrcCandidate>{}; });
        QVERIFY(res.frames.empty() && !res.srcChoice);
        QVERIFY(res.quiet);
        res = r.im.verbCommand("Zzz", true, r.srcFor());
        QCOMPARE(QString::fromStdString(res.error), QString("Unrecognized or inaccessible verb: Zzz"));
        QVERIFY(!res.quiet);
    }

    // A prefix that only a verb and its override share completes to their name.
    void anOverrideIsNoAmbiguousPrefix() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        std::string out;
        QVERIFY(r.im.spaceCompletion("Whis", out));
        QCOMPARE(QString::fromStdString(out), QString("Whisper \""));
    }
};

FUGUE_TEST_GUILESS_MAIN(VerbOverride)
#include "verboverride_test.moc"
