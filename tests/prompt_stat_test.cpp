// Prompt and stat-panel wire rules: model/prompt (alert()/input() requests and their exact reply bytes) and model/statpanel
// (tab strip, row text, token gate, click and tab-select encodings). Cancel, row-click and tab-body bytes match stock
// DreamSeeker on the wire; the request frames are the ones stock DreamDaemon emits for alert() and input().
#include <QtTest>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "model/prompt.h"
#include "model/statpanel.h"
#include "model/verbmenu.h"
#include "model/world.h"
#include "net/input.h"
#include "net/textmacro.h"

using namespace fugue;
using namespace fugue::model;

namespace {
const std::string kImp = "\xff\x16";   // `\improper`
Bytes hx(const char* s) { auto b = hexDecode(s); if (!b) qFatal("bad hex %s", s); return *b; }
std::string hs(const Bytes& b) { return hexEncode(b); }
// Stock DreamDaemon's 0x003f choice lists.
constexpr const char* kStockChoicesB =
    "0c0061614150430061226241504300635c6441504300785b7941504300700a71415043007009714150430041504300626241504300"
    "ff165a6564415043004d6978656441504300752e7641504300772741504300";
constexpr const char* kStockChoicesC =
    "0600616141504300635c6441504300700a7141504300655c415043005c5c664150430067226841504300";
constexpr const char* kStockChoicesD =
    "10005c5c66415043005c5c5c66415043005c2771415043005c227141504300782771415043005c5c2771415043005b4150435d00"
    "785c5c79415043000a415043004150435c002073415043005c5c6e41504300785c6e41504300ff16496d415043002d415043005c5c5b41504300";
constexpr const char* kStockChoicesE = "050070097141504300700d7141504300700d0a714150430009415043004150430900";
constexpr const char* kStockChoicesF = "030061415043005c5c006241504300";   // aAPC, a lone backslash, bAPC; DD leaves out an empty choice
std::vector<std::string> stockChoices(const char* hex) {
    std::vector<std::string> ch;
    if (!parseExpansionReply(hx(hex), ch)) qFatal("bad 0x003f %s", hex);
    return ch;
}
void cs(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
Bytes inputReq(const std::string& title, const std::string& def, const std::string& msg, uint32_t mask, uint8_t filter, uint8_t fk, uint32_t id) {
    Bytes b; cs(b, title); cs(b, def); cs(b, msg); putLe32(b, mask); b.push_back(filter); b.push_back(fk); putLe32(b, id); return b;
}
Bytes alertReq(uint32_t id, const std::string& msg, const std::string& title, std::vector<std::string> btns) {
    Bytes b; putLe32(b, id); cs(b, msg); cs(b, title); putLe16(b, uint16_t(btns.size())); for (auto& s : btns) cs(b, s); return b;
}
std::string verbJson(int id, const char* name, const char* cat, int flags = 4, unsigned ext = 0) {
    return "{\"id\":" + std::to_string(id) + ",\"name\":\"" + name + "\",\"category\":\"" + cat + "\",\"head\":255,\"src\":0,\"flags\":" +
           std::to_string(flags) + ",\"flagsExt\":" + std::to_string(ext) + ",\"desc\":\"\",\"args\":[]}";
}
// a 0x006a frame: tabs (optional), selected (optional), rows of (name, text, ordinal), clears
Bytes statFrame(uint8_t token, const std::vector<std::string>* tabs, int selected, const std::vector<std::tuple<std::string, std::string, int>>& rows,
                const std::vector<int>& clears = {}) {
    Bytes b;
    uint8_t flags = (tabs ? 0x02 : 0) | (selected >= 0 ? 0x04 : 0);
    b.push_back(flags); b.push_back(token);
    if (tabs) { putLe16(b, uint16_t(tabs->size())); for (auto& t : *tabs) cs(b, t); }
    if (selected >= 0) putLe16(b, uint16_t(selected));
    putLe16(b, uint16_t(rows.size()));
    for (auto& [n, v, o] : rows) { cs(b, n); b.push_back(0x06); cs(b, v); putLe16(b, uint16_t(o)); }
    putLe16(b, uint16_t(clears.size()));
    for (int c : clears) putLe16(b, uint16_t(c));
    return b;
}
}

class PromptStatTest : public QObject {
    Q_OBJECT
private slots:
    // ---- replies: byte-exact (alert / input reply encodings) ----
    void alertReplyBytes() {
        QCOMPARE(QString::fromStdString(hs(encodeAlertReply(5, "No"))), QString("050000004e6f00"));
        QCOMPARE(QString::fromStdString(hs(encodeAlertReply(0, ""))), QString("0000000000"));  // an empty answer: alert has no null
    }
    void inputReplyBytes() {
        QCOMPARE(QString::fromStdString(hs(encodeInputText(7, "hi"))), QString("0268690007000000"));  // kind 2 [cstring][id]
        // Text answers are DM-escaped, sent byte for byte as stock DreamSeeker sends them.
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a\\b"))), QString("02615c5c62" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a\\\\b"))), QString("02615c5c5c5c62" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a\\\"b"))), QString("02615c5c5c2262" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "\"a\\b"))), QString("025c22615c5c62" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a\\"))), QString("02615c5c" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a'b"))), QString("02615c2762" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "a[b]\\n"))), QString("02615b625d5c5c6e" "00" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputNum(9, 42.f))), QString("0100002842" "09000000"));  // kind 1 [f32 bits][id]
        QCOMPARE(QString::fromStdString(hs(encodeInputNull(3))), QString("00" "00" "ffff0000" "03000000"));  // tag 0, the null ref, [id]
        // A kind byte above 4 would desync the stream; no encoder may emit one.
        for (auto& b : {encodeInputText(1, ""), encodeInputNum(1, 0), encodeInputNull(1)}) QVERIFY(b[0] <= 4);
    }
    void asNumSendsANumberOnlyWhenItParses() {
        Prompt p; p.kind = Prompt::Input; p.id = 4; p.typeMask = kInputNum;
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, "42"))), QString("0100002842" "04000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, "  42 "))), QString("0100002842" "04000000"));  // surrounding spaces trimmed
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, "7"))), QString("01" "0000e040" "04000000"));
        // not a number: sent as text, never 0 (0 would be a real answer)
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, "abc"))), QString("02" "61626300" "04000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, ""))), QString("02" "00" "04000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(p, "1e40"))), QString("02" "31653430" "00" "04000000"));  // beyond float32: out of range
        Prompt t; t.id = 4; t.typeMask = kInputText;  // `as text` with digits stays a string
        QCOMPARE(QString::fromStdString(hs(encodeInputAnswer(t, "42"))), QString("02" "343200" "04000000"));
    }
    // ---- requests: the frames stock DD emitted ----
    void alertRequestDropsEmptySlots() {
        Prompt p;
        QVERIFY(parseAlertRequest(alertReq(0, "Pick one", "Fugue", {"Yes", "No", "Maybe"}), p));
        QCOMPARE(p.buttons, (std::vector<std::string>{"Yes", "No", "Maybe"}));
        QCOMPARE(p.message, std::string("Pick one"));
        QVERIFY(parseAlertRequest(alertReq(0, "Pick one", "Fugue", {"Yes", "", ""}), p));
        QCOMPARE(p.buttons, (std::vector<std::string>{"Yes"}));  // an empty slot names no button
        QVERIFY(parseAlertRequest(alertReq(1, "m", "t", {"", "", ""}), p));
        QCOMPARE(p.buttons, (std::vector<std::string>{"OK"}));  // a one-argument alert() draws OK
        Bytes bad = alertReq(1, "m", "t", {"a"}); bad.push_back(0);
        QVERIFY(!parseAlertRequest(bad, p));  // trailing bytes: malformed, so unanswered
    }
    void inputRequestForms() {
        struct F { uint32_t mask; uint8_t fk; const char* type; bool multi, num, color, null, list; };
        const F forms[] = {
            {0x0004, 0x00, "text", false, false, false, false, false},
            {0x0008, 0x00, "num", false, true, false, false, false},
            {0x0000, 0x40, "anything", false, false, false, false, true},
            {0x1000, 0x40, "anything", false, false, false, false, true},
            {0x0800, 0x00, "message", true, false, false, false, false},
            {0x0084, 0x00, "text", false, false, false, true, false},
            {0x020000, 0x00, "color", false, false, true, false, false},
        };
        for (const F& f : forms) {
            Prompt p;
            QVERIFY(parseInputRequest(inputReq("Fugue", "seed", "Pick one", f.mask, 0, f.fk, 0), p));
            QCOMPARE(p.typeName(), std::string(f.type));
            QCOMPARE(p.multiline(), f.multi); QCOMPARE(p.numeric(), f.num); QCOMPARE(p.isColor(), f.color);
            QCOMPARE(p.nullable(), f.null); QCOMPARE(p.inList, f.list);
            QCOMPARE(p.title, std::string("Fugue")); QCOMPARE(p.message, std::string("Pick one"));
            QCOMPARE(p.def, std::string(f.mask == kInputNum ? "" : "seed"));   // `as num` shows no non-number default
        }
        Prompt p;
        QVERIFY(parseInputRequest(inputReq("t", "c\\\\dAPC", "m", kInputText, 0, kInputFilterList, 0), p));
        QVERIFY(!p.inList); QCOMPARE(p.def, std::string("c\\dAPC"));   // `as text in L`: stock shows the default in a text field
        QVERIFY(!parseInputRequest(hx("00"), p));
        Bytes shortTail = inputReq("a", "b", "c", 4, 0, 0, 1); shortTail.pop_back();
        QVERIFY(!parseInputRequest(shortTail, p));  // the tail is exactly 10 bytes
    }
    // Each row: the 0x0062 default stock DreamDaemon sent, the text stock DreamSeeker showed, and the c2s 0x0062 text it
    // sent back on OK unchanged.
    void inputDefaultsShowAndReturnAsStock() {
        struct C { std::string sent, shown, back; uint32_t mask; };
        const C cases[] = {
            {"\\improper APC Arrival Area", "mproper APC Arrival Area", "mproper APC Arrival Area", 0x4},
            {"aAPC\\nb", "aAPC\nb", "aAPC\\nb", 0x4},
            {"aAPC\\tb", "aAPC\tb", "aAPC\\tb", 0x4},
            {"q\\\"APC\\\\z", "q\"APC\\z", "q\\\"APC\\\\z", 0x84},
            {"\\[APC] x", "[APC] x", "[APC] x", 0x4},
            {"\\red APC z", "ed APC z", "ed APC z", 0x4},
            {"x\\bold APC \\bold z", "xold APC old z", "xold APC old z", 0x4},
            {"\\proper APC y", "roper APC y", "roper APC y", 0x4},
            {"'s' APC", "'s' APC", "\\'s\\' APC", 0x4},
            {"x\\green APC \\green\\green z", "xreen APC reenreen z", "xreen APC reenreen z", 0x4},
            {"APC q\\... \\color(255,0,10)APC \\color z", "APC q.. olor(255,0,10)APC olor z", "APC q.. olor(255,0,10)APC olor z", 0x4},
            {"\\\\n APC \\\\t \\\\\\\\ z", "\\n APC \\t \\\\ z", "\\\\n APC \\\\t \\\\\\\\ z", 0x4},
            {"4th w iv <IMG CLASS=icon SRC=\\[0xc000001] ICONSTATE='lobby'> z", "4th w iv <IMG CLASS=icon SRC=[0xc000001] ICONSTATE='lobby'> z",
             "4th w iv <IMG CLASS=icon SRC=[0xc000001] ICONSTATE=\\'lobby\\'> z", 0x4},
            {"p\r\\nqAPC", "p\r\nqAPC", "p\\nqAPC", 0x4},
            {"aAPC\\nb\\tc\\\\d\\\"e\\[f]", "aAPC\nb\tc\\d\"e[f]", "aAPC\\nb\\tc\\\\d\\\"e[f]", 0x800},
            {"\\improper APC Arr\\n2", "mproper APC Arr\n2", "mproper APC Arr\\n2", 0x880},
        };
        for (const C& c : cases) {
            PromptBook b;
            const Prompt* p = b.onFrame(kInputMsg, inputReq("t", c.sent, "m", c.mask, 0, 0, 6));
            QVERIFY(p);
            QCOMPARE(p->def, c.shown);
            std::string shown = p->def;   // answering retires the prompt
            Bytes want{2}; cs(want, c.back); putLe32(want, 6);
            QCOMPARE(QString::fromStdString(hs(b.answerInput(6, shown).body)), QString::fromStdString(hs(want)));
        }
        // edited: the shown text plus what was typed, a typed line break included
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "aAPC\nb\tc\\d\"e[f]Z\\\r\nY"))),
                 QString::fromStdString(hs([] { Bytes w{2}; cs(w, "aAPC\\nb\\tc\\\\d\\\"e[f]Z\\\\\\nY"); putLe32(w, 0); return w; }())));
    }
    void numericDefaultShownOnlyWhenNumberShaped() {
        for (const char* d : {"1.5", "7.", "-2.5", "-", ".5", "00012", "10000000", "0.0001", ""}) {
            Prompt p;
            QVERIFY(parseInputRequest(inputReq("t", d, "m", kInputNum, 0, 0, 1), p));
            QCOMPARE(p.def, std::string(d));
        }
        for (const char* d : {"5x", " 6", "1e3", "0x10", "8 ", "1-2", "..", "+3", "1.2.3", "3-", "\\[3]APC"}) {
            Prompt p;
            QVERIFY(parseInputRequest(inputReq("t", d, "m", kInputNum, 0, 0, 1), p));
            QCOMPARE(p.def, std::string());
            QVERIFY(parseInputRequest(inputReq("t", d, "m", kInputText, 0, 0, 1), p));
            QVERIFY(!p.def.empty());
        }
    }
    void expansionChoicesShowAsStock() {
        QCOMPARE(stockChoices(kStockChoicesD),
                 (std::vector<std::string>{"\\fAPC", "\\\\fAPC", "'qAPC", "\"qAPC", "x'qAPC", "\\'qAPC", "[APC]", "x\\\\yAPC", "\nAPC",
                                           "APC\\", " sAPC", "\\nAPC", "x\\nAPC", "ImAPC", "-APC", "\\[APC"}));
        QCOMPARE(stockChoices(kStockChoicesC), (std::vector<std::string>{"aaAPC", "c\\dAPC", "p\nqAPC", "e\\APC", "\\fAPC", "g\"hAPC"}));
    }
    // Picking each choice: the c2s 0x0063 text for input(), and the c2s 0x0039 text for a verb argument.
    void expansionChoicePicksSendAsStock() {
        const std::vector<std::string> ch = stockChoices(kStockChoicesD);
        const char* resolve[] = {"\\\\fAPC ", "\\\\\\\\fAPC ", "\\'qAPC ", "\\\"qAPC ", "x\\'qAPC ", "\\\\\\'qAPC ", "[APC] ",
                                 "x\\\\\\\\yAPC ", "\\nAPC ", "APC\\\\ ", "-sAPC ", "\\\\nAPC ", "x\\\\nAPC ", "ImAPC ", "-APC ", "\\\\[APC "};
        const char* verb[] = {"\\\\fAPC", "\\\\\\\\fAPC", "\\'qAPC", "\\\"qAPC", "x\\'qAPC", "\\\\\\'qAPC", "[APC]", "x\\\\\\\\yAPC",
                              "\\nAPC", "APC\\\\", "-sAPC", "\\\\nAPC", "x\\\\nAPC", "ImAPC", "-APC", "\\\\[APC"};
        QCOMPARE(int(ch.size()), 16);
        for (size_t i = 0; i < ch.size(); i++) {
            QCOMPARE(inputChoiceText(ch[i]), std::string(resolve[i]));
            QCOMPARE(net::listPickText(ch[i]), std::string(verb[i]));
        }
        const std::vector<std::string> ce = stockChoices(kStockChoicesE);
        const char* resolveE[] = {"p\\tqAPC ", "pqAPC ", "p\\nqAPC ", "\\tAPC ", "APC\\t "};
        for (size_t i = 0; i < ce.size(); i++) QCOMPARE(inputChoiceText(ce[i]), std::string(resolveE[i]));
        QCOMPARE(net::listPickText(ce[0]), std::string("p\\tqAPC"));
        QCOMPARE(net::listPickText(ce[1]), std::string("pqAPC"));
    }
    // Which choice stock DreamSeeker selected for each 0x0062 default (the row it answered with; 0 when none matched).
    void inListDefaultSelectsAsStock() {
        auto pick = [](const std::vector<std::string>& choices, const std::string& sent) {
            std::string def = inputDefaultText(sent);
            for (size_t i = 0; i < choices.size(); i++) if (inputChoiceMatches(choices[i], def)) return int(i);
            return 0;
        };
        const std::pair<const char*, int> rb[] = {{"a\\\"bAPC", 1}, {"c\\\\dAPC", 0}, {"x\\[yAPC", 3}, {"p\\nqAPC", 0}, {"p\\tqAPC", 5},
                                                  {"APC", 6}, {"bbAPC", 7}, {"\\improper ZedAPC", 0}, {"MixedAPC", 9}, {"u.vAPC", 10},
                                                  {"w'APC", 11}, {"mixedAPC", 9}, {"Mix", 0}, {"ZedAPC", 8}, {"mproper ZedAPC", 0},
                                                  {"c\\\\\\\\dAPC", 0}, {"zzAPC", 0}};
        for (auto& [sent, row] : rb) QCOMPARE(pick(stockChoices(kStockChoicesB), sent), row);
        const std::pair<const char*, int> rc[] = {{"c\\\\dAPC", 0}, {"cAPC", 0}, {"cdAPC", 1}, {"dAPC", 0}, {"pqAPC", 0}, {"p qAPC", 0},
                                                  {"p\r\\nqAPC", 0}, {"p\rqAPC", 0}, {"eAPC", 3}, {"fAPC", 4}, {"\\\\\\\\fAPC", 0},
                                                  {"g\\\"hAPC", 5}, {"ghAPC", 0}};
        for (auto& [sent, row] : rc) QCOMPARE(pick(stockChoices(kStockChoicesC), sent), row);
        const std::pair<const char*, int> rd[] = {{"fAPC", 0}, {"\\\\fAPC", 1}, {"qAPC", 0}, {"'qAPC", 2}, {"\\\"qAPC", 3}, {"x'qAPC", 4},
                                                  {"\\\\'qAPC", 0}, {"\\[APC]", 6}, {"x\\\\yAPC", 7}, {"APC", 9}, {" sAPC", 10}, {"sAPC", 0},
                                                  {"nAPC", 11}, {"xnAPC", 12}, {"ImAPC", 13}, {"-APC", 14}, {"\\[APC", 15}};
        for (auto& [sent, row] : rd) QCOMPARE(pick(stockChoices(kStockChoicesD), sent), row);
        QCOMPARE(stockChoices(kStockChoicesF), (std::vector<std::string>{"aAPC", "\\", "bAPC"}));
        for (const char* sent : {"", "\\\\"}) QCOMPARE(pick(stockChoices(kStockChoicesF), sent), 0);
    }
    // ---- the book: pend, answer once, cancel semantics, the 0x0064/0x003f choices ----
    void bookAnswersExactlyOnce() {
        PromptBook b;
        const Prompt* a = b.onFrame(kAlertMsg, alertReq(11, "m", "t", {"Yes", "No"}));
        QVERIFY(a); QCOMPARE(b.pending().size(), size_t(1));
        const Prompt* in = b.onFrame(kInputMsg, inputReq("t", "7", "m", kInputNum, 0, 0, 12));
        QVERIFY(in); QCOMPARE(b.pending().size(), size_t(2));
        PromptReply r = b.answerAlert(11, "No");
        QCOMPARE(r.type, kAlertMsg); QCOMPARE(QString::fromStdString(hs(r.body)), QString("0b0000004e6f00"));
        QCOMPARE(b.pending().size(), size_t(1));
        r = b.answerInput(12, "42");
        QCOMPARE(r.type, kInputMsg); QCOMPARE(QString::fromStdString(hs(r.body)), QString("0100002842" "0c000000"));
        QVERIFY(b.pending().empty());
        // an unknown id still answers: a late reply must reach a parked proc
        r = b.answerInput(99, "x");
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("02" "7800" "63000000"));
    }
    // The c2s bodies stock DreamSeeker sends to DreamDaemon: our encoders must produce them byte for byte.
    void repliesAreStockDreamSeekersBytes() {
        // Cancel, Escape and the close box send the same frame for any `as` type (null|text, num, message, color, icon, file, list),
        // and DM receives null. Id 1 is the second queued prompt.
        QCOMPARE(QString::fromStdString(hs(encodeInputNull(0))), QString("0000ffff000000000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputNull(1))), QString("0000ffff000001000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputText(0, "hi'x"))), QString("0268695c27780000000000"));  // typed hi'x
        QCOMPARE(QString::fromStdString(hs(encodeInputNum(1, 7.f))), QString("010000e04001000000"));  // `as num`, default 7
        QCOMPARE(QString::fromStdString(hs(encodeInputText(2, "#ff0000"))), QString("02236666303030300002000000"));  // `as color`, default
        QCOMPARE(QString::fromStdString(hs(encodeAlertReply(0, "Yes"))), QString("0000000059657300"));
        QCOMPARE(QString::fromStdString(hs(encodeAlertReply(0, "No"))), QString("000000004e6f00"));
    }
    void onlyANullableInputCanBeCancelled() {
        PromptBook b;
        b.onFrame(kAlertMsg, alertReq(1, "m", "t", {"Yes"}));
        b.onFrame(kInputMsg, inputReq("t", "", "m", kInputText, 0, 0, 2));
        b.onFrame(kInputMsg, inputReq("t", "", "m", kInputNull | kInputText, 0, 0, 3));
        b.onFrame(kInputMsg, inputReq("t", "", "m", kInputNull | kInputAnything, 0, kInputFilterList, 4));
        QVERIFY(!b.find(1)->cancellable()); QVERIFY(!b.find(2)->cancellable());
        QVERIFY(b.find(3)->cancellable()); QVERIFY(b.find(4)->cancellable());
        // An alert or a non-null input has no cancel: nothing is sent and the prompt stays pending.
        PromptReply r = b.cancel(1);
        QCOMPARE(r.type, uint16_t(0)); QVERIFY(r.body.empty()); QVERIFY(b.find(1));
        r = b.cancel(2);
        QCOMPARE(r.type, uint16_t(0)); QVERIFY(r.body.empty()); QVERIFY(b.find(2));
        r = b.cancel(3);
        QCOMPARE(r.type, kInputMsg); QCOMPARE(QString::fromStdString(hs(r.body)), QString("0000ffff0000" "03000000"));
        r = b.cancel(4);
        QCOMPARE(r.type, kInputMsg); QCOMPARE(QString::fromStdString(hs(r.body)), QString("0000ffff0000" "04000000"));
        QVERIFY(!b.find(3) && !b.find(4));
        // the uncancellable ones still take their answer
        r = b.answerAlert(1, "Yes");
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("0100000059657300"));
        b.answerInput(2, "x");
        QVERIFY(b.pending().empty());
        // an unknown id still answers null, so a late cancel reaches a parked proc.
        r = b.cancel(77);
        QCOMPARE(r.type, kInputMsg); QCOMPARE(QString::fromStdString(hs(r.body)), QString("0000ffff0000" "4d000000"));
    }
    void inListInputWaitsForItsChoices() {
        PromptBook b;
        const Prompt* p = b.onFrame(kInputMsg, inputReq("t", "", "m", 0, 0, kInputFilterList, 0));
        QVERIFY(p && p->inList && !p->choicesKnown);
        QVERIFY(b.needsChoices());
        QCOMPARE(QString::fromStdString(hs(encodePendingInputExpansionRequest(p->id))), QString("000000000000000000"));  // [00][id 0][0]
        Bytes reply; putLe16(reply, 3); cs(reply, "alpha"); cs(reply, "beta"); cs(reply, "gamma");
        const Prompt* q = b.onFrame(kExpansionReplyMsg, reply);
        QVERIFY(q && q->choicesKnown);
        QCOMPARE(q->choices, (std::vector<std::string>{"alpha", "beta", "gamma"}));
        QVERIFY(!b.needsChoices());
        // a stray 0x003f with nothing waiting is ignored
        QVERIFY(!b.onFrame(kExpansionReplyMsg, reply));
        // a malformed list still releases the prompt (typed text only)
        b.answerInput(0, "alpha");  // (a non-null in-list prompt has no cancel)
        b.onFrame(kInputMsg, inputReq("t", "", "m", 0, 0, kInputFilterList, 0));
        Bytes badReply = hx("0300"); cs(badReply, "x");
        const Prompt* r = b.onFrame(kExpansionReplyMsg, badReply);
        QVERIFY(r && r->choicesKnown && r->choices.empty());
    }
    // An input() over a list of objects answers with the server's member, never the text. A kind-2 text answer reaches DM as text,
    // not as the object, so the object lookup that follows finds nothing.
    void inListPickResolvesThroughTheServer() {
        PromptBook b;
        // request frames: `as null|anything in L` (mask 0x1080), `in L` (0x0), `as text in L` (0x4)
        QVERIFY(b.onFrame(kInputMsg, hx("4172656100" "00" "43686f6f736520746865206a756d702061726561" "00" "80100000" "00" "40" "00000000")));
        QVERIFY(b.find(0)->resolvesThroughServer());
        PromptReply r = b.answerInput(0, "Plain Room");
        QCOMPARE(r.type, kInputResolveMsg);
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("506c61696e2d526f6f6d20" "00" "00000000" "00000000"));
        QVERIFY(b.pending().empty());
        // s2c 0x0039 `04 01000000 00000000` (area #1) -> the input() answer
        r = PromptBook::resolvedInput(0, 0x04, 1);
        QCOMPARE(r.type, kInputMsg);
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("00" "04" "01000000" "00000000"));
        QCOMPARE(QString::fromStdString(hs(PromptBook::resolvedInput(0, 0x06, 0x1b7).body)), QString("0006b701000000000000"));
        // the other picks' 0x0063s, byte for byte (the 0x003f listed them unescaped)
        QCOMPARE(QString::fromStdString(hs(encodeInputResolveRequest(0, "Twin"))), QString("5477696e20" "00" "00000000" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputResolveRequest(0, "Captain's Office"))),
                 QString("4361707461696e5c27732d4f666669636520" "00" "00000000" "00000000"));
        QCOMPARE(QString::fromStdString(hs(encodeInputResolveRequest(0, "Odd [x] a\\b"))), QString("4f64642d5b785d2d615c5c6220" "00" "00000000" "00000000"));
        // a bare `in L` resolves the same way (pick "Arrival Area": 0x0063 `Arrival-Area `, 0x0039 `04 02000000`)
        b.onFrame(kInputMsg, inputReq("Area", "", "Choose the jump area", 0, 0, kInputFilterList, 0));
        r = b.answerInput(0, "Arrival Area");
        QCOMPARE(r.type, kInputResolveMsg);
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("4172726976616c2d4172656120" "00" "00000000" "00000000"));
        // `as text in L` is a plain text field: the typed text goes out as kind-2 text, with no 0x0063.
        b.onFrame(kInputMsg, inputReq("Txt", "", "Choose txt", kInputText, 0, kInputFilterList, 0));
        QVERIFY(!b.find(0)->resolvesThroughServer());
        r = b.answerInput(0, "Twin");
        QCOMPARE(r.type, kInputMsg);
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("025477696e0000000000"));
        // a server that names no member (`00 ffff0000`) gets the null answer; the listbox cannot pick one.
        QCOMPARE(QString::fromStdString(hs(PromptBook::resolvedInput(0, 0, 0xffff).body)), QString::fromStdString(hs(encodeInputNull(0))));
    }

    // A bare DM string keeps `\improper` as FF 16 (`\proper` FF 15). Stock DreamDaemon strips a leading marker from an
    // in-list choice but sends one inside the text, and the other strings as they are; stock DreamSeeker shows none of
    // them, answers an alert() with the label as shown, and an in-list pick resolves from the shown text.
    void textMacroMarkersAreDropped() {
        QCOMPARE(net::plainText("Go to " + kImp + "Research Lab"), std::string("Go to Research Lab"));
        QCOMPARE(net::plainText("\xff\x15" "space"), std::string("space"));
        QCOMPARE(net::plainText(std::string("tail\xff")), std::string("tail"));
        QCOMPARE(net::plainText("caf\xc3\xa9"), std::string("caf\xc3\xa9"));
    }
    void alertWithoutMarkers() {
        PromptBook b;
        const Prompt* p = b.onFrame(kAlertMsg, alertReq(3, "Go to " + kImp + "Arrival Area?", kImp + "Arrival Area", {kImp + "Arrival Area", "Plain", ""}));
        QVERIFY(p);
        QCOMPARE(p->message, std::string("Go to Arrival Area?"));
        QCOMPARE(p->title, std::string("Arrival Area"));
        QCOMPARE(p->buttons, (std::vector<std::string>{"Arrival Area", "Plain"}));
        std::string label = p->buttons[0];   // answering drops the prompt
        QCOMPARE(QString::fromStdString(hs(b.answerAlert(3, label).body)), QString("03000000" "4172726976616c2041726561" "00"));
    }
    void inputInListWithoutMarkers() {
        PromptBook b;
        const Prompt* p = b.onFrame(kInputMsg, inputReq(kImp + "Arrival Area", "", kImp + "Arrival Area", 0x1080, 0, kInputFilterList, 4));
        QVERIFY(p);
        QCOMPARE(p->title, std::string("Arrival Area"));
        QCOMPARE(p->message, std::string("Arrival Area"));
        Bytes reply; putLe16(reply, 2); cs(reply, "Go to " + kImp + "Research Lab"); cs(reply, "Plain");
        p = b.onFrame(kExpansionReplyMsg, reply);
        QVERIFY(p);
        QCOMPARE(p->choices, (std::vector<std::string>{"Go to Research Lab", "Plain"}));
        std::string pick = p->choices[0];
        PromptReply r = b.answerInput(4, pick);
        QCOMPARE(r.type, kInputResolveMsg);
        QCOMPARE(QString::fromStdString(hs(r.body)), QString("476f2d746f2d52657365617263682d4c616220" "00" "04000000" "00000000"));
    }
    // A verb argument's listbox (app/sessionworker.cpp) shows each choice as parseExpansionReply decodes it, and a pick
    // goes back as listPickText of the shown text, which stock DreamDaemon resolves to the member with the marker inside it.
    void verbArgumentListboxWithoutMarkers() {
        Bytes reply; putLe16(reply, 2); cs(reply, "Go to " + kImp + "Research Lab"); cs(reply, "\\'q");
        std::vector<std::string> ch;
        QVERIFY(parseExpansionReply(reply, ch));
        QCOMPARE(ch, (std::vector<std::string>{"Go to Research Lab", "'q"}));
        QCOMPARE(net::listPickText(ch[0]), std::string("Go-to-Research-Lab"));
    }
    void statRowsWithoutMarkers() {
        Session s;
        std::vector<std::string> tabs = {"Status", kImp + "Arrival Area"};
        s.ingest(0x006a, statFrame(0, &tabs, 0, {{"Area:", kImp + "Arrival Area", 0}, {kImp + "Arrival Area", "row", 1}}));
        QCOMPARE(int(s.world().stat.lines.size()), 2);
        QCOMPARE(statText(s.world(), s.world().stat.lines[0]), std::string("Arrival Area"));
        StatValue v;
        QVERIFY(parseStatValue(s.world().stat.lines[0].valueJson, v));
        QCOMPARE(v.text, std::string("Arrival Area"));
        QCOMPARE(s.world().stat.lines[1].name, std::string("Arrival Area"));
        // A statpanel() tab is shown plain; selecting it sends the name as the server sent it, which is what stock
        // DreamDaemon then holds in client.statpanel.
        QCOMPARE(statTabs(s.world()), (std::vector<std::string>{"Status", "Arrival Area"}));
        QCOMPARE(statTabWireName(s.world(), 1), kImp + "Arrival Area");
    }
    void verbsWithoutMarkers() {
        Bytes v; putLe16(v, 1); cs(v, kImp + "Zap Gun"); cs(v, kImp + "Shoots"); cs(v, kImp + "Weird");
        v.push_back(0xff); v.push_back(0); v.push_back(0x04); v.push_back(0);
        Session s;
        s.ingest(0x0011, v);
        VerbDef d;
        QVERIFY(parseVerbDef(s.world().verbById.at(1), d));
        QCOMPARE(d.name, std::string("Zap Gun"));
        QCOMPARE(d.desc, std::string("Shoots"));
        QCOMPARE(d.category, std::string("Weird"));
        QCOMPARE(verbCategories(s.world()), (std::vector<std::string>{"Weird"}));
        QCOMPARE(int(verbsInCategory(s.world(), "Weird").size()), 1);
        QCOMPARE(statTabs(s.world()), (std::vector<std::string>{"Weird"}));
        // The command bar's verb table: stock DreamSeeker runs the verb for `Zap-Gun`.
        net::InputModel im;
        im.onFrame(0x0011, v);
        std::string rest;
        const net::VerbEntry* e = im.matchVerb("Zap-Gun", rest);
        QVERIFY(e);
        QCOMPARE(e->name, std::string("Zap Gun"));
        QCOMPARE(e->category, std::string("Weird"));
        QCOMPARE(e->desc, std::string("Shoots"));
    }
    void chatWithoutMarkers() {
        Session s;
        Bytes out; cs(out, kImp + "Arrival Area\n");
        s.ingest(0x0027, out);
        QCOMPARE(s.world().chat.back(), std::string("Arrival Area"));
        QCOMPARE(s.world().chatHtml.back(), std::string("Arrival Area"));
    }

    // ---- stat panel ----
    void tabBodyMatchesStockDreamSeeker() {  // tabs: Alpha, Beta, Commands (verbs), Status
        QCOMPARE(QString::fromStdString(hs(encodeStatTabBody(1, 1, "Alpha"))), QString("010001416c70686100"));
        QCOMPARE(QString::fromStdString(hs(encodeStatTabBody(3, 3, "verbs"))), QString("030003" "766572627300"));
        QCOMPARE(QString::fromStdString(hs(encodeStatTabBody(0, 4, "Status"))), QString("000004" "53746174757300"));
    }
    void stripIsStatTabsThenAsciiSortedCategories() {
        World w;
        w.stat.tabs = {"Zulu", "Alpha"};
        w.verbById[1] = verbJson(1, "a", "Notes"); w.verbById[2] = verbJson(2, "b", "Bravo"); w.verbById[3] = verbJson(3, "c", "Alpha");
        w.verbById[4] = verbJson(4, "d", "");  // no category: Commands
        w.verbById[5] = verbJson(5, "e", "", 0x04 | 0x20);  // set category = null -> NO panel
        w.verbById[6] = verbJson(6, "f", "Notes", 0x05);  // hidden: no panel
        w.verbById[7] = verbJson(7, "g", "Notes", 0x44);  // nopopup: still in the panel
        w.verbById[8] = verbJson(8, "h", "Object"); w.verbById[9] = verbJson(9, "i", "OOC");
        // concatenated, NOT merged (Alpha twice), Stat() order kept (Zulu before Alpha), byte order ("OOC" < "Object")
        QCOMPARE(statTabs(w), (std::vector<std::string>{"Zulu", "Alpha", "Alpha", "Bravo", "Commands", "Notes", "OOC", "Object"}));
        QCOMPARE(statTabWireName(w, 0), std::string("Zulu"));
        QCOMPARE(statTabWireName(w, 3), std::string("verbs"));  // every category tab sends "verbs"
        QCOMPARE(statTabWireName(w, 99), std::string());
        std::string cat;
        QVERIFY(!verbTabCategory(w, 1, cat));
        QVERIFY(verbTabCategory(w, 6, cat)); QCOMPARE(cat, std::string("OOC"));
        // rows of a category: by NAME, byte order; the nopopup verb is drawn, the hidden one is not
        w.verbById[10] = verbJson(10, "zzz last", "Notes"); w.verbById[11] = verbJson(11, "aaa first", "Notes");
        std::vector<std::string> names;
        for (auto& e : verbsInCategory(w, "Notes")) names.push_back(e.name);
        QCOMPARE(names, (std::vector<std::string>{"a", "aaa first", "g", "zzz last"}));
    }
    void hiddenViaFlagsExtensionIsHidden() {  // flags head byte 0x80, the real word (0x285, hidden bit 1) in the extension
        World w;
        w.verbById[1] = verbJson(1, "MoveKey", "", 0x80, 0x285);
        w.verbById[2] = verbJson(2, "Say", "IC", 0x04);
        QCOMPARE(verbCategories(w), (std::vector<std::string>{"IC"}));
    }
    void verbScopeFiltersWhenKnowableAndFallsBackWhenNot() {
        World w;
        w.verbById[1] = verbJson(1, "mine", "Mine"); w.verbById[2] = verbJson(2, "theirs", "Theirs");
        QCOMPARE(verbCategories(w), (std::vector<std::string>{"Mine", "Theirs"}));  // unknown scope: the whole dictionary
        w.haveClientAppid = true; w.clientAppid = 10;
        w.appearances[10].fieldsJson = "{\"verbs\":[1]}";
        w.haveSelf = true; w.selfRef = 5;
        w.atoms[5 | kRegistryA].appid = 20;
        w.appearances[20].fieldsJson = "{\"verbs\":[]}";
        QCOMPARE(verbCategories(w), (std::vector<std::string>{"Mine"}));  // client section = verb 1 only
    }
    void verbScopeIncludesAtomsWhoseSrcClauseAdmitsThem() {
        auto withSrc = [](std::string j, int src, int head) {
            j.replace(j.find("\"head\":255"), 10, "\"head\":" + std::to_string(head));
            j.replace(j.find("\"src\":0"), 7, "\"src\":" + std::to_string(src));
            return j;
        };
        World w;
        w.verbById[1] = verbJson(1, "mine", "Mine");
        w.verbById[2] = verbJson(2, "zoom", "Object");                           // no clause: admits a carried obj only
        w.verbById[3] = withSrc(verbJson(3, "near", "Near"), 0x01, 1);           // set src in view(1)
        w.verbById[4] = withSrc(verbJson(4, "far", "Far"), 0x01, 1);
        w.verbById[5] = verbJson(5, "floor", "Floor");                           // no clause on an obj off the contents
        w.haveClientAppid = true; w.clientAppid = 10;
        w.appearances[10].fieldsJson = "{\"verbs\":[1]}";
        w.haveSelf = true; w.selfRef = 5;
        auto& me = w.atoms[5 | kRegistryA];
        me.appid = 20; me.hasPos = true; me.x = 10; me.y = 10;
        w.appearances[20].fieldsJson = "{\"verbs\":[]}";
        w.appearances[30].fieldsJson = "{\"verbs\":[2]}";                       // a camera in usr.contents
        w.usrContents[100] = 30;
        w.appearances[31].fieldsJson = "{\"verbs\":[3]}";
        auto& near = w.atoms[200]; near.appid = 31; near.hasPos = true; near.x = 11; near.y = 10;
        w.appearances[32].fieldsJson = "{\"verbs\":[4]}";
        auto& far = w.atoms[201]; far.appid = 32; far.hasPos = true; far.x = 14; far.y = 10;
        w.appearances[33].fieldsJson = "{\"verbs\":[5]}";
        auto& floor = w.atoms[202]; floor.appid = 33; floor.hasPos = true; floor.x = 10; floor.y = 10;
        QCOMPARE(verbCategories(w), (std::vector<std::string>{"Mine", "Near", "Object"}));
        QCOMPARE(int(verbsInCategory(w, "Object").size()), 1);
    }
    void verbScopeCoversTurfAreaScreenAndRefreshes() {
        auto src = [](int id, const char* name, const char* cat, int src, int head) {
            std::string j = verbJson(id, name, cat);
            j.replace(j.find("\"head\":255"), 10, "\"head\":" + std::to_string(head));
            j.replace(j.find("\"src\":0"), 7, "\"src\":" + std::to_string(src));
            return j;
        };
        World w;
        w.verbById[1] = verbJson(1, "turf_here", "Turf");                 // turf verb, no clause: the player's own cell
        w.verbById[2] = verbJson(2, "turf_next", "Turf");                 // no clause on the cell beside the player
        w.verbById[3] = src(3, "turf_view1", "Turf", 0x01, 1);            // set src in view(1)
        w.verbById[4] = verbJson(4, "area_here", "Area");                 // area verb, no clause
        w.verbById[5] = verbJson(5, "screen_noclause", "Screen");
        w.verbById[6] = src(6, "screen_world", "Screen", 0x10, 255);
        w.verbById[7] = src(7, "screen_view", "Screen", 0x01, 255);       // a HUD obj has no cell: not in view()
        w.verbById[8] = verbJson(8, "carried", "Carried");
        w.verbById[9] = verbJson(9, "same_number", "Carried");            // on an obj numbered like the own mob, lying about
        w.haveClientAppid = true; w.clientAppid = 10;
        w.appearances[10].fieldsJson = "{\"verbs\":[]}";
        w.haveSelf = true; w.selfRef = 5;
        auto& me = w.atoms[5 | kRegistryA];
        me.appid = 20; me.hasPos = true; me.x = 10; me.y = 10;
        w.appearances[20].fieldsJson = "{\"verbs\":[]}";
        w.appearances[30].fieldsJson = "{\"verbs\":[1]}";
        w.appearances[31].fieldsJson = "{\"verbs\":[2]}";
        w.appearances[32].fieldsJson = "{\"verbs\":[3]}";
        w.appearances[33].fieldsJson = "{\"verbs\":[4]}";
        w.tiles[{9, 9, 0}] = 30;        // wire cells are 0-based: (10,10)
        w.tiles[{10, 9, 0}] = 31;       // (11,10)
        w.tiles[{9, 10, 0}] = 32;       // (10,11)
        w.areaCells[{9, 9, 0}] = 7;
        w.turfBinds[7] = 33;
        w.appearances[40].fieldsJson = "{\"verbs\":[5,6,7]}";
        w.hud[300] = "{\"appid\":40}"; w.hudAppid[300] = 40;
        w.appearances[41].fieldsJson = "{\"verbs\":[8]}";
        w.usrContents[100] = 41;
        w.appearances[42].fieldsJson = "{\"verbs\":[9]}";
        auto& twin = w.atoms[5]; twin.appid = 42; twin.hasPos = true; twin.x = 14; twin.y = 10;   // obj 5 is not mob 5
        std::vector<uint16_t> ids;
        QVERIFY(verbScope(w, ids));
        QCOMPARE(ids, (std::vector<uint16_t>{1, 3, 4, 6, 8}));
        // The panel follows the world: dropping the carried obj and stepping off the turf changes the scope.
        w.usrContents.clear();
        me.x = 11;
        QVERIFY(verbScope(w, ids));
        QCOMPARE(ids, (std::vector<uint16_t>{2, 3, 6}));   // turf_next is now underfoot; (10,11) is still within view(1)
    }
    void tokenGateAndOwnership() {
        Session s;
        std::vector<std::string> tabs = {"Status", "Values"};
        s.ingest(0x006a, statFrame(0, &tabs, 0, {{"S-Health:", "100", 0}}));
        QCOMPARE(int(s.world().stat.lines.size()), 1);
        StatTabSelect sel;
        QVERIFY(selectStatTab(s.worldMut(), 1, sel));
        QCOMPARE(int(sel.token), 1); QCOMPARE(sel.name, std::string("Values")); QCOMPARE(int(sel.index), 1);
        QVERIFY(s.world().stat.lines.empty());  // the rows of the old tab are gone the moment we ask
        // a late frame for the tab we left must not land in the new one; token 0 is unsolicited.
        s.ingest(0x006a, statFrame(7, nullptr, -1, {{"STALE:", "x", 0}}));
        QCOMPARE(s.world().stat.stale, 1); QVERIFY(s.world().stat.lines.empty());
        s.ingest(0x006a, statFrame(1, nullptr, 1, {{"V-Int:", "42", 0}}));  // our token
        QCOMPARE(int(s.world().stat.lines.size()), 1); QCOMPARE(s.world().stat.lines[0].name, std::string("V-Int:"));
        QVERIFY(selectStatTab(s.worldMut(), 0, sel)); QCOMPARE(int(sel.token), 2);
        s.ingest(0x006a, statFrame(0, nullptr, -1, {{"UNSOLICITED:", "y", 0}}));  // token 0 releases ownership and is accepted
        QVERIFY(!s.world().stat.owns); QCOMPARE(s.world().stat.lines.back().name, std::string("UNSOLICITED:"));
        // token wraps past 0: 0 must never be ours
        s.worldMut().stat.token = 255;
        QVERIFY(selectStatTab(s.worldMut(), 0, sel)); QCOMPARE(int(sel.token), 1);
        QVERIFY(!selectStatTab(s.worldMut(), 9, sel));  // out of range: refused, nothing sent
    }
    void serverEchoDoesNotStealACategoryTab() {
        Session s;
        std::vector<std::string> tabs = {"Status"};
        s.worldMut().verbById[1] = verbJson(1, "v", "Notes");
        s.ingest(0x006a, statFrame(0, &tabs, 0, {{"S:", "1", 0}}));
        StatTabSelect sel;
        QVERIFY(selectStatTab(s.worldMut(), 1, sel));  // strip = [Status, Notes]; Notes is index 1
        QCOMPARE(sel.name, std::string("verbs"));
        s.ingest(0x006a, statFrame(1, nullptr, 0, {}));  // the server's routine echo of its tab index
        QCOMPARE(s.world().stat.selected, 1);  // must not drag us back
        // a frame that carries the tab list too (a new Stat() tab appeared and was selected) is a real switch
        std::vector<std::string> grown = {"Status", "floor"};
        s.ingest(0x006a, statFrame(1, &grown, 1, {{"Turf:", "floor", 0}}));
        QCOMPARE(s.world().stat.selected, 1); QCOMPARE(int(s.world().stat.lines.size()), 1);
    }
    void replayNeverGates() {  // no selectStatTab => owns stays false: the snapshot-visible token/stale are untouched
        Session s;
        std::vector<std::string> tabs = {"A", "B"};
        s.ingest(0x006a, statFrame(2, &tabs, 0, {{"x", "1", 0}}));
        s.ingest(0x006a, statFrame(9, nullptr, -1, {{"y", "2", 1}}));
        QCOMPARE(s.world().stat.stale, 0); QCOMPARE(int(s.world().stat.lines.size()), 2); QCOMPARE(int(s.world().stat.token), 0);
    }
    void rowTextAndClickability() {
        World w;
        World::StatLine l{0, "S-Health:", "{\"appid\":0,\"hasAppid\":false,\"head\":0,\"kind\":42,\"num\":{\"f64\":\"0x4059000000000000\"},\"text\":\"\"}"};
        QCOMPARE(statText(w, l), std::string("100"));
        l.valueJson = "{\"appid\":0,\"hasAppid\":false,\"head\":0,\"kind\":42,\"num\":{\"f64\":\"0xc031800000000000\"},\"text\":\"\"}";
        QCOMPARE(statText(w, l), std::string("-17.5"));
        l.valueJson = "{\"appid\":0,\"hasAppid\":false,\"head\":0,\"kind\":6,\"num\":{\"f64\":\"0x0\"},\"text\":\"a & b <c>\"}";
        QCOMPARE(statText(w, l), std::string("a & b <c>"));
        // atom rows print the atom name (needs the appearance table); list/file/datum/null print nothing
        l.valueJson = "{\"appid\":42,\"hasAppid\":true,\"head\":3,\"kind\":2,\"num\":{\"f64\":\"0x0\"},\"text\":\"\"}";
        QCOMPARE(plainAtomName(std::string("a\xff" "Bc")), std::string("ac"));  // 0xFF and its selector byte are dropped
        w.appearances[42].fieldsJson = "{\"name\":\"token\"}";
        QCOMPARE(statText(w, l), std::string("token"));
        l.valueJson = "{\"appid\":0,\"hasAppid\":false,\"head\":0,\"kind\":15,\"num\":{\"f64\":\"0x0\"},\"text\":\"\"}";
        QCOMPARE(statText(w, l), std::string());
        // clickable: turf / obj / mob only (AREA stays refused), and only with an appid
        proto::enc::AtomRef ref; StatValue v;
        w.bounds.maxX = 500; w.bounds.maxY = 500;
        v.kind = 0x01; v.hasAppid = true; v.head = uint32_t((239 - 1) * 500 + (238 - 1));
        QVERIFY(statRowRef(w, v, ref)); QCOMPARE(int(ref.kind), int(proto::enc::RefTurfId)); QCOMPARE(ref.ref, v.head);  // [07][the row's head]
        v.kind = 0x02; v.head = 5; QVERIFY(statRowRef(w, v, ref)); QCOMPARE(int(ref.kind), int(proto::enc::RefObj)); QCOMPARE(ref.ref, 5u);
        v.kind = 0x03; QVERIFY(statRowRef(w, v, ref)); QCOMPARE(int(ref.kind), int(proto::enc::RefMob));
        v.kind = 0x04; QVERIFY(!statRowRef(w, v, ref));
        v.kind = 0x02; v.hasAppid = false; QVERIFY(!statRowRef(w, v, ref));
        v.hasAppid = true; w.bounds.maxX = 0; v.kind = 0x01; QVERIFY(!statRowRef(w, v, ref));  // no bounds: refuse, never divide by zero
    }
    void numberRowsPrintAsStockDreamSeeker() {  // the wire carries float32; each pair is (sent value, stock's text)
        const std::pair<float, const char*> cases[] = {
            {0.f, "0"}, {3.f, "3"}, {-17.5f, "-17.5"}, {123.4f, "123.4"}, {-123.4f, "-123.4"}, {177.7f, "177.7"},
            {0.1f, "0.1"}, {1.f / 3.f, "0.333333"}, {3.14159265f, "3.14159"}, {65.4321f, "65.4321"},
            {2.5e-05f, "2.5e-05"}, {1.2345e-05f, "1.2345e-05"}, {0.0001f, "0.0001"}, {-0.000123456f, "-0.000123456"}, {1e-07f, "1e-07"},
            {999999.f, "999999"}, {123456.7f, "123457"}, {999999.5f, "1e+06"}, {1e6f, "1e+06"}, {-1e6f, "-1e+06"},
            {1234567.f, "1.23457e+06"}, {16777216.f, "1.67772e+07"}, {1e15f, "1e+15"}, {1e20f, "1e+20"}, {1.5e38f, "1.5e+38"},
        };
        World w;
        for (auto [f, want] : cases) {
            double d = f;
            uint64_t bits; std::memcpy(&bits, &d, 8);
            char hex[32]; std::snprintf(hex, sizeof hex, "0x%llx", (unsigned long long)bits);
            World::StatLine l{0, "n", std::string("{\"appid\":0,\"hasAppid\":false,\"head\":0,\"kind\":42,\"num\":{\"f64\":\"") + hex + "\"},\"text\":\"\"}"};
            QCOMPARE(QString::fromStdString(statText(w, l)), QString(want));
        }
    }
    // A click on a stat-panel row, as stock DreamSeeker sends it: the location is "Probe" and the control is "infowindow.info".
    // Params are as client/Click receives them.
    void rowClickIsStockDreamSeekersFrame() {
        World w;
        w.bounds.maxX = 15; w.bounds.maxY = 15;
        w.stat.tabs = {"Probe", "Second"};
        w.stat.selected = 0;
        StatRowClick c;
        c.control = "infowindow.info";
        const char* tail = "030250726f62650002696e666f77696e646f772e696e666f0002";  // [03][02 "Probe"][02 "infowindow.info"][02 ...
        auto body = [&](const proto::enc::AtomRef& r) { return QString::fromStdString(hs(statRowClickBody(w, r, c))); };
        // stat("pocket", obj in contents), stat(obj) and an obj on a turf: over [01][head]; the loc slot is the PANEL, not a turf
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6c6566743d313b627574746f6e3d6c65667400");
        QCOMPARE(body(proto::enc::objRef(2)), QString("0300ffff0102000000") + tail + "6c6566743d313b627574746f6e3d6c65667400");
        QCOMPARE(body(proto::enc::mobRef(0)), QString("0300ffff0200000000") + tail + "6c6566743d313b627574746f6e3d6c65667400");  // stat("me", src)
        // modifiers and buttons
        c.mods.shift = true;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6c6566743d313b73686966743d313b627574746f6e3d6c65667400");
        c.mods.ctrl = true;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6c6566743d313b6374726c3d313b73686966743d313b627574746f6e3d6c65667400");
        c.mods.shift = false;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6c6566743d313b6374726c3d313b627574746f6e3d6c65667400");
        c.mods = {}; c.mods.alt = true;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6c6566743d313b616c743d313b627574746f6e3d6c65667400");
        c.mods = {}; c.button = "middle";
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff0100000000") + tail + "6d6964646c653d313b627574746f6e3d6d6964646c6500");
        // the second press of a double click is sub 04 (stock sends 03, 04, 03)
        c.button = "left"; c.sub = proto::enc::kMouseDblClick;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0400ffff0100000000") + tail + "6c6566743d313b627574746f6e3d6c65667400");
        // the location is the SELECTED tab: the same row clicked on "Second"
        c.sub = proto::enc::kMouseClick; w.stat.selected = 1;
        QCOMPARE(body(proto::enc::objRef(0)), QString("0300ffff010000000003025365636f6e640002696e666f77696e646f772e696e666f00026c6566743d313b627574746f6e3d6c65667400"));
        // A turf row names the turf by id: `over` for the turf whose row head was 0x40; params carry icon-x/icon-y because the turf's icon is drawn.
        w.stat.selected = 0;
        StatValue v; v.kind = 0x01; v.hasAppid = true; v.head = 0x40;
        proto::enc::AtomRef ref;
        QVERIFY(statRowRef(w, v, ref));
        QVERIFY(body(ref).startsWith("0300ffff0740000000" + QString(tail)));
    }
    // A click on a row that draws its icon (stock DreamSeeker and DreamDaemon, staticons world): icon-x/icon-y lead,
    // in the row's icon cell, with the modifiers where they always were.
    void iconRowClickLeadsWithIconXY() {
        using proto::enc::infoClickParams;
        proto::enc::Mods m;
        QCOMPARE(infoClickParams("left", m, true, 10, 10), std::string("icon-x=10;icon-y=10;left=1;button=left"));
        QCOMPARE(infoClickParams("left", m, true, 1, 1), std::string("icon-x=1;icon-y=1;left=1;button=left"));  // the cell's bottom-left pixel
        QCOMPARE(infoClickParams("left", m, true, 70, 20), std::string("icon-x=70;icon-y=20;left=1;button=left"));  // on the name: unclamped
        QCOMPARE(infoClickParams("middle", m, true, 10, 10), std::string("icon-x=10;icon-y=10;middle=1;button=middle"));
        m.ctrl = m.shift = true;
        QCOMPARE(infoClickParams("left", m, true, 10, 10), std::string("icon-x=10;icon-y=10;left=1;ctrl=1;shift=1;button=left"));
        m = {}; m.alt = true;
        QCOMPARE(infoClickParams("left", m, true, 10, 10), std::string("icon-x=10;icon-y=10;left=1;alt=1;button=left"));
        QCOMPARE(infoClickParams("left", {}, false, 10, 10), std::string("left=1;button=left"));  // o_noicon: nothing drawn, no icon-x
        // ...and through the body: the selected tab, the INFO control, then these params
        World w;
        w.stat.tabs = {"Tile"};
        StatRowClick c;
        c.control = "infowindow.info"; c.icon = true; c.iconX = 10; c.iconY = 10;
        QCOMPARE(QString::fromStdString(hs(statRowClickBody(w, proto::enc::objRef(2), c))),
                 QString("0300ffff0102000000") + "030254696c650002696e666f77696e646f772e696e666f0002" +
                     QString::fromLatin1(QByteArray("icon-x=10;icon-y=10;left=1;button=left").toHex()) + "00");
    }
};

QTEST_APPLESS_MAIN(PromptStatTest)
#include "prompt_stat_test.moc"
