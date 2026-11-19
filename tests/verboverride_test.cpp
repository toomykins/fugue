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
#include <fstream>
#include <regex>
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/bytes.h"
#include "net/input.h"
#include "proto/enc_c2s_0002_input.h"

using namespace fugue;
using proto::enc::VerbArg;

namespace {
Bytes unhex(const std::string& h) {
    Bytes b;
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(uint8_t(v(h[i]) << 4 | v(h[i + 1])));
    return b;
}
std::string hex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (uint8_t c : b) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

struct DsSend {
    std::string line, pick;
    uint16_t verb;
    uint8_t kind;
    bool self;
    uint32_t ref;
    std::string text;
    bool hasText;
};

struct Recorded {
    model::Session sess;
    net::InputModel im;
    bool load() {
        std::ifstream f(std::string(FUGUE_TEST_DATA_DIR) + "/verboverride-world/frames.txt");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("0x", 0) != 0) continue;
            uint16_t typ = uint16_t(std::stoul(line.substr(2, 4), nullptr, 16));
            size_t sp = line.find(' ', line.find("len="));
            Bytes body = sp == std::string::npos ? Bytes{} : unhex(line.substr(sp + 1));
            im.onFrame(typ, body);
            sess.ingest(typ, body);
        }
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

void loadDs(std::vector<DsSend>& sends, std::vector<std::string>& listbox) {
    std::ifstream f(std::string(FUGUE_TEST_DATA_DIR) + "/verboverride-world/dreamseeker-verbs.txt");
    std::regex re(R"(^([^\t]+)\tverb=(\d+) src=(\d+):(self|\d+)(?: args=text:(\S+))?)");
    std::regex lb(R"(^Use-It\tlistbox "Use-It word": (\S+))");
    std::string line;
    std::smatch m;
    while (std::getline(f, line)) {
        if (std::regex_search(line, m, lb)) {
            std::string s = m[1];
            for (size_t p = 0, q; p <= s.size(); p = q + 1) {
                q = s.find('|', p);
                if (q == std::string::npos) q = s.size();
                listbox.push_back(s.substr(p, q - p));
            }
            continue;
        }
        if (!std::regex_search(line, m, re)) continue;
        DsSend d;
        std::string typed = m[1];
        size_t arrow = typed.find(" <- ");
        d.line = arrow == std::string::npos ? typed : typed.substr(0, arrow);
        d.pick = arrow == std::string::npos ? "" : typed.substr(arrow + 4);
        d.verb = uint16_t(std::stoul(m[2]));
        d.kind = uint8_t(std::stoul(m[3]));
        d.self = m[4] == "self";
        d.ref = d.self ? 0 : uint32_t(std::stoul(m[4]));
        d.hasText = m[5].matched;
        d.text = m[5];
        sends.push_back(d);
    }
}
}  // namespace

class VerbOverride : public QObject {
    Q_OBJECT
    std::vector<DsSend> sends_;
    std::vector<std::string> listbox_;

private slots:
    void initTestCase() {
        loadDs(sends_, listbox_);
        QCOMPARE(int(sends_.size()), 8);
        QCOMPARE(int(listbox_.size()), 3);
    }

    // Both ids of each overridden name are in the dictionary: Whisper 2 (/mob, which the human does not carry) and 9
    // (/mob/living), Poke 8 and 13. Ids are DreamDaemon's own: this session learned 9 first, as a human.
    void theDictionaryHoldsBothIds() {
        Recorded r;
        QVERIFY(r.load());
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
        QVERIFY(r.load());
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
                QCOMPARE(QString::fromStdString(res.srcChoice->title), QString("Use-It word"));
                res = r.im.verbCommand(res.srcChoice->line, true, [&](uint16_t) { return picked; });
            }
            QVERIFY2(res.frames.size() == 1, qPrintable(QString::fromStdString(d.line + ": " + res.error)));
            std::vector<VerbArg> args;
            if (d.hasText) {
                VerbArg a;
                a.tag = 2;
                a.str = d.text;
                args.push_back(a);
            }
            Bytes want = proto::enc::encodeVerb(d.verb, d.kind, d.self ? w.selfRef : d.ref, args);
            QVERIFY2(hex(res.frames[0].body) == hex(want),
                     qPrintable(QString::fromStdString(d.line + " " + d.pick + ": got " + hex(res.frames[0].body) +
                                                       ", DreamSeeker " + hex(want))));
        }
    }

    // A verb the dictionary knows but nothing carries: DreamSeeker sends nothing and prints nothing.
    void anUncarriedVerbIsDroppedSilently() {
        Recorded r;
        QVERIFY(r.load());
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
        QVERIFY(r.load());
        std::string out;
        QVERIFY(r.im.spaceCompletion("Whis", out));
        QCOMPARE(QString::fromStdString(out), QString("Whisper \""));
    }
};

QTEST_GUILESS_MAIN(VerbOverride)
#include "verboverride_test.moc"
