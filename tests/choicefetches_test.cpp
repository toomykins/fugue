// The choices request in flight (app/choicefetches.h) and the session's path through it, on stock DreamDaemon 516's
// bytes, recorded on the contrast worlds "Fugue Prompt Withdraw" and "Fugue Prompt Order". DreamDaemon reads a 0x0064
// when its DM thread is free and answers with the list of whichever prompt holds the id then, or not at all; the
// 0x0090 echo of a 0x008f sent behind the request comes after that answer. Prompt ids are server-wide, lowest free first.
#include <QtTest>
#include "tests/testmain.h"
#include "app/choicefetches.h"
#include "model/prompt.h"
#include <deque>

using namespace fugue;
using namespace std::chrono_literals;
using Step = PromptFlow::Step;

static QByteArray raw(const char* hexBytes) { return QByteArray::fromHex(hexBytes); }
static ByteSpan span(const char* hexBytes) {
    static std::deque<QByteArray> keep;  // the spans outlive each call
    keep.push_back(raw(hexBytes));
    return ByteSpan(reinterpret_cast<const uint8_t*>(keep.back().constData()), size_t(keep.back().size()));
}
static QByteArray hex(const Bytes& b) {
    return QByteArray(reinterpret_cast<const char*>(b.data()), int(b.size())).toHex();
}

// s2c frames as stock DreamDaemon sent them
constexpr const char* kListA = "4c4100004c69737420410080100000004000000000";   // input "List A" in list, id 0
constexpr const char* kListB = "4c4200004c69737420420080100000004000000000";   // input "List B" in list, id 0
constexpr const char* kListC1 = "4c4300004c69737420430080100000004001000000";  // input "List C" in list, id 1
constexpr const char* kWithdraw0 = "0000000000";                               // 0x009d input id 0
constexpr const char* kAlpha = "0200416c706861004170657800";
constexpr const char* kBravo = "0200427261766f00426f6e657300";
constexpr const char* kCharlie = "0300436861726c696500436f62616c7400437261746500";
constexpr const char *kEcho1 = "640001000000", *kEcho2 = "640002000000";  // 0x0090: our 0x008f probes, echoed verbatim

static std::vector<Step> sends(const std::vector<Step>& v) {
    std::vector<Step> o;
    for (const Step& s : v)
        if (s.kind == Step::Send) o.push_back(s);
    return o;
}
static std::vector<std::string> shows(const std::vector<Step>& v) {
    std::vector<std::string> o;
    for (const Step& s : v)
        if (s.kind == Step::Show) o.push_back(s.prompt->message);
    return o;
}
// The one 0x0064 for `id` and its probe.
static bool asked(const std::vector<Step>& v, uint32_t id, const char* probe) {
    std::vector<Step> s = sends(v);
    return s.size() == 2 && s[0].type == C2S::InputExpansion && s[0].id == id &&
           hex(s[0].body) == hex(model::encodePendingInputExpansionRequest(id)) && s[1].type == C2S::Probe &&
           hex(s[1].body) == QByteArray(probe);
}

