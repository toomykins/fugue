// pager_listing: the hub listing parser, on a sample listing with invented names.
#include <QtTest>
#include "tests/testmain.h"
#include "pager/listing.h"
#include "pager/text.h"

using namespace fugue::pager;

static const char* kSpecimen =
    "general\n"
    "\ttype = \"game\"\n"
    "\ttitle = \"Space Station 13\"\n"
    "\tfans = 200136\n"
    "world/1\n"
    "\turl = \"byond://BYOND.world.1234567890\"\n"
    "\tserver_version = 516\n"
    "\tstatus = \"<a rel=\\\"nofollow\\\"><b>\\[EN] Example Station</b> &#8212; (</a><a "
    "href=\\\"https://example.invalid/\\\">Discord</a>)<br>\\nMAP: <b>Box, \\\"the\\\" map</b><br>\\nPlayers: "
    "<b>2</b>\"\n"
    "\tplayers = list(\"Some One\",\"Other, With Comma\")\n"
    "world/2\n"
    "\turl = \"byond://BYOND.world.42\"\n"
    "\tserver_version = 515\n"
    "\thost = \"Caf\xe9Host\"\n"
    "\tstatus = \"<b>Quiet</b>\"\n"
    "\tplayers = list()\n"
    "\tsome_future_field = list(1,2)\n";

class PagerListing : public QObject {
    Q_OBJECT
private slots:
    void specimen() {
        std::string err;
        auto w = parseListing(kSpecimen, &err);
        QVERIFY2(w, err.c_str());
        QCOMPARE(w->size(), size_t(2));
        const auto &a = (*w)[0], &b = (*w)[1];
        QCOMPARE(a.url, std::string("byond://BYOND.world.1234567890"));
        QCOMPARE(a.id, std::string("1234567890"));
        QCOMPARE(a.serverVersion, 516LL);
        QCOMPARE(a.players, (std::vector<std::string>{"Some One", "Other, With Comma"}));
        std::vector<std::string> want = {"[EN] Example Station \xe2\x80\x94 (Discord)", "MAP: Box, \"the\" map",
                                         "Players: 2"};
        QCOMPARE(a.lines(), want);
        QCOMPARE(a.title(), want[0]);
        QCOMPARE(b.host, std::string("Caf\xc3\xa9Host"));  // ISO-8859-1 0xE9 -> U+00E9
        QCOMPARE(b.players.size(), size_t(0));
        QCOMPARE(b.id, std::string("42"));
        QCOMPARE(b.title(), std::string("Quiet"));
    }
    void refusesOtherFormats() {
        std::string err;
        QVERIFY(!parseListing("\t<html><body>Service unavailable</body></html>", &err));
        QCOMPARE(err, std::string("hub listing: not in the hub's text format (46 bytes)"));
        QVERIFY(!parseListing("", &err));
        QVERIFY(parseListing("general\n"));  // a section with no worlds is "zero servers", not an error
    }
    void textHelpers() {
        QCOMPARE(trimSpace("\xc2\xa0 a b \xc2\xa0"), std::string("a b"));  // U+00A0 counts as space
        QCOMPARE(htmlUnescape("a&amp;b &#x41;&#8212;&nbsp;&bogus; &lt"),
                 std::string("a&b A\xe2\x80\x94\xc2\xa0&bogus; <"));
        long long v;
        QVERIFY(!goAtoi("12x", &v));
        QVERIFY(goAtoi("+80", &v) && v == 80);
        QCOMPARE(goQuote("a\"b\n"), std::string("\"a\\\"b\\n\""));
    }
};
FUGUE_TEST_APPLESS_MAIN(PagerListing)
#include "pager_listing_test.moc"
