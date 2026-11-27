// Browse-page handshakes that a client with no web view answers (model/headlessacks.h).
#include <QtTest>
#include "tests/testmain.h"
#include "model/headlessacks.h"

using namespace fugue::model;

class HeadlessAcksTest : public QObject {
    Q_OBJECT
private slots:
    void assetCachePageYieldsItsLiteralHref() {
        auto h = navShimHref("<script>window.location.href='byond://?asset_cache_confirm_arrival=7'</script>");
        QVERIFY(h);
        QCOMPARE(*h, std::string("byond://?asset_cache_confirm_arrival=7"));
    }
    void computedOrConditionalNavigationIsRefused() {
        // A page that guards or concatenates its navigation target has no static answer.
        QVERIFY(!navShimHref(
            "<script>try { window.location = 'byond://?a=' + document.body.getAttribute('x'); } catch(e){}</script>"));
        QVERIFY(!navShimHref("<script>location.href = 'byond://?a=' + q;</script>"));
        QVERIFY(!navShimHref("<script>location = 'http://example.com/';</script>"));  // not a byond destination
        QVERIFY(!navShimHref("<p>no script at all</p>"));
    }
    void collectAnswersEachHrefOnceAndGoonchatOnce() {
        HeadlessAcks a;
        std::map<std::string, std::string> w;
        QVERIFY(a.collect(w).empty());
        w["browseroutput"] = "<html>chat</html>";
        w["asset_cache_browser"] = "<script>window.location.href='byond://?asset_cache_confirm_arrival=1'</script>";
        auto r = a.collect(w);
        QCOMPARE(r.size(), size_t(2));
        QCOMPARE(r[0], std::string("?_src_=chat&proc=doneLoading"));
        QCOMPARE(r[1], std::string("byond://?asset_cache_confirm_arrival=1"));
        QVERIFY(a.collect(w).empty());  // identical repeat: nothing
        w["asset_cache_browser"] = "<script>window.location.href='byond://?asset_cache_confirm_arrival=2'</script>";
        r = a.collect(w);  // a fresh job id on the same window is a new question
        QCOMPARE(r.size(), size_t(1));
        QCOMPARE(r[0], std::string("byond://?asset_cache_confirm_arrival=2"));
    }
    void navAckIsPerHrefAndMachineOnly() {
        // the per-browse() entry point, BrowseHost::open in shell/browsehost.cpp
        HeadlessAcks a;
        const std::string pg =
            "\n\t<script>\n\t\twindow.location.href=\"?asset_cache_confirm_arrival=9\"\n\t</script>\n";  // a bare relative location
        auto r = a.navAck("asset_cache_browser", pg);
        QVERIFY(r);
        QCOMPARE(*r, std::string("?asset_cache_confirm_arrival=9"));
        QVERIFY(!a.navAck("asset_cache_browser", pg));  // identical repeat
        QVERIFY(!a.navAck("pda", pg));                  // not a machine window
        QVERIFY(a.navAck("tooltip42", pg.substr(0, 0) + "<script>location='?x=1'</script>"));
    }
    void onlyTheAssetCacheGoesUnhosted() {
        // the GUI hosts a tooltip page in the browser control vg winset-created for it; only the asset cache's
        // handshake page is answered without a page
        QVERIFY(hostlessBrowser("asset_cache_browser"));
        QVERIFY(!hostlessBrowser("tooltip07045140117552442"));
        QVERIFY(machineBrowser("tooltip07045140117552442"));
    }
    void ordinaryWindowsAreNeverAnswered() {
        HeadlessAcks a;
        std::map<std::string, std::string> w;
        w["pda"] = "<script>window.location.href='byond://?evil=1'</script>";
        QVERIFY(a.collect(w).empty());
    }
};
FUGUE_TEST_APPLESS_MAIN(HeadlessAcksTest)
#include "headlessacks_test.moc"
