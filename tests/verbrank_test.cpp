// verbrank: which of several same-named verbs a typed line runs, and on which src, graded against STOCK DreamSeeker.
//
// tests/data/verbrank-world gives one verb name to verbs on different owners: client + mob, mob + obj in view, client
// + obj, two obj types, an obj override, a mob verb in view (own mob + two npcs), turf / area defaults, `set src =
// view(1)`, objs in usr, and pairs whose arguments differ (num against text). vg collides the same way (Hide, Toggle
// Listening, Evolve, Crawl through Vent).
//
// tests/data/verbrank-reversed-world declares the owners of its pairs in reverse, so the verb ids run the other way.
// tests/data/verborder-world gives the lower id to a verb whose 0x0011 arrives last (an obj made after login), and
// lists several atoms of one name. tests/data/verbnear-world and verbwide-world set srcs apart by distance, footprint
// and the tie-breaks at one distance. tests/data/verbcell-world, verblate-world and verbpile-world put same-named srcs
// on one cell as atoms arrive, leave, come back and pile up, which only the cell's own order settles.
//
// Each scene is a RECORDED session of that world (stock DD 516.1684, standing still). dreamseeker-verbs.txt is what stock
// DreamSeeker 516.1684 did for each typed line in its own fresh session: the frame, the `<Verb> word` listbox (and the
// frame for a pick), the dialog, what it printed, or the command bar after a space.
#include <QtTest>
#include "tests/recordedinput.h"
#ifndef Q_MOC_RUN
#include "tests/recordedverbs.h"
#endif
#include "tests/testmain.h"
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/bytes.h"
#include "net/input.h"
#include "proto/enc_c2s_0002_input.h"

using namespace fugue;

namespace {
using Ds = testinput::RecordedVerb;

struct Recorded {
    model::Session sess;
    net::InputModel im;
    QString error;
    bool load(const std::string& world = "verbrank-world") {
        const auto path = QString(FUGUE_TEST_DATA_DIR) + '/' + QString::fromStdString(world) + "/frames.txt";
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
    // The app's typed-atom resolver (app/sessionworker.cpp argAtoms).
    net::InputModel::ArgResolver argFor() const {
        return [this](const net::VerbArgSpec& a) -> std::optional<std::vector<net::SrcCandidate>> {
            model::VerbArgSpec ma{a.typeMask, a.filter, a.filterKind};
            if (!model::argResolvesLocally(ma)) return std::nullopt;
            std::vector<net::SrcCandidate> out;
            const model::World& w = sess.world();
            for (const model::VerbSrcCandidate& c : model::verbArgCandidates(w, ma)) {
                bool own = c.src.kind == proto::enc::kVerbSrcMob && c.src.ref == w.selfRef;
                out.push_back(
                    {{c.src.kind, c.src.ref}, c.name, !own, std::nullopt, c.rank.dist, c.rank.cell, c.rank.carried});
            }
            return out;
        };
    }
    std::string selfName() const {
        for (const auto& [id, v] : im.verbs())
            for (const net::SrcCandidate& c : srcFor()(id))
                if (c.src.kind == proto::enc::kVerbSrcMob && c.src.ref == sess.world().selfRef) return c.name;
        return "";
    }
};

bool loadDs(std::vector<Ds>& out, QString& error, const std::string& world = "verbrank-world") {
    return testinput::readRecordedVerbs(
        QString(FUGUE_TEST_DATA_DIR) + '/' + QString::fromStdString(world) + "/dreamseeker-verbs.txt", out, error);
}
}  // namespace

class VerbRank : public QObject {
    Q_OBJECT
    std::vector<Ds> ds_;

private slots:
    void initTestCase() {
        QString error;
        QVERIFY2(loadDs(ds_, error), qPrintable(error));
        QCOMPARE(int(ds_.size()), 82);
    }