class ChoiceFetchesTest : public QObject {
    Q_OBJECT
private slots:
    void inputWaitsForItsChoices() {
        // DreamDaemon's DM thread busy for 3 s: the 0x003f, then the echo, 3.1 s after the 0x0062. No dialog until then.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        QCOMPARE(f.onFrame(S2C::Input, span(kListA)).kind, Step::None);
        auto out = f.pump(t0);
        QVERIFY(asked(out, 0, "640001000000"));
        QVERIFY(shows(out).empty());
        QVERIFY(f.pump(t0 + 20s).empty());
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        out = f.pump(t0 + 3100ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List A"}));
        QCOMPARE(out[0].prompt->choices, (std::vector<std::string>{"Alpha", "Apex"}));
        QVERIFY(!out[0].gaveUp);
        QVERIFY(sends(out).empty());
    }

    void requestNamesThePromptBehindAnotherPlayers() {
        // Another player's text input() holds id 0, so this player's in-list input() is id 1. Stock DreamSeeker asked
        // `00 01000000 00000000`.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span("4200005365636f6e64206c6973740080100000004001000000"));
        auto out = f.pump(t0);
        QVERIFY(asked(out, 1, "640001000000"));
        QCOMPARE(hex(sends(out)[0].body), QByteArray("000100000000000000"));
        f.onFrame(S2C::ExpansionReply, span("02005a756c750059616e6b656500"));
        out = f.pump(t0);
        QCOMPARE(out.size(), size_t(1));
        QCOMPARE(out[0].prompt->choices, (std::vector<std::string>{"Zulu", "Yankee"}));
    }

    void reuseInOneTickAsksOnlyForTheLivePrompt() {
        // One tick: in-list A gets id 0 and is withdrawn, B reuses id 0, C gets id 1, all four frames in one read. A
        // client that asked as each 0x0062 arrived got three replies (B's, B's, C's) for A, B and C; this asks once, for
        // B, and for C only once B is answered.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        QCOMPARE(f.onFrame(S2C::PromptWithdraw, span(kWithdraw0)).kind, Step::Withdraw);
        f.onFrame(S2C::Input, span(kListB));
        f.onFrame(S2C::Input, span(kListC1));
        auto out = f.pump(t0);
        QVERIFY(asked(out, 0, "640001000000"));
        QVERIFY(shows(out).empty());
        QCOMPARE(f.onFrame(S2C::ExpansionReply, span(kBravo)).kind, Step::None);
        out = f.pump(t0 + 100ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List B"}));
        QCOMPARE(out[0].prompt->choices, (std::vector<std::string>{"Bravo", "Bones"}));
        QVERIFY(sends(out).empty());  // C waits for B's answer
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        QVERIFY(f.pump(t0 + 100ms).empty());
        QCOMPARE(f.book.answerInput(0, "Bravo").type, uint16_t(C2S::InputResolve));
        out = f.pump(t0 + 2s);
        QVERIFY(asked(out, 1, "640002000000"));
        f.onFrame(S2C::ExpansionReply, span(kCharlie));
        out = f.pump(t0 + 2s);
        QCOMPARE(shows(out), (std::vector<std::string>{"List C"}));
        QCOMPARE(out[0].prompt->choices, (std::vector<std::string>{"Charlie", "Cobalt", "Crate"}));
    }

    void sameTickWithdrawalSendsNothing() {
        // The proc dies in the tick it asks: the 0x0062 and its 0x009d arrive in one read, and nothing is asked.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.onFrame(S2C::PromptWithdraw, span(kWithdraw0));
        QVERIFY(f.pump(t0).empty());
        QVERIFY(f.book.pending().empty());
        QVERIFY(!f.fetches.slot);
    }

    void withdrawnAfterTheRequestLeft() {
        // The 0x0064 left before the 0x009d arrived, and DreamDaemon read it with id 0 free: no 0x003f, only the echo.
        // The slot frees on the echo, and the next prompt at id 0 is asked for with its own request.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        QVERIFY(asked(f.pump(t0), 0, "640001000000"));
        f.onFrame(S2C::PromptWithdraw, span(kWithdraw0));
        QVERIFY(f.pump(t0 + 100ms).empty());
        QVERIFY(f.fetches.slot);
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        QVERIFY(!f.fetches.slot);
        QVERIFY(f.pump(t0 + 500ms).empty());
        f.onFrame(S2C::Input, span(kListB));
        QVERIFY(asked(f.pump(t0 + 4s), 0, "640002000000"));
        f.onFrame(S2C::ExpansionReply, span(kBravo));
        QCOMPARE(shows(f.pump(t0 + 4s)), (std::vector<std::string>{"List B"}));
    }

    void withdrawnAfterTheRequestLeftAndReused() {
        // As above, but B took id 0 and C id 1 before DreamDaemon read the request: it answered with B's list. That reply
        // is B's, and C waits for its own.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        QVERIFY(asked(f.pump(t0), 0, "640001000000"));
        f.onFrame(S2C::PromptWithdraw, span(kWithdraw0));
        f.onFrame(S2C::Input, span(kListB));
        f.onFrame(S2C::Input, span(kListC1));
        QVERIFY(f.pump(t0 + 100ms).empty());  // one request at a time
        f.onFrame(S2C::ExpansionReply, span(kBravo));
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        auto out = f.pump(t0 + 500ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List B"}));
        QCOMPARE(out[0].prompt->choices, (std::vector<std::string>{"Bravo", "Bones"}));
        QVERIFY(sends(out).empty());
        QCOMPARE(f.book.find(1)->choicesKnown, false);
    }

    void answeredBeforeItsWithdrawal() {
        // The DM thread busy while the proc was killed: DreamDaemon answered the queued request with A's list before
        // the 0x009d. A shows complete, then closes.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        QCOMPARE(f.onFrame(S2C::PromptWithdraw, span(kWithdraw0)).kind, Step::Withdraw);
        QVERIFY(f.pump(t0 + 3s).empty());  // withdrawn in the same read: never shown
        QVERIFY(!f.fetches.slot);
    }

    void anotherPlayersListIsDropped() {
        // A withdrawn and id 0 taken by another player's in-list prompt before the request was read: the reply is that
        // player's list, and no prompt of ours holds id 0.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        f.onFrame(S2C::PromptWithdraw, span(kWithdraw0));
        QCOMPARE(f.onFrame(S2C::ExpansionReply, span(kBravo)).kind, Step::Dropped);
        QVERIFY(!f.fetches.slot);
        f.onFrame(S2C::ProbeEcho, span(kEcho1));  // already answered
        f.onFrame(S2C::Input, span(kListC1));
        QVERIFY(asked(f.pump(t0 + 1s), 1, "640002000000"));
    }

    void echoWithNoReplyForALivePromptShowsTypedText() {
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        f.onFrame(S2C::ProbeEcho, span(kEcho1));
        auto out = f.pump(t0 + 100ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List A"}));
        QVERIFY(out[0].gaveUp);
        QVERIFY(out[0].prompt->choices.empty());
        QVERIFY(sends(out).empty());  // not asked again
    }

    void serverThatNeverEchoes() {
        // A reply alone frees the slot; a request with neither reply nor echo is given up after kNoEchoKeep.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        QCOMPARE(shows(f.pump(t0)).size(), size_t(1));
        QCOMPARE(f.book.answerInput(0, "Alpha").type, uint16_t(C2S::InputResolve));
        f.onFrame(S2C::Input, span(kListB));
        QVERIFY(asked(f.pump(t0 + 1s), 0, "640002000000"));
        QVERIFY(f.pump(t0 + 1s + ChoiceFetches::kNoEchoKeep).empty());
        auto out = f.pump(t0 + 1s + ChoiceFetches::kNoEchoKeep + 1ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List B"}));
        QVERIFY(out[0].gaveUp);
        // Once a server has echoed, the wait has no bound.
        PromptFlow g;
        g.fetches.echoSeen = true;
        g.onFrame(S2C::Input, span(kListA));
        g.pump(t0);
        QVERIFY(g.pump(t0 + 10 * ChoiceFetches::kNoEchoKeep).empty());
        QVERIFY(g.fetches.slot);
    }

    void verbArgumentsWaitForTheSlot() {
        // A verb argument asked while an input()'s request is out goes after its reply, and owns the next 0x003f.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        QVERIFY(asked(f.pump(t0), 0, "640001000000"));
        f.askVerbArg(7, C2S::VerbExpansion, Bytes{0x01, 0x02}, t0 + 10ms);
        QVERIFY(f.pump(t0 + 20ms).empty());
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        auto out = f.pump(t0 + 100ms);
        QCOMPARE(shows(out), (std::vector<std::string>{"List A"}));
        auto s = sends(out);
        QCOMPARE(s.size(), size_t(2));
        QVERIFY(s[0].type == C2S::VerbExpansion && s[0].id == 7 && hex(s[0].body) == "0102");
        QCOMPARE(hex(s[1].body), QByteArray("640002000000"));
        f.onFrame(S2C::ProbeEcho, span(kEcho1));  // A's, late: ignored
        Step r = f.onFrame(S2C::ExpansionReply, span(kBravo));
        QCOMPARE(r.kind, Step::VerbArgChoices);
        QCOMPARE(r.id, 7u);
    }

    void verbArgumentGivesUpOnceAndItsLateReplyIsSwallowed() {
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.askVerbArg(7, C2S::VerbExpansion, Bytes{0x01}, t0);
        QCOMPARE(sends(f.pump(t0)).size(), size_t(2));
        QVERIFY(f.pump(t0 + ChoiceFetches::kWait).empty());
        auto out = f.pump(t0 + ChoiceFetches::kWait + 1ms);
        QCOMPARE(out.size(), size_t(1));
        QVERIFY(out[0].kind == Step::VerbArgGaveUp && out[0].id == 7);
        QVERIFY(f.pump(t0 + ChoiceFetches::kWait + 2ms).empty());
        Step r = f.onFrame(S2C::ExpansionReply, span(kBravo));
        QVERIFY(r.kind == Step::Dropped && r.abandoned);
        // A verb argument still waiting for the slot when its time is up opens with typed text and is never sent.
        PromptFlow g;
        g.onFrame(S2C::Input, span(kListA));
        g.pump(t0);
        g.askVerbArg(8, C2S::VerbExpansion, Bytes{0x01}, t0);
        out = g.pump(t0 + ChoiceFetches::kWait + 1ms);
        QCOMPARE(out.size(), size_t(1));
        QVERIFY(out[0].kind == Step::VerbArgGaveUp && out[0].id == 8);
        g.onFrame(S2C::ExpansionReply, span(kAlpha));
        QVERIFY(sends(g.pump(t0 + 5s)).empty());
    }

    void laterPromptsWaitBehindAListPrompt() {
        // Stock shows prompts one at a time in arrival order and asks for an in-list prompt's choices when it shows it.
        // A text input() at id 0 shows; the in-list one at id 1 is not asked for until it is answered.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span("5443310000546578742043310084000000000000000000"));
        f.onFrame(S2C::Input, span("4c433200004c6973742043320080100000004001000000"));
        f.onFrame(S2C::Alert, span("02000000416c6572742044004144000300596573004e6f0000"));
        auto out = f.pump(t0);
        QCOMPARE(shows(out), (std::vector<std::string>{"Text C1"}));
        QVERIFY(sends(out).empty());
        QCOMPARE(f.book.answerInput(0, "x").type, uint16_t(C2S::Input));
        out = f.pump(t0 + 1s);
        QVERIFY(asked(out, 1, "640001000000"));
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        QCOMPARE(shows(f.pump(t0 + 1s)), (std::vector<std::string>{"List C2", "Alert D"}));
    }

    void withdrawnIdIsRefusedUntilReused() {
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        f.onFrame(S2C::ExpansionReply, span(kAlpha));
        f.pump(t0);
        f.onFrame(S2C::PromptWithdraw, span(kWithdraw0));
        QVERIFY(f.book.withdrawn(0, false));
        QCOMPARE(f.book.answerInput(0, "Alpha").type,
                 uint16_t(0));  // a dialog answered as the 0x009d lands sends nothing
        QCOMPARE(f.book.cancel(0).type, uint16_t(0));
        f.onFrame(S2C::Input, span(kListB));  // the server asks with id 0 again
        QVERIFY(!f.book.withdrawn(0, false));
        f.pump(t0 + 4s);
        f.onFrame(S2C::ExpansionReply, span(kBravo));
        QCOMPARE(f.book.answerInput(0, "Bravo").type, uint16_t(C2S::InputResolve));
    }

    void aLiveIdAskedAgainReplacesThePrompt() {
        // The server holds an id for one prompt: a second 0x0062 at id 0 means the first is gone.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span(kListA));
        f.pump(t0);
        Step b = f.onFrame(S2C::Input, span(kListB));
        QVERIFY(b.replaced);
        QCOMPARE(f.book.pending().size(), size_t(1));
        f.onFrame(S2C::ExpansionReply, span(kBravo));
        auto out = f.pump(t0 + 1s);
        QCOMPARE(shows(out), (std::vector<std::string>{"List B"}));
    }

    void withdrawalNamesTheKindAndTheId() {
        // A text input() at id 0 with an in-list one queued at id 1; then an alert() at id 0.
        PromptFlow f;
        const auto t0 = PromptFlow::Clock::now();
        f.onFrame(S2C::Input, span("5443310000546578742043310084000000000000000000"));
        f.onFrame(S2C::Input, span("4c433200004c6973742043320080100000004001000000"));
        QCOMPARE(shows(f.pump(t0)).size(), size_t(1));
        Step w = f.onFrame(S2C::PromptWithdraw, span("0100000000"));
        QVERIFY(w.kind == Step::Withdraw && w.id == 1 && !w.alert);
        QVERIFY(f.book.find(0, false));
        f.onFrame(S2C::Alert, span("00000000416c6572742044004144000300596573004e6f0000"));
        QCOMPARE(shows(f.pump(t0 + 4s)).size(), size_t(1));
        w = f.onFrame(S2C::PromptWithdraw, span("0000000001"));
        QVERIFY(w.kind == Step::Withdraw && w.id == 0 && w.alert);
        QVERIFY(!f.book.find(0, true));
        QVERIFY(f.book.find(0, false));  // the input() at id 0 is not the alert()
        QCOMPARE(f.book.answerAlert(0, "Yes").type, uint16_t(0));
        QCOMPARE(f.book.answerInput(0, "x").type, uint16_t(C2S::Input));
        QCOMPARE(f.onFrame(S2C::PromptWithdraw, span("0000000003")).kind, Step::None);  // not a prompt
        QVERIFY(sends(f.pump(t0 + 6s)).empty());
    }
};

FUGUE_TEST_GUILESS_MAIN(ChoiceFetchesTest)
#include "choicefetches_test.moc"
