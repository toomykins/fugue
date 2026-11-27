// Player macro lists (skin/usermacros.h): the Client > Macros layer.
// File format: the bytes stock DreamSeeker 516.1684 writes to cfg/macros.dmf parse, and what we write reads back identically in the same shape.
// Editing: the entry editor's commit rule, including that a blank key or command deletes.
// Precedence: skin set > this game's list > global list; a key the skin binds is in use. Any: which spec answers a key.
// Storage: paths under the state dir, with a save/load round trip.
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"

#include "skin/dmf.h"
#include "skin/usermacros.h"

using namespace fugue::skin;

namespace {
// Stock DreamSeeker's output after F6 -> hello then F1 -> zap user: "macro " entries with CRLF, newest first.
const char* kDsGlobal =
    "macro \r\n\telem \r\n\t\tname = \"F1\"\r\n\t\tcommand = \"zap user\"\r\n\telem \r\n\t\tname = \"F6\"\r\n"
    "\t\tcommand = \"hello\"\r\n\r\n";

// Stock DreamSeeker's per-game file after F6 -> Rest then R -> zap userR: newest entry first, so the file is not sorted by key.
const char* kDsGame =
    "macro \r\n\telem \r\n\t\tname = \"R\"\r\n\t\tcommand = \"zap userR\"\r\n\telem \r\n\t\tname = \"F6\"\r\n"
    "\t\tcommand = \"Rest\"\r\n\r\n";

MacroKey key(const char* spec, bool* rep = nullptr) {
    MacroKey k;
    bool r = false;
    parseMacroName(spec, k, r);
    if (rep) *rep = r;
    return k;
}

MacroSet skinSet(const char* dmf) {
    ParseResult r = parseDmf(dmf);
    if (!r.ok) qFatal("bad test skin: %s", r.error.c_str());
    return macroSetFromBlock(r.file.blocks.at(0));
}
}  // namespace