    // Every send, listbox and refusal of one world's dreamseeker-verbs.txt, against fugue-qt on the recorded scene.
    static void sameAsDreamSeeker(const std::string& world, const std::vector<Ds>& ds, int wantSends, int wantBoxes,
                                  int wantRefusals = 0) {
        Recorded r;
        QVERIFY2(r.load(world), qPrintable(r.error));
        const model::World& w = r.sess.world();
        std::string self = r.selfName();  // empty in a world with no verb on the own mob
        int sends = 0, boxes = 0, refusals = 0;
        for (const Ds& d : ds) {
            if (d.what == "listbox") {
                net::CommandResult res = r.im.verbCommand(d.line, true, r.srcFor(), r.argFor());
                QVERIFY2(res.srcChoice, qPrintable(QString::fromStdString(d.line + ": no listbox, " + res.error)));
                QCOMPARE(QString::fromStdString(res.srcChoice->title), QString::fromStdString(d.title));
                std::vector<std::string> names, want = d.names;
                for (const net::SrcCandidate& c : res.srcChoice->candidates) names.push_back(c.name);
                for (std::string& n : want)
                    if (n == "self") {
                        QVERIFY(!self.empty());
                        n = self;
                    }
                QVERIFY2(names == want, qPrintable(QString::fromStdString(d.line)));
                boxes++;
                continue;
            }
            if (d.what == "refuses") {
                net::CommandResult res = r.im.verbCommand(d.line, true, r.srcFor(), r.argFor());
                QVERIFY2(res.frames.empty() && !res.missing && !res.srcChoice,
                         qPrintable(QString::fromStdString(d.line)));
                std::string want;
                for (const std::string& l : d.names) want += (want.empty() ? "" : "\n") + l;
                QCOMPARE(QString::fromStdString(res.error), QString::fromStdString(want));
                refusals++;
                continue;
            }
            if (d.what != "send") continue;
            net::CommandResult res = r.im.verbCommand(d.line, true, r.srcFor(), r.argFor());
            if (!d.pick.empty()) {
                QVERIFY2(res.srcChoice, qPrintable(QString::fromStdString(d.line + ": no listbox, " + res.error)));
                std::vector<net::SrcCandidate> picked;
                for (const net::SrcCandidate& c : res.srcChoice->candidates)
                    if (c.name == d.pick) picked.push_back(c);
                QCOMPARE(int(picked.size()), 1);
                res = r.im.verbCommand(res.srcChoice->line, true, [&](uint16_t) { return picked; }, r.argFor());
            }
            QVERIFY2(res.frames.size() == 1, qPrintable(QString::fromStdString(d.line + ": " + res.error)));
            // A client src goes out as ref 0 (stock sent a per-session ref here; the kind is what is graded).
            uint32_t ref = d.ref == "self" ? w.selfRef : d.ref == "client" ? 0 : uint32_t(std::stoul(d.ref));
            Bytes want = proto::enc::encodeVerb(d.verb, d.kind, ref, d.args);
            QVERIFY2(res.frames[0].body == want,
                     qPrintable(QString::fromStdString(d.line + " " + d.pick + ": got " + hexEncode(res.frames[0].body) +
                                                       ", DreamSeeker " + hexEncode(want))));
            sends++;
        }
        QCOMPARE(sends, wantSends);
        QCOMPARE(boxes, wantBoxes);
        QCOMPARE(refusals, wantRefusals);
    }

    void eachTypedLineAsDreamSeeker() { sameAsDreamSeeker("verbrank-world", ds_, 53, 8); }

