// Round-trips the key.txt line-1 codec on synthesized blocks (encodeKeyLine1 / buildKeyBlob invert the decoder).
#include <QtTest>
#include "tests/testmain.h"
#include "account/desc.h"
#include "account/keyblock.h"

using namespace fugue;
using namespace fugue::account;

namespace {
Bytes pwBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
Bytes synthIdentity(size_t n, bool zeros) {
    Bytes id(n);
    for (size_t i = 0; i < n; i++) id[i] = uint8_t(0x30 + (i * 37 + 11) % 190);
    if (zeros && n > 8) {
        id[n - 6] = 0;
        id[3] = 0;
    }  // identities may contain 0x00
    return id;
}
}  // namespace

class AccountCodec : public QObject {
    Q_OBJECT
private slots:
    void roundTripsAtManyNameLengths() {
        int n = 0;
        for (size_t nameLen = 1; nameLen <= 30; nameLen++) {
            for (bool zeros : {false, true}) {
                std::string account(nameLen, 'A');
                std::string pw = "synthetic-pw-" + std::to_string(nameLen);
                Bytes id = synthIdentity(nameLen + 13, zeros);
                Bytes blob = buildKeyBlob(id, 0x00262781u + uint32_t(nameLen), 0xcccc37b5u, Bytes{0x11, 0x22, 0x33});
                QCOMPARE(blob.size(), nameLen + 27);
                std::string line1 = encodeKeyLine1(blob, pwordCryptLevel2(pwBytes(pw)));
                std::string err;
                auto f = decodeKeyBlock(line1, account, pwordCryptLevel2(pwBytes(pw)), &err);
                QVERIFY2(f, err.c_str());
                QVERIFY(f->identity == id);
                QCOMPARE(f->installId, 0x00262781u + uint32_t(nameLen));
                QCOMPARE(f->innerKey, 0xcccc37b5u);
                QVERIFY(f->haveHubIds);
                n++;
            }
        }
        qInfo() << "synthesized blocks round-tripped:" << n;
    }

    void wrongPasswordFailsFraming() {
        std::string account = "SyntheticName";
        Bytes id = synthIdentity(account.size() + 13, true);
        Bytes blob = buildKeyBlob(id, 0x1234, 0x5678, Bytes{1, 2, 3});
        std::string line1 = encodeKeyLine1(blob, pwordCryptLevel2(pwBytes("right")));
        QVERIFY(decodeKeyBlock(line1, account, pwordCryptLevel2(pwBytes("right"))));
        int rejected = 0;
        for (int i = 0; i < 200; i++) {
            std::string err;
            auto f = decodeKeyBlock(line1, account, pwordCryptLevel2(pwBytes("wrong" + std::to_string(i))), &err);
            if (!f) rejected++;
        }
        // The only cipher-key gate is the leading 0x00 of the stage-2 plaintext (~1/256 false-pass),
        // so nearly all wrong keys are rejected -- but NOT by a name-derived length check.
        QVERIFY2(rejected >= 190, std::to_string(rejected).c_str());
        // The block layout does NOT depend on the account name: decoding under a different name
        // recovers the SAME identity and credentials (this is the bug the old length check caused).
        std::string err;
        auto other = decodeKeyBlock(line1, "SomeOtherName", pwordCryptLevel2(pwBytes("right")), &err);
        QVERIFY2(other, err.c_str());
        QVERIFY(other->identity == id);
        QCOMPARE(other->installId, 0x1234u);
        // no cipher key, and a too-short block, are errors
        QVERIFY(!decodeKeyBlock(line1, account, "", &err) && err.find("no cipher key") != std::string::npos);
        QVERIFY(
            !decodeKeyBlock("xx\x5d!!!!", "SomeAccount", "0123456789abcdef0123456789abcdef", &err));  // truncated block
        QVERIFY(!keyBlockBlob("xx", "0123456789abcdef0123456789abcdef", &err) &&
                err.find("too short") != std::string::npos);
    }

    void zeroHubCredentialsAreRefused() {
        Bytes blob = buildKeyBlob(synthIdentity(20, false), 0, 0x55, Bytes{});
        std::string ks = pwordCryptLevel2(pwBytes("p"));
        std::string err;
        QVERIFY(!decodeKeyBlock(encodeKeyLine1(blob, ks), std::string(7, 'x'), ks, &err) &&
                err.find("zero") != std::string::npos);
    }

    void line1ExtractionIsCaseInsensitiveAndScoped() {
        const std::string body = "BEGIN KEY Guest\nGLINE\n`\nEND KEY\nBEGIN KEY ExamplePlayer\nTLINE\n`\nEND KEY\n";
        for (auto n : {"ExamplePlayer", "exampleplayer", "EXAMPLEPLAYER", "eXAmPlEpLaYeR"})
            QCOMPARE(*keyBlockLine1(body, n), std::string("TLINE"));
        std::string err;
        QVERIFY(!keyBlockLine1(body, "someoneelse", &err));
        QCOMPARE(*keyBlockLine1(body, "guest"), std::string("GLINE"));
        QCOMPARE(*keyBlockLine1("BEGIN KEY A\r\nCRLF  \r\n", "a"), std::string("CRLF"));
    }

    void pwordCryptLevels() {  // level 1 is 13 chars, level 2 is 32 hex chars, credential is level2;level1
        Bytes pw = pwordCryptLevelsPw();
        std::string l1 = pwordCrypt(pw), l2 = pwordCryptLevel2(pw);
        QCOMPARE(l1.size(), size_t(13));
        QVERIFY(l1.rfind("DM", 0) == 0);
        QCOMPARE(l2.size(), size_t(32));
        QCOMPARE(downloadKeyCredential(pw), l2 + ";" + l1);
        QCOMPARE(pwordCryptLevel2(pw), l2);
        QVERIFY(pwordCryptLevel2(pwBytes("hunter3")) != l2);
        // only the first 8 bytes matter for level 1, but level 2 hashes the whole password
        QCOMPARE(pwordCrypt(pwBytes("abcdefgh")), pwordCrypt(pwBytes("abcdefghXYZ")));
        QVERIFY(pwordCryptLevel2(pwBytes("abcdefgh")) != pwordCryptLevel2(pwBytes("abcdefghXYZ")));
    }

private:
    static Bytes pwordCryptLevelsPw() { return pwBytes("hunter2"); }
};

FUGUE_TEST_APPLESS_MAIN(AccountCodec)
#include "account_codec_test.moc"