class UserMacros : public QObject {
    Q_OBJECT
private slots:
    void readsDreamSeekersOwnFile() {
        std::string err;
        MacroSet s = parseUserMacros(kDsGlobal, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(int(s.entries.size()), 2);
        const MacroEntry* f1 = s.lookup(key("F1"), false);
        QVERIFY(f1);
        QCOMPARE(QString::fromStdString(f1->command), QString("zap user"));
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("hello"));
    }
    void readsAPerGameFileNewestFirst() {
        std::string err;
        MacroSet s = parseUserMacros(kDsGame, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(int(s.entries.size()), 2);
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("Rest"));
        QCOMPARE(QString::fromStdString(s.lookup(key("R"), false)->command), QString("zap userR"));
        MacroSet r = parseUserMacros(formatUserMacros(s));  // what we write back reads the same
        QCOMPARE(int(r.entries.size()), 2);
        QCOMPARE(QString::fromStdString(r.lookup(key("F6"), false)->command), QString("Rest"));
    }
    void writesDreamSeekersShape() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "F6", "hello"));
        QVERIFY(editUserMacro(s, "", "F1", "zap user"));
        std::string out = formatUserMacros(s);
        std::string ds = kDsGlobal;
        std::string lf;
        for (char c : ds)
            if (c != '\r') lf.push_back(c);
        QCOMPARE(QString::fromStdString(out),
                 QString::fromStdString(lf));  // unnamed datum, same rows; ours is sorted by key, LF endings
    }
    void roundTripsEscapesIdsAndFlags() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "CTRL+SHIFT+F8+REP", "say \"hi\\there\"", "myid"));
        QVERIFY(editUserMacro(s, "", "WEST+UP", "MoveKey 8 0"));
        MacroSet r = parseUserMacros(formatUserMacros(s));
        QCOMPARE(int(r.entries.size()), 2);
        bool rep = false;
        MacroKey k = key("SHIFT+CTRL+F8+REP", &rep);  // modifier order does not matter
        QVERIFY(rep);
        const MacroEntry* e = r.lookup(k, true);
        QVERIFY(e);
        QCOMPARE(QString::fromStdString(e->command), QString("say \"hi\\there\""));
        QCOMPARE(QString::fromStdString(e->id), QString("myid"));
        QVERIFY(r.lookup(key("WEST+UP"), false));
        QVERIFY(!r.lookup(key("WEST"), false));  // +UP is part of the identity
    }
    void editorCommitRule() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "F6", "hello"));
        QVERIFY(editUserMacro(s, "", "f6", "zap"));  // re-binding the same key replaces it
        QCOMPARE(int(s.entries.size()), 1);
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("zap"));
        QVERIFY(editUserMacro(s, "F6", "F7", "zap"));  // editing the key moves the binding
        QVERIFY(!s.lookup(key("F6"), false));
        QVERIFY(s.lookup(key("F7"), false));
        QVERIFY(editUserMacro(s, "F7", "F7", "   "));  // blank command deletes
        QCOMPARE(int(s.entries.size()), 0);
        QVERIFY(editUserMacro(s, "", "F9", "x"));
        QVERIFY(editUserMacro(s, "F9", "", "x"));  // blank key deletes
        QCOMPARE(int(s.entries.size()), 0);
        QVERIFY(editUserMacro(s, "", "F9", "x"));
        QVERIFY(!editUserMacro(s, "F9", "+REP", "x"));  // names no key: refused, nothing changed
        QVERIFY(s.lookup(key("F9"), false));
    }
    void precedenceIsSkinThenGameThenGlobal() {
        // Layers: skin F1 -> hello; global F1 -> zap user, F6 -> hello; this game F6 -> zap game, F7 -> hello.
        MacroSet skin = skinSet(
            "macro \"macro\"\n\telem\n\t\tname = \"F1\"\n\t\tcommand = \"hello\"\n"
            "\telem\n\t\tname = \"F2\"\n\t\tcommand = \"x\"\n\t\tis-disabled = true\n");
        MacroSet global, game;
        editUserMacro(global, "", "F1", "zap user");
        editUserMacro(global, "", "F6", "hello");
        editUserMacro(global, "", "F2", "global f2");
        editUserMacro(game, "", "F6", "zap game");
        editUserMacro(game, "", "F7", "hello");
        auto fire = [&](const char* k) {
            const MacroEntry* e = lookupLayered(&skin, game, global, key(k), false);
            return e ? QString::fromStdString(e->command) : QString("<none>");
        };
        QCOMPARE(fire("F1"), QString("hello"));     // the skin's: the key is in use
        QCOMPARE(fire("F6"), QString("zap game"));  // this game's beats global
        QCOMPARE(fire("F7"), QString("hello"));     // this game's alone
        QCOMPARE(fire("F2"), QString("<none>"));    // a disabled skin binding still claims its key
        QCOMPARE(fire("F3"), QString("<none>"));
        MacroSet noGame;
        QCOMPARE(QString::fromStdString(lookupLayered(&skin, noGame, global, key("F6"), false)->command),
                 QString("hello"));
        QCOMPARE(QString::fromStdString(lookupLayered(nullptr, noGame, global, key("F1"), false)->command),
                 QString("zap user"));
    }
    void anyAnswersEveryKeyWithTheBestFittingModifiers() {
        auto set = [](std::initializer_list<const char*> specs) {
            MacroSet s;
            for (const char* sp : specs) editUserMacro(s, "", sp, sp);
            return s;
        };
        auto any = [](const MacroSet& s, const char* k, bool repeat = false) {
            const MacroEntry* e = s.findAny(key(k), repeat);
            return e ? QString::fromStdString(e->spec) : QString("<none>");
        };
        MacroSet a = set({"Any", "SHIFT+Any", "CTRL+SHIFT+Any", "CTRL+ALT+Any"});
        QCOMPARE(any(a, "Q"), QString("Any"));
        QCOMPARE(any(a, "CTRL+Q"), QString("Any"));
        QCOMPARE(any(a, "SHIFT+Q"), QString("SHIFT+Any"));
        QCOMPARE(any(a, "CTRL+SHIFT+Q"), QString("CTRL+SHIFT+Any"));
        QCOMPARE(any(a, "SHIFT+ALT+Q"), QString("SHIFT+Any"));
        QCOMPARE(any(a, "CTRL+SHIFT+ALT+Q"), QString("CTRL+ALT+Any"));
        MacroSet b = set({"Any", "CTRL+Any", "SHIFT+Any", "ALT+Any"});
        QCOMPARE(any(b, "CTRL+SHIFT+Q"), QString("CTRL+Any"));
        QCOMPARE(any(b, "SHIFT+ALT+Q"), QString("ALT+Any"));
        QCOMPARE(any(b, "CTRL+ALT+Q"), QString("ALT+Any"));
        QCOMPARE(any(b, "CTRL+SHIFT+ALT+Q"), QString("ALT+Any"));
        MacroSet c = set({"Any+REP", "ALT+Any", "CTRL+SHIFT+Any"});
        QCOMPARE(any(c, "CTRL+SHIFT+ALT+Q"), QString("CTRL+SHIFT+Any"));
        QCOMPARE(any(c, "Q"), QString("Any+REP"));
        QCOMPARE(any(c, "Q", true), QString("<none>"));  // Any never repeats, +REP or not
        MacroSet d = set({"Any", "Any+REP", "SHIFT+ALT+Any", "CTRL+SHIFT+Any"});
        QCOMPARE(any(d, "Q"), QString("Any"));
        QCOMPARE(any(d, "CTRL+SHIFT+ALT+Q"), QString("SHIFT+ALT+Any"));
        MacroSet e = set({"Any+UP", "SHIFT+Any+UP"});
        QCOMPARE(any(e, "SHIFT+Q+UP"), QString("Any+UP"));
        QCOMPARE(any(set({"SHIFT+Any+UP"}), "SHIFT+Q+UP"), QString("<none>"));
        // the key's own binding is found apart from Any
        MacroSet f = set({"Any", "Z"});
        QVERIFY(f.lookup(key("Z"), false));
        QVERIFY(!f.lookup(key("Q"), false));
        QCOMPARE(QString::fromStdString(lookupLayeredAny(&f, MacroSet{}, MacroSet{}, key("Q"), false)->spec),
                 QString("Any"));
        for (auto [canon, name] : {std::pair{"NORTH", "North"},
                                   {"SHIFT", "Shift"},
                                   {"CTRL", "Ctrl"},
                                   {"NORTHWEST", "Northwest"},
                                   {"BACK", "Back"},
                                   {"F11", "F11"},
                                   {"A", "A"},
                                   {"[", "["}})
            QCOMPARE(QString::fromStdString(anyKeyName(canon)), QString(name));
    }
    void ownBindingFallsBackToFewerModifiers() {
        auto set = [](std::initializer_list<const char*> specs) {
            MacroSet s;
            for (const char* sp : specs) {
                bool off = sp[0] == '!';
                editUserMacro(s, "", sp + off, sp + off);
                s.entries.back().disabled = off;
            }
            return s;
        };
        auto fire = [](const MacroSet& s, const char* k, bool repeat = false) {
            const MacroEntry* e = lookupHeld({&s}, key(k), repeat);
            return e ? QString::fromStdString(e->spec) : QString("<none>");
        };
        MacroSet z = set({"Z"});
        for (const char* k : {"Z", "CTRL+Z", "SHIFT+Z", "ALT+Z", "CTRL+SHIFT+ALT+Z"})
            QCOMPARE(fire(z, k), QString("Z"));
        QCOMPARE(fire(z, "Q"), QString("<none>"));
        MacroSet a = set({"SHIFT+Z", "CTRL+Z", "ALT+Z"});
        QCOMPARE(fire(a, "Z"), QString("<none>"));
        QCOMPARE(fire(a, "CTRL+SHIFT+Z"), QString("CTRL+Z"));
        QCOMPARE(fire(a, "CTRL+ALT+Z"), QString("ALT+Z"));
        QCOMPARE(fire(a, "SHIFT+ALT+Z"), QString("ALT+Z"));
        QCOMPARE(fire(a, "CTRL+SHIFT+ALT+Z"), QString("ALT+Z"));
        MacroSet b = set({"CTRL+SHIFT+Z", "ALT+Z", "Z"});
        QCOMPARE(fire(b, "CTRL+Z"), QString("Z"));
        QCOMPARE(fire(b, "CTRL+ALT+Z"), QString("ALT+Z"));
        QCOMPARE(fire(b, "CTRL+SHIFT+ALT+Z"), QString("CTRL+SHIFT+Z"));
        QCOMPARE(fire(set({"CTRL+SHIFT+Z", "SHIFT+ALT+Z", "CTRL+ALT+Z"}), "CTRL+SHIFT+ALT+Z"), QString("CTRL+ALT+Z"));
        // releases: the +UP bindings, by the same order
        MacroSet u = set({"Z", "Z+UP", "CTRL+Z", "SHIFT+Z+UP"});
        QCOMPARE(fire(u, "CTRL+Z+UP"), QString("Z+UP"));
        QCOMPARE(fire(u, "SHIFT+ALT+Z+UP"), QString("SHIFT+Z+UP"));
        QCOMPARE(fire(set({"Z", "CTRL+Z+UP"}), "CTRL+SHIFT+Z+UP"), QString("CTRL+Z+UP"));
        // repeats: a hold repeats if the combination that claims the press has a REP binding (repeats()); each repeat
        // runs the first REP binding the modifiers held then reach
        auto repeats = [](const MacroSet& s, const char* k) { return matchHeld({&s}, key(k)).rep != nullptr; };
        MacroSet r = set({"Z+REP"});
        QCOMPARE(fire(r, "CTRL+Z"), QString("Z+REP"));
        QCOMPARE(fire(r, "CTRL+Z", true), QString("Z+REP"));
        QVERIFY(repeats(r, "CTRL+Z"));
        MacroSet r2 = set({"Z+REP", "CTRL+Z"});
        QCOMPARE(fire(r2, "CTRL+Z"), QString("CTRL+Z"));
        QVERIFY(!repeats(r2, "CTRL+Z"));
        QVERIFY(repeats(r2, "Z"));
        QCOMPARE(fire(r2, "CTRL+Z", true), QString("Z+REP"));
        MacroSet both = set({"Z", "Z+REP"});
        QCOMPARE(fire(both, "Z"), QString("Z"));
        QVERIFY(repeats(both, "Z"));
        QCOMPARE(fire(both, "Z", true), QString("Z+REP"));
        MacroSet r3 = set({"Z", "CTRL+Z+REP"});
        QCOMPARE(fire(r3, "CTRL+Z"), QString("CTRL+Z+REP"));
        QCOMPARE(fire(r3, "SHIFT+Z", true), QString("<none>"));
        MacroSet r4 = set({"CTRL+Z", "CTRL+Z+REP", "Z+REP"});
        QCOMPARE(fire(r4, "CTRL+Z"), QString("CTRL+Z"));
        QCOMPARE(fire(r4, "CTRL+Z", true), QString("CTRL+Z+REP"));
        // a disabled binding is passed over
        QCOMPARE(fire(set({"Z", "!CTRL+Z"}), "CTRL+Z"), QString("Z"));
        QCOMPARE(fire(set({"Z", "!SHIFT+Z", "CTRL+Z"}), "CTRL+SHIFT+Z"), QString("CTRL+Z"));
        QCOMPARE(fire(set({"!Z"}), "CTRL+Z"), QString("<none>"));
        QCOMPARE(fire(set({"!CTRL+Z+REP", "Z+REP"}), "CTRL+Z", true), QString("Z+REP"));
        QVERIFY(!repeats(set({"CTRL+Z", "!CTRL+Z+REP", "Z+REP"}), "CTRL+Z"));
        QCOMPARE(fire(set({"!CTRL+Z", "CTRL+Z+REP", "Z"}), "CTRL+Z"), QString("CTRL+Z+REP"));
        // with the player's lists: each combination is the skin's, then a list's, before fewer modifiers are tried
        MacroSet skinZ = set({"Z"}), skinCQ = set({"CTRL+Q", "Z"}), skinOff = set({"!CTRL+Z", "Z", "!Q"}), none;
        MacroSet global = set({"CTRL+Z", "Q"});
        auto layered = [&](const MacroSet& s, const char* k) {
            const MacroEntry* e = lookupLayered(&s, none, global, key(k), false);
            return e ? QString::fromStdString(e->command) +
                           (&s.entries.front() <= e && e <= &s.entries.back() ? "" : " (list)")
                     : QString("<none>");
        };
        QCOMPARE(layered(skinZ, "CTRL+Z"), QString("CTRL+Z (list)"));
        QCOMPARE(layered(skinZ, "CTRL+Q"), QString("Q (list)"));
        QCOMPARE(layered(skinCQ, "CTRL+SHIFT+Q"), QString("CTRL+Q"));
        QCOMPARE(layered(skinCQ, "Q"), QString("Q (list)"));
        QCOMPARE(layered(skinOff, "CTRL+Z"), QString("Z"));
        QCOMPARE(layered(skinOff, "Q"), QString("<none>"));
        QCOMPARE(layered(skinOff, "CTRL+Q"), QString("<none>"));
    }
    void storage() {
        QCOMPARE(QString::fromStdString(userMacrosPath("/s", "")), QString("/s/macros.dmf"));
        QCOMPARE(QString::fromStdString(worldKeyFor("127.0.0.1", 45000)), QString("127.0.0.1_45000"));
        QCOMPARE(QString::fromStdString(worldKeyFor("a/../b:c", 1)),
                 QString("a_.._b_c_1"));  // no path separators survive
        QCOMPARE(QString::fromStdString(userMacrosPath("/s", "h_1")), QString("/s/macros/h_1/macros.dmf"));
        QTemporaryDir d;
        QVERIFY(d.isValid());
        std::string p = userMacrosPath(d.path().toStdString(), worldKeyFor("127.0.0.1", 47915));
        QCOMPARE(int(loadUserMacros(p).entries.size()), 0);  // missing file = empty, not an error
        MacroSet s;
        editUserMacro(s, "", "F6", "hello");
        std::string err;
        QVERIFY2(saveUserMacros(p, s, &err), err.c_str());  // creates macros/<key>/
        MacroSet r = loadUserMacros(p);
        QCOMPARE(int(r.entries.size()), 1);
        QCOMPARE(QString::fromStdString(r.lookup(key("F6"), false)->command), QString("hello"));
        QVERIFY(!QFile::exists(QString::fromStdString(p + ".tmp")));
        QCOMPARE(int(parseUserMacros("window \"x\"\n\tjunk\n").entries.size()), 0);  // a damaged file reads as empty
    }
};

FUGUE_TEST_GUILESS_MAIN(UserMacros)
#include "usermacros_test.moc"
