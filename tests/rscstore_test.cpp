// On-disk resource cache (net/rscstore) and the warm-join behaviour of the pump built on it.
// Checked: layout and mode bits, temp+rename atomicity, refuse-on-put, delete-on-bad-read, the name log, and declaring only what the cache lacks.
#include <sys/stat.h>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include <filesystem>
#include <fstream>
#include "net/bytes.h"
#include "net/inflate.h"
#include "net/resource.h"
#include "net/respump.h"
#include "net/rscstore.h"
#include "proto/msg.h"

using namespace fugue;
using namespace fugue::net;
namespace fs = std::filesystem;

namespace {
Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }
Bytes bigFile(int seed, size_t n = 3000) {
    Bytes b(n);
    for (size_t i = 0; i < n; i++) b[i] = uint8_t(i * 31 + seed + (i >> 7));
    return b;
}
std::string slurpS(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
unsigned modeOf(const std::string& p) {
    struct stat sb;
    return ::stat(p.c_str(), &sb) == 0 ? (sb.st_mode & 07777) : 0;
}
Bytes listOf(std::initializer_list<uint32_t> ids) {
    Bytes b;
    for (uint32_t id : ids) {
        putLe32(b, id);
        b.push_back(2);
    }
    return b;
}
Bytes offerFrame(uint32_t rid, uint32_t total, uint32_t size, const std::string& name, const std::string& params) {
    Bytes b;
    putLe32(b, rid);
    putLe32(b, 0xffffffffu);
    putLe32(b, 0);
    putLe32(b, total);
    b.push_back(1);
    putLe32(b, size);
    b.push_back(2);
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(0);
    b.insert(b.end(), params.begin(), params.end());
    b.push_back(0);
    return b;
}
Bytes dataFrame(uint32_t rid, uint32_t pkt, uint32_t total, uint32_t offset, ByteSpan payload) {
    Bytes b;
    putLe32(b, rid);
    putLe32(b, 0xffffffffu);
    putLe32(b, pkt);
    putLe32(b, total);
    putLe32(b, offset);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
struct Rig {
    std::vector<std::pair<uint16_t, Bytes>> sent;
    std::unique_ptr<ResourcePump> p;
    explicit Rig(const std::string& dir) {
        p = std::make_unique<ResourcePump>(
            dir,
            [this](uint16_t t, ByteSpan b) {
                sent.push_back({t, Bytes(b.begin(), b.end())});
                return true;
            },
            [](const std::string&) {});
    }
    int count(uint16_t t) const {
        int n = 0;
        for (auto& s : sent)
            if (s.first == t) n++;
        return n;
    }
    // answer the oldest declaration: offer + data for `file` named `name`
    void serve(const Bytes& file, const std::string& name) {
        uint32_t id = resId(file);
        p->onFrame(offerFrame(id, 2, uint32_t(file.size()), name, "##action=load_rsc"));
        p->onFrame(dataFrame(id, 1, 2, 0, encodeResource(file, id)));
    }
    // A load_rsc gzip bundle: one ciphered record per declared id,
    // with a header id that is none of them; the gzip stream uses stored blocks.
    Bytes bundleBytes(uint32_t rid, const std::vector<std::pair<const Bytes*, uint32_t>>& recs) {
        Bytes u;
        for (auto& [file, key] : recs) {
            Bytes enc = encodeResource(*file, key);
            Bytes rr;
            rr.push_back(1);
            putLe32(rr, key ^ 0x5a5a5a5au);
            while (rr.size() < 13) rr.push_back(0);
            putLe32(rr, uint32_t(enc.size()));
            rr.push_back('f');
            rr.push_back(0);
            rr.insert(rr.end(), enc.begin(), enc.end());
            putLe32(u, uint32_t(rr.size()));
            u.push_back(0x83);
            u.insert(u.end(), rr.begin(), rr.end());
        }
        Bytes gz = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
        for (size_t off = 0; off < u.size() || off == 0; off += 65535) {
            size_t n = std::min<size_t>(65535, u.size() - off);
            gz.push_back(off + n >= u.size() ? 1 : 0);
            putLe16(gz, uint16_t(n));
            putLe16(gz, uint16_t(~n));
            gz.insert(gz.end(), u.begin() + long(off), u.begin() + long(off + n));
            if (off + n >= u.size()) break;
        }
        putLe32(gz, crc32Gzip(u));
        putLe32(gz, uint32_t(u.size()));
        return encodeResource(gz, rid);
    }
    void serveBundle(uint32_t rid, const std::vector<std::pair<const Bytes*, uint32_t>>& recs) {
        Bytes b = bundleBytes(rid, recs);
        p->onFrame(offerFrame(rid, 2, uint32_t(b.size()), "resources", "##action=load_rsc"));
        p->onFrame(dataFrame(rid, 1, 2, 0, b));
    }
};
}  // namespace

class RscStoreTest : public QObject {
    Q_OBJECT
private slots:
    void layoutIsShardedById() {
        QTemporaryDir td;
        std::string d = td.path().toStdString() + "/rsc";
        Bytes f = bigFile(1);
        uint32_t id = resId(f);
        {
            RscStore s(d);
            QVERIFY(s.ok());
            QVERIFY(s.put(id, "icons/a.dmi", f));
            QVERIFY(!s.put(id, "", {}));  // empty: not stored
        }
        char want[512];
        std::snprintf(want, sizeof want, "%s/%02x/%08x", d.c_str(), unsigned(id >> 24), unsigned(id));
        QCOMPARE(RscStore::pathFor(d, id), std::string(want));
        QCOMPARE(slurpS(want), std::string(f.begin(), f.end()));  // the file is the raw bytes and nothing else
#ifndef _WIN32  // Cache files inherit the profile ACL on Windows; CRT mode bits are not Unix permissions.
        QCOMPARE(modeOf(want), 0600u);
        QCOMPARE(modeOf(std::string(want).substr(0, std::string(want).rfind('/'))), 0700u);
        QCOMPARE(modeOf(d + "/names"), 0600u);
#endif
        char nl[256];
        std::snprintf(nl, sizeof nl, "%08x\ticons/a.dmi\n", unsigned(id));
        QCOMPARE(slurpS(d + "/names"), std::string(nl));
        for (auto& e : fs::directory_iterator(fs::path(want).parent_path()))
            QVERIFY2(e.path().filename().string().rfind(".tmp-", 0) != 0, "temp file left behind");
        // fixed vector: pins the shard rule independent of the implementation (id 0xdeadbeef -> de/deadbeef)
        QCOMPARE(RscStore::pathFor("/x", 0xdeadbeefu), std::string("/x/de/deadbeef"));
        QCOMPARE(RscStore::pathFor("/x", 0x0000000au), std::string("/x/00/0000000a"));
    }

    void put_refuses_bytes_that_do_not_hash() {
        QTemporaryDir td;
        RscStore s(td.path().toStdString());
        Bytes f = bigFile(2), g = bigFile(3);
        QVERIFY(!s.put(resId(f), "x", g));  // wrong bytes for the id
        QVERIFY(!s.has(resId(f)));
        QCOMPARE(s.stats().rejected, uint64_t(1));
        QVERIFY(!fs::exists(RscStore::pathFor(td.path().toStdString(), resId(f))));
        QVERIFY(!fs::exists(RscStore::pathFor(td.path().toStdString(), resId(g))));
    }

    void tampered_file_is_refused_and_deleted_on_read() {
        QTemporaryDir td;
        std::string d = td.path().toStdString();
        Bytes f = bigFile(4);
        uint32_t id = resId(f);
        std::string p = RscStore::pathFor(d, id);
        {
            RscStore s(d);
            QVERIFY(s.put(id, "t.dmi", f));
        }
        {  // flip one byte on disk (a foreign writer / bit rot)
            std::fstream io(p, std::ios::in | std::ios::out | std::ios::binary);
            io.seekp(100);
            char c = char(f[100] ^ 0x5a);
            io.write(&c, 1);
        }
        RscStore s(d);  // reopen: indexed by stat only, so it looks present ...
        QVERIFY(s.has(id));
        Bytes out;
        QVERIFY(!s.get(id, out));  // ... and the read-time re-hash refuses it
        QVERIFY2(!fs::exists(p), "a failing file must be deleted");
        QVERIFY(!s.has(id));
        QCOMPARE(s.stats().deletedBad, uint64_t(1));
        // truncated file: same
        QVERIFY(s.put(id, "t.dmi", f));
        fs::resize_file(p, 10);
        RscStore s2(d);
        QVERIFY(!s2.get(id, out));
        QVERIFY(!fs::exists(p));
    }

    void reopen_reads_names_and_ignores_foreign_files() {
        QTemporaryDir td;
        std::string d = td.path().toStdString();
        Bytes f = bigFile(5);
        uint32_t id = resId(f);
        {
            RscStore s(d);
            QVERIFY(s.put(id, "", f));
            QVERIFY(!s.hasName(id));
            QVERIFY(s.put(id, "late.png", f));
        }
        fs::create_directories(d + "/zz");
        std::ofstream(d + "/zz/00000001") << "x";  // not a shard
        fs::create_directories(d + "/ab");
        std::ofstream(d + "/ab/notanid") << "x";  // not an id
        std::ofstream(d + "/notes.txt") << "mine";
        RscStore s(d);
        QVERIFY(s.hasName(id));
        Bytes out;
        std::string nm;
        QVERIFY(s.get(id, out, &nm));
        QCOMPARE(nm, std::string("late.png"));
        QCOMPARE(s.stats().files, uint64_t(1));
        QVERIFY(fs::exists(d + "/zz/00000001") && fs::exists(d + "/ab/notanid") &&
                fs::exists(d + "/notes.txt"));  // never deleted
        // sanitised name: a tab/newline in a server-controlled name cannot corrupt the log
        Bytes g = bigFile(6);
        QVERIFY(s.put(resId(g), "a\tb\nc", g));
        RscStore s2(d);
        QVERIFY(s2.get(resId(g), out, &nm));
        QCOMPARE(nm, std::string("a b c"));
    }

    void eviction_trims_to_90_percent_lru() {
        QTemporaryDir td;
        std::string d = td.path().toStdString();
        RscStore s(d, 10000);
        std::vector<uint32_t> ids;
        for (int i = 0; i < 5; i++) {  // 5 x 3000 = 15000 > cap 10000 -> evicts down to <= 9000
            Bytes f = bigFile(10 + i);
            ids.push_back(resId(f));
            QVERIFY(s.put(ids.back(), "", f));
        }
        QVERIFY(s.stats().bytes <= 9000);
        QVERIFY(s.stats().evicted >= 1);
    }

    // ---- pump on top of the store ----
    void cold_declares_all_then_warm_declares_none() {
        QTemporaryDir td;
        std::string d = td.path().toStdString() + "/rsc";
        Bytes a = bigFile(20), b = bigFile(21);
        {
            Rig cold(d);
            cold.p->onResList(listOf({resId(a), resId(b)}));
            QCOMPARE(cold.count(C2S::ResourceDeclare), 1);    // one batch
            QCOMPARE(cold.p->stats().declared, uint64_t(2));  // both ordinals
            cold.serve(a, "a.dmi");
            cold.serve(b, "b.ogg");
            QCOMPARE(cold.p->stats().verified, uint64_t(2));
        }
        Rig warm(d);
        warm.p->onResList(listOf({resId(a), resId(b), 0x12345678u}));
        QCOMPARE(warm.count(C2S::ResourceDeclare), 1);  // only the id the cache lacks
        QCOMPARE(warm.sent[0].second,
                 (Bytes{2, 0, 0, 0}));  // ORDINAL stays the list index (the cache hits are 0 and 1)
        QCOMPARE(warm.p->stats().cacheServed, uint64_t(2));
        QCOMPARE(warm.p->storedCount(), size_t(2));  // reported to the session as already held
        Bytes out;
        QVERIFY(warm.p->readStored(resId(a), out));
        QCOMPARE(out, a);
        QCOMPARE(warm.p->storedThisRun().at(resId(a)), std::string("a.dmi"));  // the cached name fills the blank
    }

    // Declarations go out in batches, and one load_rsc offer answers a whole batch.
    // The batch length follows the bytes the last bundles carried, and only one background batch
    // waits at DD behind the one being answered.
    void declarations_are_batched_by_bytes_one_queued() {
        QTemporaryDir td;
        Rig r(td.path().toStdString() + "/rsc");
        Bytes list;
        for (int i = 0; i < 400; i++) {
            putLe32(list, 0x20000000u + uint32_t(i) * 7919u);
            list.push_back(3);
        }
        r.p->onResList(list);
        QCOMPARE(r.count(C2S::ResourceDeclare), 1);        // one batch at DD, nothing measured yet
        QCOMPARE(r.sent[0].second.size(), size_t(8 * 4));  // kFirstBatch ordinals, 0..7
        QCOMPARE(le32(&r.sent[0].second[0]), 0u);
        QCOMPARE(r.p->inFlight(), size_t(8));
        // Its offer arrives: that batch is being answered, so the next one is declared to wait behind it, and no more.
        r.p->onFrame(offerFrame(0x51, 2, 8 * 2000, "resources", "##action=load_rsc"));
        QCOMPARE(r.count(C2S::ResourceDeclare), 2);
        QCOMPARE(r.p->inFlight(), size_t(8));
        r.p->tick();
        QCOMPARE(r.count(C2S::ResourceDeclare), 2);
        r.p->onFrame(dataFrame(0x51, 1, 2, 0, bigFile(1, 8 * 2000)));  // 2000 bytes per ordinal
        QCOMPARE(r.p->nextBackgroundBatch(), size_t(32));              // 1 MiB / 2000 B -> capped at kBatch
        r.serve(bigFile(2, 8 * 256 * 1024), "resources");              // the second batch: 256 KB per ordinal
        QCOMPARE(r.p->nextBackgroundBatch(), size_t(7));               // EWMA ~129 KB/ordinal -> 1 MiB holds 7 more
        QCOMPARE(r.count(C2S::ResourceDeclare), 3);
        QCOMPARE(r.sent.back().second.size(), size_t(32 * 4));  // declared before that bundle landed, at 32
        r.serve(bigFile(3, 4 << 20), "resources");              // that 32-batch carried 4 MiB
        QCOMPARE(r.sent.back().second.size(), size_t(7 * 4));   // the batch declared behind it holds 7, not 32
        QCOMPARE(r.p->inFlight(), size_t(7));                   // still exactly one background batch at DD
    }

    // What the client needs now jumps the queue (ResourcePump::prioritize): a title-screen icon, a lobby song, a page's browse_rsc asset.
    // These can be the last entries of a long list. The demanded id is declared at once, ahead of everything queued,
    // without waiting for a background slot. DD then holds ahead of it only the bundle it is sending and one background batch.
    void demanded_id_jumps_ahead_of_9000_queued() {
        QTemporaryDir td;
        Rig r(td.path().toStdString() + "/rsc");
        std::vector<uint32_t> ids;
        Bytes list;
        for (uint32_t i = 0; i < 9000; i++) {
            ids.push_back(0x10000000u + i * 7919u);
            putLe32(list, ids.back());
            list.push_back(3);
        }
        r.p->onResList(list);
        r.p->onFrame(offerFrame(0x61, 2, 1000, "resources", "##action=load_rsc"));  // batch 1 is being answered
        QCOMPARE(r.count(C2S::ResourceDeclare), 2);                                 // batch 2 waits behind it
        QCOMPARE(r.p->queued(), size_t(9000 - 16));
        const int before = r.count(C2S::ResourceDeclare);
        r.p->prioritize({ids[8999], ids[5000], 0x99999999u});  // two queued entries, plus an id the list never offered
        QCOMPARE(r.count(C2S::ResourceDeclare), before + 1);   // sent NOW: no transfer has completed
        Bytes want;
        putLe32(want, 5000);
        putLe32(want, 8999);  // list order among the demanded
        QCOMPARE(r.sent.back().second, want);
        QCOMPARE(r.p->queued(), size_t(9000 - 18));
        r.p->prioritize({ids[8999]});  // already declared: ignored, nothing re-sent
        QCOMPARE(r.count(C2S::ResourceDeclare), before + 1);
        // demands have their own window, so a burst still goes out in batches
        std::vector<uint32_t> burst(ids.begin() + 1000, ids.begin() + 1300);
        r.p->prioritize(burst);
        QCOMPARE(r.count(C2S::ResourceDeclare), before + 1 + 3);  // 4 urgent frames open at most (one already)
        QCOMPARE(le32(&r.sent.back().second[0]), 1064u);          // 1000..1031, 1032..1063, 1064..1095
        // While demands are queued the background fill waits behind them: the offer for the background frame DD holds sends nothing;
        // the offer for the first demanded frame sends the next demanded one.
        const int afterBurst = r.count(C2S::ResourceDeclare);
        r.p->onFrame(dataFrame(0x61, 1, 2, 0, bigFile(9, 1000)));  // batch 1's bytes
        r.serve(bigFile(10), "resources");                         // batch 2
        QCOMPARE(r.count(C2S::ResourceDeclare), afterBurst);
        r.serve(bigFile(20), "resources");  // the [5000, 8999] frame
        QCOMPARE(r.count(C2S::ResourceDeclare), afterBurst + 1);
        QCOMPARE(le32(&r.sent.back().second[0]), 1096u);
        QCOMPARE(r.p->queued(), size_t(9000 - 18 - 128));
    }

    // A batch's gzip bundle holds one record per declared id, each ciphered under its own 0x000e id, which the record header does not carry.
    // Every id of the batch must be tried, not just the batch's first id and the record id. A wrong key must never store a wrong file.
    void ciphered_bundle_records_are_keyed_by_their_own_ids() {
        QTemporaryDir td;
        Rig r(td.path().toStdString() + "/rsc");
        std::vector<Bytes> files;
        std::vector<uint32_t> ids;
        for (int i = 0; i < 8; i++) {
            files.push_back(bigFile(300 + i, 700 + size_t(i)));
            ids.push_back(resId(files.back()));
        }
        Bytes list;
        for (uint32_t id : ids) {
            putLe32(list, id);
            list.push_back(3);
        }
        r.p->onResList(list);
        QCOMPARE(r.count(C2S::ResourceDeclare), 1);
        std::vector<std::pair<const Bytes*, uint32_t>> recs;
        for (int i : {3, 0, 7, 1, 6, 2, 5, 4})
            recs.push_back({&files[size_t(i)], ids[size_t(i)]});  // NOT declaration order
        r.serveBundle(0x700, recs);
        QCOMPARE(r.p->stats().bundles, uint64_t(1));
        QCOMPARE(r.p->stats().verified, uint64_t(8));
        QCOMPARE(r.p->stats().unverified, uint64_t(0));
        for (size_t i = 0; i < 8; i++) {
            Bytes got;
            QVERIFY(r.p->readStored(ids[i], got));
            QCOMPARE(got, files[i]);
        }
    }

    // An expired declaration frees its window slot but stays in the order: DD's queue is serial, so a batch waiting behind
    // a big bundle is answered late, not never. Dropping it would misattribute later offers.
    void expired_batch_answered_late_keeps_later_offers_verifying() {
        QTemporaryDir td;
        Rig r(td.path().toStdString() + "/rsc");
        uint64_t now = 1000000;
        r.p->setClockForTest([&now] { return now; });
        std::vector<Bytes> files;
        std::vector<uint32_t> ids;
        for (int i = 0; i < 24; i++) {
            files.push_back(bigFile(500 + i, 600 + size_t(i)));
            ids.push_back(resId(files.back()));
        }
        Bytes list;
        for (uint32_t id : ids) {
            putLe32(list, id);
            list.push_back(3);
        }
        r.p->onResList(list);  // batch A = 0..7 declared
        auto recsOf = [&](int from) {
            std::vector<std::pair<const Bytes*, uint32_t>> v;
            for (int i = from; i < from + 8; i++) v.push_back({&files[size_t(i)], ids[size_t(i)]});
            return v;
        };
        // A's offer arrives and its (big) bundle starts streaming; B = 8..15 is declared behind it
        r.p->onFrame(offerFrame(0xA00, 2, 0, "resources", "##action=load_rsc"));
        QCOMPARE(r.count(C2S::ResourceDeclare), 2);
        now += 31000;  // A is still streaming: B passes the expiry
        r.p->tick();
        QCOMPARE(r.p->stats().declExpired, uint64_t(8));
        QCOMPARE(r.count(C2S::ResourceDeclare), 3);  // its slot is free: C = 16..23 goes out
        r.p->onFrame(dataFrame(0xA00, 1, 2, 0, r.bundleBytes(0xA00, recsOf(0))));  // A lands
        r.serveBundle(0xB00, recsOf(8));  // DD answers B after all - late, not never
        r.serveBundle(0xC00, recsOf(16));
        QCOMPARE(r.p->stats().verified, uint64_t(24));
        QCOMPARE(r.p->stats().unverified, uint64_t(0));
        QCOMPARE(r.p->stats().offerMismatch, uint64_t(0));  // no offer was guessed onto the wrong batch
        QCOMPARE(r.p->stats().declExpired, uint64_t(0));    // answered: no longer counted as expired
        QCOMPARE(r.p->outstandingBatches(), size_t(0));
        QCOMPARE(r.p->pending(), size_t(0));
    }

    // If DD answers PAST a batch (content names a later one), the guess is corrected and the skipped batch declared again.
    void offer_matched_by_content_not_position() {
        QTemporaryDir td;
        Rig r(td.path().toStdString() + "/rsc");
        std::vector<Bytes> files;
        std::vector<uint32_t> ids;
        for (int i = 0; i < 16; i++) {
            files.push_back(bigFile(700 + i, 500 + size_t(i)));
            ids.push_back(resId(files.back()));
        }
        Bytes list;
        for (uint32_t id : ids) {
            putLe32(list, id);
            list.push_back(3);
        }
        r.p->onResList(list);  // A = 0..7
        r.p->onFrame(offerFrame(0x1, 2, 0, "resources", "##action=load_rsc"));
        QCOMPARE(r.count(C2S::ResourceDeclare), 2);  // B = 8..15
        std::vector<std::pair<const Bytes*, uint32_t>> b;
        for (int i = 8; i < 16; i++) b.push_back({&files[size_t(i)], ids[size_t(i)]});
        r.p->onFrame(dataFrame(0x1, 1, 2, 0, r.bundleBytes(0x1, b)));  // the offer guessed A, but the bytes are B's
        QCOMPARE(r.p->stats().verified, uint64_t(8));
        QCOMPARE(r.p->stats().unverified, uint64_t(0));
        QCOMPARE(r.p->stats().offerMismatch, uint64_t(1));
        QCOMPARE(r.p->stats().declSkipped, uint64_t(8));  // A will never be answered: declared again
        QCOMPARE(r.count(C2S::ResourceDeclare), 3);
        QCOMPARE(le32(&r.sent.back().second[0]), 0u);
        std::vector<std::pair<const Bytes*, uint32_t>> a;
        for (int i = 0; i < 8; i++) a.push_back({&files[size_t(i)], ids[size_t(i)]});
        r.serveBundle(0x2, a);
        QCOMPARE(r.p->stats().verified, uint64_t(16));
        QCOMPARE(r.p->pending(), size_t(0));
    }
    void nameless_cached_entry_is_still_declared() {
        QTemporaryDir td;
        std::string d = td.path().toStdString() + "/rsc";
        Bytes a = bigFile(22);
        {
            Rig cold(d);
            cold.p->onResList(listOf({resId(a)}));
            cold.serve(a, "");  // a browse_rsc(file(x)) asset: the offer carries no name
        }
        Rig warm(d);
        warm.p->onResList(listOf({resId(a)}));
        QCOMPARE(warm.count(C2S::ResourceDeclare),
                 1);  // bytes cached but nameless: the offer is the only source of the name
        QCOMPARE(warm.p->stats().cacheNameless, uint64_t(1));
        QCOMPARE(warm.p->stats().cacheServed, uint64_t(0));
        warm.serve(a, "panel.css");
        QCOMPARE(warm.p->stats().alreadyHeld, uint64_t(1));
        QCOMPARE(warm.p->storedThisRun().at(resId(a)), std::string("panel.css"));
        Rig warm2(d);  // the name is learned for the NEXT join
        warm2.p->onResList(listOf({resId(a)}));
        QCOMPARE(warm2.count(C2S::ResourceDeclare), 0);
    }

    void corrupted_cache_entry_falls_back_to_the_wire() {
        QTemporaryDir td;
        std::string d = td.path().toStdString() + "/rsc";
        Bytes a = bigFile(23);
        {
            Rig cold(d);
            cold.p->onResList(listOf({resId(a)}));
            cold.serve(a, "a.dmi");
        }
        fs::resize_file(RscStore::pathFor(d, resId(a)), 7);  // damage after the list skipped it
        Rig warm(d);
        warm.p->onResList(listOf({resId(a)}));
        QCOMPARE(warm.count(C2S::ResourceDeclare), 0);  // optimistic Has: skipped
        Bytes out;
        QVERIFY(!warm.p->readStored(resId(a), out));    // the verified read refuses and deletes it, and
        QCOMPARE(warm.count(C2S::ResourceDeclare), 1);  // re-declares (re-resolve, never stuck)
        QCOMPARE(warm.p->stats().cacheRedeclared, uint64_t(1));
        QVERIFY(!fs::exists(RscStore::pathFor(d, resId(a))));
        warm.serve(a, "a.dmi");
        QVERIFY(warm.p->readStored(resId(a), out));
        QCOMPARE(out, a);
    }

    void bytes_that_do_not_hash_never_reach_the_store() {
        QTemporaryDir td;
        std::string d = td.path().toStdString() + "/rsc";
        Bytes f = bigFile(24), wrong = bigFile(25);
        Rig r(d);
        r.p->onResList(listOf({resId(f)}));
        r.p->onFrame(offerFrame(resId(f), 2, uint32_t(wrong.size()), "x.dmi", "##action=load_rsc"));
        r.p->onFrame(dataFrame(resId(f), 1, 2, 0, encodeResource(wrong, resId(f))));
        QCOMPARE(r.p->stats().unverified, uint64_t(1));
        uint64_t files, bytes;
        RscStore::usage(d, files, bytes);
        QCOMPARE(files, uint64_t(0));
    }
};

FUGUE_TEST_APPLESS_MAIN(RscStoreTest)
#include "rscstore_test.moc"
