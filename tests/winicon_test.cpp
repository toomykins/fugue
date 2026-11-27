// Icon resolution (skin/winicon.h): which resource the main window's `icon` names, and when its bytes are here.
// The value forms follow stock DreamSeeker 516 and stock DD 516.1684. The resolver runs against a fake 0x000e list and store,
// and once against a real ResourcePump for the prioritize hand-off.
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include <map>
#include "net/bytes.h"
#include "net/resource.h"
#include "net/respump.h"
#include "proto/msg.h"
#include "skin/winicon.h"

using namespace fugue;
using namespace fugue::skin;

namespace {
// Fake state: `list` is the 0x000e list (ordinal -> id), `store` what is stored this run, `names` the on-disk store's recorded names (RscStore::nameOf).
struct Fake {
    std::vector<uint32_t> list;
    std::map<uint32_t, std::string> store, names;
    int reads = 0;
    IconResolver::Env env() {
        return {
            [this](uint32_t ord, uint32_t& id) {
                if (ord >= list.size()) return false;
                id = list[ord];
                return true;
            },
            [this](uint32_t id) { return store.count(id) != 0; },
            [this](uint32_t id, std::string& b) {
                reads++;
                auto it = store.find(id);
                if (it == store.end()) return false;
                b = it->second;
                return true;
            },
            [this](uint32_t id) {
                auto it = names.find(id);
                return it == names.end() ? std::string() : it->second;
            },
        };
    }
};
}  // namespace

class WinIconTest : public QObject {
    Q_OBJECT
private slots:
    // a skin's `icon = 'icons\\ss13_64.png'` and the .rsc's own name for the file (backslash, as DreamMaker stores it).
    void fullNameResolves() {
        Fake f;
        f.list = {0x11, 0x22};
        f.store[0x22] = "PNG-ss13";
        IconResolver r(f.env());
        r.noteResource(0x11, "icons\\other.png");
        r.noteResource(0x22, "icons\\ss13_64.png");
        IconResolver::Out o;
        QVERIFY(r.update("icons\\ss13_64.png", o));
        QCOMPARE(o.bytes, std::string("PNG-ss13"));
        QCOMPARE(o.name, std::string("icons\\ss13_64.png"));
        QVERIFY(r.resolved());
        QVERIFY(!r.update("icons\\ss13_64.png", o));  // resolved: nothing to do until the value changes
        QVERIFY(r.takeWanted().empty());
    }
    // A name with a directory, and a file stored under its basename only (and the reverse); case-blind.
    void basenameOnly() {
        Fake f;
        f.store[0x33] = "B";
        IconResolver r(f.env());
        r.noteResource(0x33, "ss13_64.png");
        IconResolver::Out o;
        QVERIFY(r.update("icons\\SS13_64.PNG", o));
        QCOMPARE(o.bytes, std::string("B"));

        Fake g;
        g.store[0x44] = "C";
        IconResolver q(g.env());
        q.noteResource(0x44, "icons/ui/tg_32.png");
        QVERIFY(q.update("tg_32.png", o));
        QCOMPARE(o.bytes, std::string("C"));
    }
    // '\' (DreamMaker form) and '/' are one separator; a full match beats another directory's same basename.
    void backslashVersusSlash() {
        Fake f;
        f.store[0x51] = "wrong";
        f.store[0x52] = "right";
        IconResolver r(f.env());
        r.noteResource(0x51, "other/ss13_64.png");
        r.noteResource(0x52, "icons/ss13_64.png");
        IconResolver::Out o;
        QVERIFY(r.update("icons\\ss13_64.png", o));
        QCOMPARE(o.bytes, std::string("right"));
        QCOMPARE(r.idFor(parseIconRef("ss13_64.png")), 0x51u);  // the basename key keeps the first file seen
        QCOMPARE(r.idFor(parseIconRef("x.dm")), 0u);            // only image files are indexed
    }
    // `[0xc0000NN]`: NN is the 0-based ordinal, so [0xc000003] is the list's 4th entry.
    void ordinalIsZeroBased() {
        Fake f;
        f.list = {0xa0, 0xa1, 0xa2, 0xa3};
        f.store[0xa3] = "green";
        f.store[0xa2] = "not-me";
        IconResolver r(f.env());
        IconResolver::Out o;
        QVERIFY(r.update("[0xc000003]", o));
        QCOMPARE(o.bytes, std::string("green"));
        QVERIFY(r.update("[0xc000004]", o));  // past the end: not an error, not here yet
        QVERIFY(o.bytes.empty());
        QVERIFY(!o.none);
        QVERIFY(!r.resolved());
        f.list.push_back(0xa4);  // the list grows (a later 0x000e) and the file lands
        f.store[0xa4] = "late";
        QVERIFY(r.update("[0xc000004]", o));
        QCOMPARE(o.bytes, std::string("late"));
    }
    // The skin names a file that has not arrived: announce "not here yet" once, then stay quiet until it lands.
    void notHereYetThenArrives() {
        Fake f;
        IconResolver r(f.env());
        IconResolver::Out o;
        QVERIFY(r.update("icons\\ss13_64.png", o));
        QVERIFY(o.bytes.empty() && !o.none);
        QVERIFY(!r.update("icons\\ss13_64.png", o));
        QVERIFY(!r.update("icons\\ss13_64.png", o));
        QVERIFY(r.takeWanted().empty());  // no id is known for a NAME before its offer
        f.store[0x77] = "ss13";
        r.noteResource(0x77, "icons\\ss13_64.png");
        QVERIFY(r.update("icons\\ss13_64.png", o));
        QCOMPARE(o.bytes, std::string("ss13"));
        QVERIFY(r.resolved());
        QVERIFY(r.update("", o));  // empty winset: back to none
        QVERIFY(o.none && o.bytes.empty());
        QVERIFY(!r.update("", o));
    }
    // A warm join reports a cached file with no name; the on-disk store recorded it.
    void warmJoinNameFromTheStore() {
        Fake f;
        f.store[0x88] = "cached";
        f.names[0x88] = "icons\\ss13_64.png";
        IconResolver r(f.env());
        r.noteResource(0x88, "");
        IconResolver::Out o;
        QVERIFY(r.update("'icons\\ss13_64.png'", o));  // a winset's quoted form, too
        QCOMPARE(o.bytes, std::string("cached"));
    }
    // A known id that is not stored yet is wanted once (ResourcePump::prioritize), not every tick.
    void wantedOncePerId() {
        Fake f;
        f.list = {0x1, 0x2, 0x3};
        IconResolver r(f.env());
        IconResolver::Out o;
        r.update("[0xc000002]", o);
        QCOMPARE(r.takeWanted(), std::vector<uint32_t>{0x3});
        r.update("[0xc000002]", o);
        r.update("[0xc000002]", o);
        QVERIFY(r.takeWanted().empty());
        QCOMPARE(f.reads, 0);  // bytes are read only once the file is stored
    }