    // The same pairs numbered the other way round: wherever the line reads differently for the two verbs (a number for
    // one, text for the other), the lower id decides how it is read, so the other side wins now.
    void reversedIdsAsDreamSeeker() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verbrank-reversed-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 17);
        sameAsDreamSeeker("verbrank-reversed-world", ds, 17, 0);
    }

    // The order is the verb ids', not the order the client learnt them: the lamp's Hoot (id 0, learnt last) reads `5` as
    // its number. Atoms of one name are one listbox row, so the first is taken without asking. Every refusing verb prints
    // its own word and usage.
    void verbIdOrderAndOneRowPerName() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verborder-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 8);
        sameAsDreamSeeker("verborder-world", ds, 7, 0, 1);
    }

    // Between srcs the line need not name, the nearer wins (Buzz), measured from usr's footprint to the src's: bounds and
    // step count (Ping, Hiss), pixel_x does not (Purr). At one distance the earlier in its cell's list wins, mobs before
    // objs, in the cell's list order (Ring, Bell, Gong, Clap), else the lower id (Zing); off the map, the earlier carried
    // (Toss, Flip). A word naming several atoms, src or argument, takes the nearest (Tap pad, aim pad), and so does the
    // one `<Verb> word` row of a bare line (Tap); a verb's own implicit src is still its first atom (Hum).
    void nearestSrcAsDreamSeeker() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verbnear-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 15);
        sameAsDreamSeeker("verbnear-world", ds, 15, 0);
    }
    // usr's own bounds widen the footprint distances are measured from: yelp b and pad 2, 1 tile past the footprint's
    // west edge, beat yelp a and pad 1, 2 tiles east of usr's cell. DreamDaemon centres its own view() of this mob two
    // tiles EAST of its cell, so the scene keeps every src inside that view or the server would drop the command.
    void usrFootprintAsDreamSeeker() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verbwide-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 2);
        sameAsDreamSeeker("verbwide-world", ds, 2, 0);
    }

    // The cell's list is refilled by descending id at each relink, then each record moves before its back-link, the
    // atom that entered after it (the server's Bind). Where ids follow arrival that puts the later arrival first (Drum,
    // Horn, Chime, Peal, a re-entry: Knock, Scrape), but not in general: arrivals of refs 2, 0, 1 leave 0 first (Gong),
    // of 6, 3, 5, 4 leave 3 first (Hum). A new atom on a lower, reused ref (Beep); an appearance change or a step within
    // the cell moves nothing (Toll, Rasp). Mobs come first (Woof, Yap), and a full mob relink keeps the mob back-links
    // it had, so a mob that arrived late in a frame that also carried the Binds stays behind the higher id (Bark).
    void cellOrderAsDreamSeeker() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verbcell-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 10);
        sameAsDreamSeeker("verbcell-world", ds, 10, 0);
        QVERIFY2(loadDs(ds, error, "verblate-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 3);
        sameAsDreamSeeker("verblate-world", ds, 3, 0);
        QVERIFY2(loadDs(ds, error, "verbgong-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 3);
        sameAsDreamSeeker("verbgong-world", ds, 3, 0);
    }
    // At 51 records or more a cell is laid out along its chains by a second ordering, which on these ascending-id
    // piles leaves the earliest arrival first (Hoot, 52 on the cell); 49 keep the back-link order (Toot).
    void bigCellOrderAsDreamSeeker() {
        std::vector<Ds> ds;
        QString error;
        QVERIFY2(loadDs(ds, error, "verbpile-world"), qPrintable(error));
        QCOMPARE(int(ds.size()), 2);
        sameAsDreamSeeker("verbpile-world", ds, 2, 0);
    }

    // A bare line that leaves an argument out asks for it, on the verb stock picked.
    void aMissingArgumentAsksTheVerbDreamSeekerPicked() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        int n = 0;
        for (const Ds& d : ds_) {
            if (d.what != "dialog") continue;
            net::CommandResult res = r.im.verbCommand(d.line, true, r.srcFor(), r.argFor());
            QVERIFY2(res.missing, qPrintable(QString::fromStdString(d.line + ": " + res.error)));
            QCOMPARE(QString::fromStdString(net::argPrompt(*res.missing).title), QString::fromStdString(d.title));
            n++;
        }
        QCOMPARE(n, 2);
        // Calc (client num, mob text) and Zap (mob num, client text): stock sent the text verb (41, 23) with a stray
        // text; fugue-qt asks for that verb's text instead.
        for (auto [line, id] : {std::pair<const char*, uint16_t>{"Calc", 41}, {"Zap", 23}}) {
            net::CommandResult res = r.im.verbCommand(line, true, r.srcFor());
            QVERIFY2(res.missing, line);
            QCOMPARE(int(res.missing->verb.id), int(id));
        }
    }

    void spaceCompletesAsDreamSeeker() {
        Recorded r;
        QVERIFY2(r.load(), qPrintable(r.error));
        int n = 0;
        for (const Ds& d : ds_) {
            if (d.what != "completes") continue;
            std::string out;
            bool done = r.im.spaceCompletion(d.line, out);
            if (!done) out = d.line + " ";
            QCOMPARE(QString::fromStdString(out), QString::fromStdString(d.title));
            n++;
        }
        QCOMPARE(n, 19);
    }
};

FUGUE_TEST_GUILESS_MAIN(VerbRank)
#include "verbrank_test.moc"
