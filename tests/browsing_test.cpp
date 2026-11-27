// The browse() rules of model/browsing.h: parameter parsing, the output() hand-off, browse(file()) and the page-call bridge.
#include <QtTest>
#include "tests/testmain.h"
#include "model/browsing.h"

using namespace fugue::model;

static fugue::Bytes B(const std::string& s) { return fugue::Bytes(s.begin(), s.end()); }
#define LIT(x) std::string(x, sizeof(x) - 1)

class BrowsingTest : public QObject {
    Q_OBJECT
private slots:
    void paramsDelimitersAndTriState() {
        auto p = parseBrowseParams("window=pda;size=400x444;can_resize=0;titlebar=1;pos=10,20&file=pda.html");
        QCOMPARE(p.window, std::string("pda"));
        QCOMPARE(p.w, 400);
        QCOMPARE(p.h, 444);
        QVERIFY(p.posSet);
        QCOMPARE(p.x, 10);
        QCOMPARE(p.y, 20);  // a comma inside pos= is not a delimiter
        QCOMPARE(p.file, std::string("pda.html"));
        QVERIFY(!p.resizable());
        QVERIFY(p.hasTitlebar());
        auto q = parseBrowseParams("window=a,size=1x2,titlebar=0");  // a comma that starts a key= is a delimiter
        QCOMPARE(q.window, std::string("a"));
        QCOMPARE(q.w, 1);
        QVERIFY(!q.hasTitlebar());
        auto n = parseBrowseParams("window=x");  // unstated is not false
        QVERIFY(n.resizable());
        QVERIFY(n.hasTitlebar());
        QCOMPARE(n.canResize, -1);
        auto d = parseBrowseParams("stat");  // no '=' names no window
        QVERIFY(d.window.empty());
        auto k = parseBrowseParams("window=w;can-resize=0;auto_format=0");  // '-' and '_' are the same key
        QVERIFY(!k.resizable());
        QCOMPARE(k.autoFormat, 0);
        auto z = parseBrowseParams("file=asset_data.json&display=0");  // file= with display=0 is saved, not shown
        QCOMPARE(z.file, std::string("asset_data.json"));
        QCOMPARE(z.display, 0);
        QCOMPARE(n.display, -1);
    }
    void browseFileDirective() {
        std::string o;
        QVERIFY(browseFileOptions("##action=browse_file;options=window%3dbrowseroutput",
                                  o));  // encoded, as servers send it
        QCOMPARE(o, std::string("window=browseroutput"));
        QVERIFY(browseFileOptions("##action=browse_file;options=window=lobbybrowser",
                                  o));  // unencoded '=' inside options is rejected
        QCOMPARE(o, std::string("window=lobbybrowser"));
        QVERIFY(!browseFileOptions("##action=load_rsc", o));  // a load_rsc answer is not a browse_file
        QVERIFY(!browseFileOptions("", o));
        QVERIFY(browseFileOptions("##action=browse_file;options=file=titlescreen.png", o));
        QCOMPARE(browseFileAlias(o), std::string("titlescreen.png"));
        QCOMPARE(browseFileAlias("window=x"), std::string(""));
    }
    void pageCallRouting() {
        PageCall c;
        QVERIFY(parsePageCall("tgui-window-3:update", "%7b%22a%22%3a1%7d", c));  // the window is the browser control
        QCOMPARE(c.window, std::string("tgui-window-3"));
        QCOMPARE(c.fn, std::string("update"));
        QCOMPARE(c.args.size(), size_t(1));
        QCOMPARE(c.args[0], std::string("{\"a\":1}"));
        QVERIFY(parsePageCall("tgui-window-3.browser:replaceHtml", "x", c));
        QCOMPARE(c.window, std::string("tgui-window-3"));
        QVERIFY(parsePageCall("stat.stat:update", "x", c));
        QCOMPARE(c.window, std::string("stat"));  // split at the LAST dot
        QVERIFY(parsePageCall("browseroutput:ehjaxCallback", "pang", c));
        QVERIFY(!parsePageCall("browseroutput:output", "line", c));               // chat output stays chat
        QVERIFY(parsePageCall("browseroutput:output", "a%26b&%2526&", c, true));  // explicit local call
        QCOMPARE(c.fn, std::string("output"));
        QCOMPARE(c.args, (std::vector<std::string>{"a&b", "%26", ""}));
        QVERIFY(!parsePageCall("browseroutput", "line", c, true));  // not a browser function call
        QVERIFY(!parsePageCall("browseroutput", "line", c));
        QVERIFY(!parsePageCall("win.browser", "<html>", c));  // no function: a REPLACE, not a call
        QVERIFY(parsePageCall("mapwindow.credits:setupCredits", "Cast&Producers&default&20&2000",
                              c));  // any function, not a list
        QCOMPARE(c.target, std::string("mapwindow.credits"));
        QCOMPARE(c.args.size(), size_t(5));
        QVERIFY(parsePageCall("exosuit.browser:replaceContent", "x", c));
        QCOMPARE(c.window, std::string("exosuit"));
    }
    void outputFileDirective() {
        std::string c;
        QVERIFY(outputFileControl("##action=output_file;window=mapwindow.credits",
                                  c));  // output(file, "mapwindow.credits")
        QCOMPARE(c, std::string("mapwindow.credits"));
        QVERIFY(!outputFileControl("##action=browse_file;options=window%3dbrowseroutput", c));
        QVERIFY(!outputFileControl("##action=load_rsc", c));
        QVERIFY(!outputFileControl("##action=output_file", c));
        QVERIFY(!outputFileControl("", c));
    }
    void pageCallArgsSplitBeforeDecoding() {  // parameters made by list2params() are split before they are decoded
        PageCall c;
        QVERIFY(parsePageCall("rpane.hosttracker:SetMusic", "http%3a%2f%2fh%2ff.ogg&0&50", c));
        QCOMPARE(c.target, std::string("rpane.hosttracker"));  // hosttracker and hosttracker2 share the rpane prefix
        QCOMPARE(c.args.size(), size_t(3));
        QCOMPARE(c.args[0], std::string("http://h/f.ogg"));
        QCOMPARE(c.args[1], std::string("0"));
        QCOMPARE(c.args[2], std::string("50"));
        QVERIFY(parsePageCall("w:update", "a%26b", c));  // an encoded & does not split
        QCOMPARE(c.args.size(), size_t(1));
        QCOMPARE(c.args[0], std::string("a&b"));
    }
    void byondUrls() {
        auto t = parseByondUrl("byond://?src=0x2000001;action=x");
        QVERIFY(t.path.empty());
        QCOMPARE(t.get("src"), std::string("0x2000001"));
        QCOMPARE(t.get("action"), std::string("x"));
        auto w = parseByondUrl(
            "byond://winset?mapwindow.map.focus=true;command=say+hi%21");  // ';' and '&' both separate parameters
        QCOMPARE(w.path, std::string("winset"));
        QCOMPARE(w.get("mapwindow.map.focus"), std::string("true"));
        QCOMPARE(w.get("command"), std::string("say hi!"));
        auto g = parseByondUrl("byond://winget?callback=wingetMacros&id=hotkeymode.*&property=command");
        QCOMPARE(g.path, std::string("winget"));
        QCOMPARE(g.get("id"), std::string("hotkeymode.*"));
        QCOMPARE(g.get("callback"), std::string("wingetMacros"));
        auto g2 = parseByondUrl("byond://winget/?id=x");
        QCOMPARE(g2.path, std::string("winget"));
        QCOMPARE(parseByondUrl("?a=b").raw, std::string("byond://?a=b"));
        QCOMPARE(unescapeQuery("100%"), std::string("100%"));  // tolerant of a bare %
    }
    void linkFrames() {
        auto kind = [](const std::string& url) {
            auto b = B(url + LIT("\0"));
            LinkFrame f;
            if (!parseLinkFrame(fugue::ByteSpan(b.data(), b.size()), f)) return -1;
            return int(f.kind);
        };
        QCOMPARE(kind("http://www.vgstation13.org/forum/"), int(LinkFrame::External));
        QCOMPARE(kind("HTTPS://example.org/a?b=1&c=2"), int(LinkFrame::External));  // the scheme is case-insensitive
        QCOMPARE(kind("mailto:a@example.org"), int(LinkFrame::External));
        QCOMPARE(kind("?src=1;x=2"), int(LinkFrame::Topic));
        QCOMPARE(kind("byond://?src=1"), int(LinkFrame::Topic));
        QCOMPARE(kind("byond://127.0.0.1:5000"), int(LinkFrame::World));
        QCOMPARE(kind("file:///etc/passwd"), int(LinkFrame::Refused));
        QCOMPARE(kind("steam://run/1"), int(LinkFrame::Refused));
        QCOMPARE(kind("javascript:alert(1)"), int(LinkFrame::Refused));
        QCOMPARE(kind("www.example.org/a:b"),
                 int(LinkFrame::Refused));  // no scheme: a colon after the first '/' is not one
        QCOMPARE(kind(""), int(LinkFrame::Refused));
        LinkFrame f;
        auto good = B(LIT("http://a/b\0"));
        QVERIFY(parseLinkFrame(fugue::ByteSpan(good.data(), good.size()), f));
        QCOMPARE(f.url, std::string("http://a/b"));
        auto noNul = B("http://a/b"), trailing = B(LIT("http://a/b\0x"));
        QVERIFY(!parseLinkFrame(fugue::ByteSpan(noNul.data(), noNul.size()), f));
        QVERIFY(!parseLinkFrame(fugue::ByteSpan(trailing.data(), trailing.size()), f));
    }
    // Stock DreamDaemon's 0x0084 for a page built from `"\improper YouTool"` and `"\red [x] B2 \b[x]\b B3"`; stock
    // DreamSeeker writes the page without any marker (FF 16 \improper, FF 1F \red, FF 17 \b).
    void browseDropsTextMacroMarkers() {
        BrowseFrame f;
        auto b = B(LIT("<b>\xff\x16YouTool C1</b> \xff\x1fTMX B2 \xff\x17TMX\xff\x17 B3\0window=vend\xff\x16ing\0\0"));
        QVERIFY(parseBrowseFrame(fugue::ByteSpan(b.data(), b.size()), f));
        QCOMPARE(f.html, std::string("<b>YouTool C1</b> TMX B2 TMX B3"));
        QCOMPARE(f.params, std::string("window=vending"));
        QCOMPARE(parseBrowseParams(f.params).window, std::string("vending"));
    }
    void frames() {
        BrowseFrame f;
        auto b = B(LIT("<b>hi</b>\0win=a;size=1x1\0\0"));
        QVERIFY(parseBrowseFrame(fugue::ByteSpan(b.data(), b.size()), f));
        QCOMPARE(f.html, std::string("<b>hi</b>"));
        QCOMPARE(f.params, std::string("win=a;size=1x1"));
        QVERIFY(!f.close);
        auto c = B(LIT("\0window=a\0\1"));
        QVERIFY(parseBrowseFrame(fugue::ByteSpan(c.data(), c.size()), f));
        QVERIFY(f.close);
        auto bad = B(LIT("x\0y\0\2"));
        QVERIFY(!parseBrowseFrame(fugue::ByteSpan(bad.data(), bad.size()), f));
        OutputFrame o;
        auto oo = B(LIT("v\n\0stat.stat:update\0"));
        QVERIFY(parseOutputFrame(fugue::ByteSpan(oo.data(), oo.size()), o));
        QCOMPARE(o.value, std::string("v"));
        QCOMPARE(o.control, std::string("stat.stat:update"));
        auto r =
            B(LIT("\x03\0\0\0"
                  "tgui.js\0"
                  "\x04\0\0\0\0"));
        auto recs = parseBrowseRscFrame(fugue::ByteSpan(r.data(), r.size()));
        QCOMPARE(recs.size(), size_t(2));
        QCOMPARE(recs[0].ordinal, 3u);
        QCOMPARE(recs[0].name, std::string("tgui.js"));
        QCOMPARE(recs[1].ordinal, 4u);
        QVERIFY(recs[1].name.empty());
    }
};
FUGUE_TEST_APPLESS_MAIN(BrowsingTest)
#include "browsing_test.moc"