    // The icon's file can be the last of many more entries than the in-flight window; without prioritize it would wait
    // behind all of them. With it, it is declared at once.
    void wantedIdJumpsTheRealQueue() {
        QTemporaryDir td;
        std::vector<std::pair<uint16_t, Bytes>> sent;
        net::ResourcePump p(
            td.path().toStdString() + "/rsc",
            [&](uint16_t t, ByteSpan b) {
                sent.push_back({t, Bytes(b.begin(), b.end())});
                return true;
            },
            [](const std::string&) {});
        std::vector<Bytes> files;
        std::vector<uint32_t> ids;
        Bytes list;
        for (int i = 0; i < 200; i++) {
            Bytes b(64);
            for (size_t k = 0; k < b.size(); k++) b[k] = uint8_t(k * 7 + i * 13 + 1);
            files.push_back(b);
            ids.push_back(net::resId(b));
            putLe32(list, ids.back());
            list.push_back(6);
        }
        p.onResList(list);
        IconResolver r(IconResolver::Env{
            [&](uint32_t ord, uint32_t& id) {
                if (ord >= ids.size()) return false;
                id = ids[ord];
                return true;
            },
            [&](uint32_t id) { return p.storedThisRun().count(id) != 0; },
            [&](uint32_t id, std::string& out) {
                Bytes b;
                if (!p.readStored(id, b)) return false;
                out.assign(b.begin(), b.end());
                return true;
            },
            nullptr,
        });
        IconResolver::Out o;
        QVERIFY(r.update("[0xc0000c7]", o));  // ordinal 199
        QVERIFY(o.bytes.empty());
        p.prioritize(r.takeWanted());
        auto serve = [&](const Bytes& f, const std::string& name) {
            uint32_t id = net::resId(f);
            Bytes of;
            putLe32(of, id);
            putLe32(of, 0xffffffffu);
            putLe32(of, 0);
            putLe32(of, 2);
            of.push_back(1);
            putLe32(of, uint32_t(f.size()));
            of.push_back(2);
            of.insert(of.end(), name.begin(), name.end());
            of.push_back(0);
            const std::string prm = "##action=load_rsc";
            of.insert(of.end(), prm.begin(), prm.end());
            of.push_back(0);
            p.onFrame(of);
            Bytes d;
            putLe32(d, id);
            putLe32(d, 0xffffffffu);
            putLe32(d, 1);
            putLe32(d, 2);
            putLe32(d, 0);
            Bytes enc = net::encodeResource(f, id);
            d.insert(d.end(), enc.begin(), enc.end());
            p.onFrame(d);
        };
        // Declared at once, before any transfer completes (its own urgent batch).
        Bytes last;
        for (auto& s : sent)
            if (s.first == C2S::ResourceDeclare) last = s.second;
        Bytes ord199;
        putLe32(ord199, 199);
        QCOMPARE(last, ord199);
        // ... and once served, the resolver finds it on the next tick.
        serve(files[199], "icon.png");
        QVERIFY(r.update("[0xc0000c7]", o));
        QCOMPARE(o.bytes, std::string(files[199].begin(), files[199].end()));
    }
};

FUGUE_TEST_APPLESS_MAIN(WinIconTest)
#include "winicon_test.moc"
