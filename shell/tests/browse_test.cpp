// HTML surface (real QtWebEngine, offscreen): a page loads from the loopback origin with its browse_rsc assets, talks back
// through byond:// navigations and cef_to_byond, and is called by the server through pageCall(). DOM state is read back with
// runJavaScript; nothing here compares pixels.
#include <cmath>
#include <memory>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebEngineProfile>
#include <QtTest>
#include "tests/testmain.h"
#include <QLineEdit>
#include "shell/browsehost.h"
#include "shell/localorigin.h"
#include "shell/focusfilter.h"
#include "shell/inputrouter.h"
#include "shell/skinsync.h"
#include "shell/skinwindow.h"
#include "shell/mapwidget.h"
#include "skin/skinfeed.h"
#include "skin/usermacros.h"
#include "proto/msg.h"
#include "browse_pages.h"
#include "live_skin.h"

using namespace fugue::shell;

static QVariant ev(BrowserPane* p, const QString& js, int ms = 8000) {
    // A callback can arrive after the wait times out; do not capture stack locals.
    struct Result {
        QVariant value;
        bool done = false;
    };
    auto result = std::make_shared<Result>();
    p->page()->runJavaScript(js, [result](const QVariant& v) {
        result->value = v;
        result->done = true;
    });
    if (!QTest::qWaitFor([&] { return result->done; }, ms)) {
        QTest::qFail("Timed out waiting for JavaScript evaluation", __FILE__, __LINE__);
        return {};
    }
    return result->value;
}
static QString evs(BrowserPane* p, const QString& js) { return ev(p, js).toString(); }

// Give a pane's page the keyboard and return the widget that holds it (Chromium's focus-proxy child, not the pane).
static QWidget* focusPage(BrowserPane* p) {
    p->window()->activateWindow();
    p->setFocus();
    if (!QTest::qWaitFor(
            [&] {
                QWidget* f = qApp->focusWidget();
                return f && (f == p || p->isAncestorOf(f)) && !(p->findField() && f == p->findField());
            },
            5000)) {
        QTest::qFail("Timed out waiting for browser page focus", __FILE__, __LINE__);
        return nullptr;
    }
    return qApp->focusWidget();
}
static const char* kFindPage =
    "<p id=t>apple banana apple cherry apple</p><input id=i>"
    "<script>window.kd=[];addEventListener('keydown',function(e){kd.push(e.key+(e.ctrlKey?'^':''))},true)</script>";

struct Http {
    int status = 0;
    QString type;
    QByteArray body;
    qint64 ms = 0;
};
static Http get(const QString& url, const QByteArray& host = {}) {
    QNetworkAccessManager nam;
    QNetworkRequest rq{QUrl(url)};
    if (!host.isEmpty()) rq.setRawHeader("Host", host);
    QElapsedTimer t;
    t.start();
    QNetworkReply* r = nam.get(rq);
    if (!QTest::qWaitFor([&] { return r->isFinished(); }, 5000)) {
        QTest::qFail("Timed out waiting for HTTP response", __FILE__, __LINE__);
        delete r;
        return {};
    }
    Http h;
    h.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    h.type = r->header(QNetworkRequest::ContentTypeHeader).toString();
    h.body = r->readAll();
    h.ms = t.elapsed();
    delete r;
    return h;
}

class BrowseTest : public QObject {
    Q_OBJECT
    QWidget* anchor = nullptr;
    BrowseHost* host = nullptr;
    QSignalSpy* topics = nullptr;
    QSignalSpy* cmds = nullptr;
private slots:
    void initTestCase() {
        // The test's own client talks to 127.0.0.1: no system proxy lookup (on macOS it adds seconds to timed requests).
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        anchor = new QWidget;
        anchor->resize(300, 300);
        host = new BrowseHost(anchor);
        QVERIFY(host->origin()->listening());
        topics = new QSignalSpy(host, &BrowseHost::byondLink);
        cmds = new QSignalSpy(host, &BrowseHost::commandRequested);
        auto st = host->store();
        st->put(1, "app.js", kAppJs);
        st->put(2, "app.css", kAppCss);
        static constexpr char png[] =
            "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\1\0\0\0\1\x08\x06\0\0\0\x1f\x15\xc4\x89"
            "\0\0\0\rIDATx\x9c"
            "c\xf8\xff\xff?\0\5\xfe\2\xfe\xa7\x35\x81\x84\0\0\0\0IEND\xae"
            "B`\x82";
        st->put(3, "px.png", std::string(png, sizeof png - 1));
    }
    void cleanupTestCase() {
        delete topics;
        delete cmds;
        delete host;
        delete anchor;
    }

    // ---- popup window + assets ---------------------------------------------------------------------------------
    void popupLoadsItsAssetsFromTheOrigin() {
        host->open("pop1", "window=pop1;size=300x200;can_resize=0", kPopupPage);
        BrowserPane* p = host->paneFor("pop1");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('out').textContent") == "js-ran"; },
                                8000));  // app.js ran
        QCOMPARE(evs(p, "getComputedStyle(document.getElementById('out')).color"),
                 QString("rgb(1, 2, 3)"));  // app.css applied
        QVERIFY(QTest::qWaitFor([&] { return ev(p, "document.getElementById('im').naturalWidth").toInt() == 1; },
                                5000));  // px.png decoded
        QWidget* w = host->popupFor("pop1");
        QVERIFY(w);
        QVERIFY(!w->isHidden());
        QCOMPARE(w->minimumSize(), QSize(300, 200));  // can_resize=0 pins min = max
        QCOMPARE(w->maximumSize(), QSize(300, 200));
        QVERIFY(w->windowFlags() & Qt::Dialog);
        QCOMPARE(w->parentWidget(), anchor);  // transient-for the main window
        QVERIFY(host->origin()->stats().rsc >= 3);
    }
    void notFoundKeepsTheRightMime() {
        const QString b = host->origin()->base();
        Http h = get(b + "nope.css");
        QCOMPARE(h.status, 404);
        QVERIFY2(h.type.startsWith("text/css"), qPrintable(h.type));
        h = get(b + "nope.js");
        QCOMPARE(h.status, 404);
        QVERIFY(h.type.startsWith("text/javascript"));
        h = get(b + "app.js");
        QCOMPARE(h.status, 200);
        QVERIFY(h.type.startsWith("text/javascript"));
        QVERIFY(h.body.contains("update("));
        // the secret prefix and the Host guard
        QCOMPARE(get(QString("http://127.0.0.1:%1/app.js").arg(host->origin()->port())).status, 404);
        QCOMPARE(get(QString("http://127.0.0.1:%1/deadbeef/app.js").arg(host->origin()->port())).status, 404);
        QCOMPARE(get(b + "app.js", "evil.example:80").status, 403);
    }
    void secondChanceForAFreshResource() {
        auto st = host->store();
        // announced (0x00fe) but the bytes are ~100 ms away: held open, then served
        st->declare("late.png", 77);
        QTimer::singleShot(120, [st] { st->put(77, "late.png", "PNGBYTES"); });
        Http h = get(host->origin()->base() + "late.png");
        QCOMPARE(h.status, 200);
        QCOMPARE(h.body, QByteArray("PNGBYTES"));
        QVERIFY(h.ms >= 100 && h.ms < 500);
        QVERIFY(host->origin()->stats().waited >= 1);
        // A slow resource still arrives: the wait outlasts 500 ms, since a page never re-requests a stylesheet or <img>.
        st->declare("slow.css", 78);
        QTimer::singleShot(1500, [st] { st->put(78, "slow.css", "body{}"); });
        h = get(host->origin()->base() + "slow.css");
        QCOMPARE(h.status, 200);
        QCOMPARE(h.body, QByteArray("body{}"));
        QVERIFY2(h.ms >= 1400, qPrintable(QString::number(h.ms)));
        // never announced: no wait at all
        h = get(host->origin()->base() + "ghost.png");
        QCOMPARE(h.status, 404);
        QVERIFY2(h.ms < 500, qPrintable(QString::number(h.ms)));  // no hold (holds are >= 1400 ms)
        // An unnamed record may be the one outstanding; matched by basename.
        st->declare("", 88);
        QTimer::singleShot(100, [st] { st->put(88, "nano/js/nano_utility.js", "var nano=1;"); });
        h = get(host->origin()->base() + "nano_utility.js");
        QCOMPARE(h.status, 200);
        QCOMPARE(h.body, QByteArray("var nano=1;"));
        // a named alias (##action=browse_file;options=file=titlescreen.png)
        st->put(99, "torchlogo.gif", "GIF");
        st->alias("titlescreen.png", 99);
        QCOMPARE(get(host->origin()->base() + "titlescreen.png").body, QByteArray("GIF"));
        // Media is also a browser asset: an announced Ogg waits for its bytes.
        st->declare("song.ogg", 5);
        QTimer::singleShot(120, [st] { st->put(5, "sound/song.ogg", "OGGBYTES"); });
        h = get(host->origin()->base() + "song.ogg");
        QCOMPARE(h.status, 200);
        QCOMPARE(h.body, QByteArray("OGGBYTES"));
        QVERIFY(h.type.startsWith("audio/"));
    }
    void mediaResources_data() {
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QString>("mimePrefix");
        for (const auto* ext : {"ogg", "OGG", "wav", "mp3", "mid", "midi", "flac", "opus"})
            QTest::newRow(ext) << QString(ext) << QString("audio/");
        for (const auto* ext : {"mp4", "webm", "ogv"}) QTest::newRow(ext) << QString(ext) << QString("video/");
        // Retention/serving is independent of Chromium's codec support, not a media whitelist.
        for (const auto* ext : {"mod", "xm", "it", "s3m", "unknown-extension"})
            QTest::newRow(ext) << QString(ext) << QString();
    }
    void mediaResources() {
        QFETCH(QString, extension);
        QFETCH(QString, mimePrefix);
        auto st = std::make_shared<BrowseRscStore>();
        LocalOrigin origin(st);
        QVERIFY(origin.listening());
        const auto name = "media." + extension;
        st->declare(name.toStdString(), 1);
        QCOMPARE(st->waitFor(name.toStdString()), BrowseRscStore::Wait::Announced);
        const std::string bytes("binary\0payload", 14);
        st->put(1, ("sound/" + name).toStdString(), bytes);
        QCOMPARE(st->waitFor(name.toStdString()), BrowseRscStore::Wait::No);
        const auto response = get(origin.urlFor(name));
        QCOMPARE(response.status, 200);
        QCOMPARE(response.body, QByteArray::fromStdString(bytes));
        QVERIFY2(response.type.startsWith(mimePrefix), qPrintable(response.type));
        // Alias resolution and replacement must work just like scripts/images.
        st->alias("alias." + extension.toStdString(), 1);
        QCOMPARE(get(origin.urlFor("alias." + extension)).body, response.body);
        st->put(1, ("sound/" + name).toStdString(), "replacement");
        QCOMPARE(get(origin.urlFor(name)).body, QByteArray("replacement"));
    }
    // tgui's DmIcon loads "[0xc0000NN]?state=...": the origin asks the session thread through the store and serves
    // what it answers, a PNG or nothing.
    void iconReferences() {
        auto st = std::make_shared<BrowseRscStore>();
        LocalOrigin origin(st);
        QStringList asked;
        QTimer session;
        connect(&session, &QTimer::timeout, [&] {
            for (const std::string& key : st->takeWantedIcons()) {
                asked << QString::fromStdString(key);
                st->putIcon(key, key.find("state=none") == std::string::npos ? "PNG bytes" : "");
            }
        });
        session.start(10);
        const auto drawn = get(origin.urlFor("[0xc000002]") + "?state=crowbar&dir=2&movement=false&frame=1");
        QCOMPARE(drawn.status, 200);
        QCOMPARE(drawn.type, QString("image/png"));
        QCOMPARE(drawn.body, QByteArray("PNG bytes"));
        QCOMPARE(asked, QStringList{"[0xc000002]?state=crowbar&dir=2&movement=false&frame=1"});
        QCOMPARE(get(origin.urlFor("[0xc000002]") + "?state=none").status, 404);
        QCOMPARE(get(origin.urlFor("[0xc000002]") + "?state=crowbar&dir=2&movement=false&frame=1").body, drawn.body);
        QCOMPARE(asked.size(), 2);  // answered once, served from the store after that
    }
    void browserResourceLimits() {
        BrowseRscStore st;
        std::string bytes;
        st.put(1, "oversized.ogg", std::string((32u << 20) + 1, 'x'));
        QVERIFY(!st.get("oversized.ogg", bytes));
        for (const auto* name : {"world.dmf", "icon.DMI"}) {
            st.declare(name, 2);
            st.put(2, name, "not browser media");
            QCOMPARE(st.waitFor(name), BrowseRscStore::Wait::No);
            QVERIFY(!st.get(name, bytes));
        }
    }

    // A held request takes one of Chromium's 6 connections to the origin. Unnamed records are common (most names a page asks for are unannounced), so
    // 'an unnamed record is outstanding' is true for almost any name. Unknown names therefore wait briefly, at most kMaxHeldUnnamed at once, and at
    // most kMaxHeld are held while any are unknown. An announced name outranks an unknown one (it evicts the oldest) and is held even over the cap;
    // __bridge is never held.
    void heldRequestsAreBoundedAndBridgeIsNeverStarved() {
        auto st = std::make_shared<BrowseRscStore>();
        LocalOrigin o(st);
        QVERIFY(o.listening());
        st->put(1, "here.css", "a{}");
        o.setBridge([](int, const QByteArray& body) { return QString("ok:") + QString::fromUtf8(body); });
        st->declare("", 500);  // an unnamed record that never lands
        QNetworkAccessManager nam;
        // Separate pools keep Qt's connection reuse/scheduling from queuing a request behind a held one. This test
        // measures the origin's hold budget, not QNetworkAccessManager's platform-dependent connection scheduler.
        struct R {
            std::unique_ptr<QNetworkAccessManager> nam;
            QNetworkReply* r;
            QElapsedTimer t;
            qint64 ms = -1;
        };
        std::vector<std::unique_ptr<R>> rs;
        auto fire = [&](const QString& file) {
            auto x = std::make_unique<R>();
            x->nam = std::make_unique<QNetworkAccessManager>();
            x->t.start();
            x->r = x->nam->get(QNetworkRequest(QUrl(o.base() + file)));
            R* raw = x.get();
            QObject::connect(x->r, &QNetworkReply::finished, x->r, [raw] { raw->ms = raw->t.elapsed(); });
            rs.push_back(std::move(x));
            return raw;
        };
        auto status = [](R* x) { return x->r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); };
        R* u1 = fire("missing1.png");
        QVERIFY(QTest::qWaitFor([&] { return o.held() == 1; }, 3000));  // make u1 deterministically the oldest
        R* u2 = fire("missing2.png");
        QVERIFY(QTest::qWaitFor([&] { return o.held() == 2; }, 3000));
        R* u3 = fire("missing3.png");  // a third unknown name: not held
        QVERIFY(QTest::qWaitFor([&] { return u3->ms >= 0; }, 3000));
        QCOMPARE(status(u3), 404);
        QVERIFY2(u3->ms < 300, qPrintable(QString::number(u3->ms)));
        st->declare("a.css", 10);
        st->declare("b.css", 11);
        st->declare("c.css", 12);
        R* a = fire("a.css");
        R* b2 = fire("b.css");
        QVERIFY(QTest::qWaitFor([&] { return o.held() == 4; }, 3000));
        R* c = fire("c.css");  // over kMaxHeld: the OLDEST unknown-name hold gives way
        QVERIFY(QTest::qWaitFor([&] { return u1->ms >= 0; }, 3000));
        QCOMPARE(status(u1), 404);
        QVERIFY(QTest::qWaitFor([&] { return o.held() == 4; }, 3000));
        QVERIFY(u2->ms < 0 && a->ms < 0 && b2->ms < 0 && c->ms < 0);
        // four held, and the page can still load what IS here and talk to the client
        QCOMPARE(get(o.base() + "here.css").status, 200);
        {
            QNetworkRequest rq{QUrl(o.base() + "__bridge?pane=1")};
            rq.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
            QElapsedTimer t;
            t.start();
            QNetworkReply* r = nam.post(rq, QByteArray("byond://winget?id=x"));
            QVERIFY(QTest::qWaitFor([&] { return r->isFinished(); }, 3000));
            QCOMPARE(r->readAll(), QByteArray("ok:byond://winget?id=x"));
            QVERIFY2(t.elapsed() < 500, qPrintable(QString::number(t.elapsed())));
            delete r;
        }
        R* d = fire("d.css");  // an unknown name while all kMaxHeld are held: 404 now
        QVERIFY(QTest::qWaitFor([&] { return d->ms >= 0; }, 3000));
        QCOMPARE(status(d), 404);
        st->put(10, "a.css", "A");
        st->put(11, "b.css", "B");
        st->put(12, "c.css", "C");
        QVERIFY(QTest::qWaitFor([&] { return a->ms >= 0 && b2->ms >= 0 && c->ms >= 0; }, 3000));
        QCOMPARE(status(a), 200);
        QCOMPARE(status(b2), 200);
        QCOMPARE(status(c), 200);
        QCOMPARE(a->r->readAll(), QByteArray("A"));
        // the remaining unknown name never arrives: 404 after the SHORT bound, not the announced-name one
        QVERIFY(QTest::qWaitFor([&] { return u2->ms >= 0; }, LocalOrigin::kUnnamedWaitMs + 8000));  // slow CI runners
        QCOMPARE(status(u2), 404);
        QVERIFY2(u2->ms >= LocalOrigin::kUnnamedWaitMs - 100 && u2->ms < LocalOrigin::kUnnamedWaitMs + 1500,
                 qPrintable(QString::number(u2->ms)));
        QCOMPARE(o.held(), 0);
        QCOMPARE(o.stats().heldPeak, LocalOrigin::kMaxHeld);
        QVERIFY(o.stats().heldRefused >= 3);
        for (auto& x : rs) delete x->r;
    }
    void missingResourceTimeoutUsesElapsedTime() {
        auto st = std::make_shared<BrowseRscStore>();
        LocalOrigin o(st);
        QVERIFY(o.listening());
        st->declare("", 500);
        QNetworkAccessManager nam;
        QNetworkReply* r = nam.get(QNetworkRequest(QUrl(o.urlFor("missing.png"))));
        QVERIFY(QTest::qWaitFor([&] { return o.held() == 1; }, 3000));
        QVERIFY(!r->isFinished());
        // Deliberately stop dispatching timers. On resuming, the hold has expired regardless of how few
        // polling callbacks ran; counting callbacks would make us wait another two seconds (issue #13).
        QTest::qSleep(LocalOrigin::kUnnamedWaitMs + 100);
        QVERIFY(QTest::qWaitFor([&] { return r->isFinished(); }, 1000));
        QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
        QCOMPARE(o.held(), 0);
        QCOMPARE(o.stats().waited, 1);
        QCOMPARE(o.stats().notFound, 1);
        delete r;
    }

    // Announced-but-missing files beyond kMaxHeld are all still held: a 404'd stylesheet is never retried.
    void announcedNamesOverTheCapAreAllServed() {
        auto st = std::make_shared<BrowseRscStore>();
        LocalOrigin o(st);
        QVERIFY(o.listening());
        QNetworkAccessManager nam;
        const int n = LocalOrigin::kMaxHeld + 2;
        std::vector<QNetworkReply*> rs;
        for (int i = 0; i < n; i++) {
            st->declare(QString("e%1.css").arg(i).toStdString(), uint32_t(100 + i));
            rs.push_back(nam.get(QNetworkRequest(QUrl(o.base() + QString("e%1.css").arg(i)))));
        }
        QVERIFY(QTest::qWaitFor([&] { return o.held() == n; }, 3000));
        for (QNetworkReply* r : rs) QVERIFY(!r->isFinished());  // none refused for the cap
        for (int i = 0; i < n; i++) st->put(uint32_t(100 + i), QString("e%1.css").arg(i).toStdString(), "x");
        QVERIFY(QTest::qWaitFor(
            [&] {
                for (auto* r : rs)
                    if (!r->isFinished()) return false;
                return true;
            },
            3000));
        for (QNetworkReply* r : rs) QCOMPARE(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
        QCOMPARE(o.held(), 0);
        for (QNetworkReply* r : rs) delete r;
    }

    // ---- server -> page ----------------------------------------------------------------------------------------
    void pageCallIsQueuedUntilReady() {
        host->open("pop2", "window=pop2", kPopupPage);
        BrowserPane* p = host->paneFor("pop2");
        QVERIFY(p && !p->ready());
        host->pageCall("pop2", "update", {"x y", "a&b"});  // before the page exists as a document
        host->pageCall("pop2", "noSuchFunction",
                       {"z"});  // a function the page does not define is a no-op, not an error
        QCOMPARE(p->callsRun(), 0);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('out').textContent") == "upd:x y,a&b"; },
                                8000));  // runs after js-ran
        QCOMPARE(p->callsRun(), 2);
        // a call after ready runs at once, multiple arguments in order
        host->pageCall("pop2", "update", {"1", "2", "3"});
        QVERIFY(
            QTest::qWaitFor([&] { return evs(p, "document.getElementById('out').textContent") == "upd:1,2"; }, 5000));
        // a name with no page is dropped (logged once), never thrown
        host->pageCall("nobody", "update", {"x"});
    }

    // ---- page -> client ----------------------------------------------------------------------------------------
    void topicsFromEveryNavigationShape() {
        BrowserPane* p = host->paneFor("pop2");
        QVERIFY(p && p->ready());
        topics->clear();
        ev(p, "location.href = 'byond://?src=%5B0x2000001%5D;action=go'");  // tgui: gestureless, a Redirect
        QVERIFY(QTest::qWaitFor([&] { return topics->count() >= 1; }, 5000));
        QCOMPARE(QString::fromUtf8(topics->at(0).at(0).toUrl().toEncoded()),
                 QString("byond://?src=%5B0x2000001%5D;action=go"));
        ev(p, "document.getElementById('lnk').click()");  // a clicked relative '?' link
        QVERIFY(QTest::qWaitFor([&] { return topics->count() >= 2; }, 5000));
        QCOMPARE(QString::fromUtf8(topics->at(1).at(0).toUrl().toEncoded()), QString("byond://?src=7;action=clicked"));
        ev(p, "window.location = '?_src_=chat&proc=ping'");  // goon: relative, gestureless
        QVERIFY(QTest::qWaitFor([&] { return topics->count() >= 3; }, 5000));
        QCOMPARE(QString::fromUtf8(topics->at(2).at(0).toUrl().toEncoded()), QString("byond://?_src_=chat&proc=ping"));
        ev(p, "cef_to_byond('byond://?src=9;action=bridge')");  // helpers: the XHR bridge
        QVERIFY(QTest::qWaitFor([&] { return topics->count() >= 4; }, 5000));
        QCOMPARE(QString::fromUtf8(topics->at(3).at(0).toUrl().toEncoded()), QString("byond://?src=9;action=bridge"));
        // ...and the page stayed where it was (a Topic is not a navigation)
        QCOMPARE(evs(p, "document.getElementById('out').textContent"), QString("upd:1,2"));
        QCOMPARE(p->url().host(), QString("127.0.0.1"));
    }
    void aWorldAddressIsAWorldLinkNotATopic() {
        BrowserPane* p = host->paneFor("pop2");
        QSignalSpy worlds(host, &BrowseHost::worldLink);
        topics->clear();
        ev(p, "location = 'byond://127.0.0.1:5000?x=1'");
        QVERIFY(QTest::qWaitFor([&] { return worlds.count() >= 1; }, 5000));
        QCOMPARE(worlds.at(0).at(0).toString(), QString("byond://127.0.0.1:5000?x=1"));
        ev(p, "location = 'byond://winset?id=pop2&size=300x200'");
        QTest::qWait(300);
        QCOMPARE(worlds.count(), 1);
        QCOMPARE(topics->count(), 0);
    }
    void aBurstOfNavigationsInOneTaskAllArrive() {
        // A DOM-ready handler can issue several byond:// navigations in one task.
        BrowserPane* p = host->paneFor("pop2");
        topics->clear();
        cmds->clear();
        ev(p,
           "location = 'byond://winset?id=mainwindow&macro=hotkeymode'; location = 'byond://winset?command=burst2'; "
           "location = 'byond://winget?callback=gotWinget&id=pop2&property=can-resize'; location = "
           "'?_src_=chat&proc=doneLoading';");
        QTest::qWait(600);
        qInfo() << "burst: topics" << topics->count() << "commands" << cmds->count() << "winget"
                << evs(p, "window.wg || ''");
        QCOMPARE(topics->count(), 1);  // Blink keeps only the last navigation assigned in one task
        QVERIFY(topics->count() + cmds->count() >= 1);
    }
    void wingetAnswersAnObjectLocally() {
        BrowserPane* p = host->paneFor("pop2");
        topics->clear();
        ev(p, "window.wg = ''; location = 'byond://winget?id=pop2&property=size,is-visible%3Btype&callback=gotWinget'");
        QVERIFY(QTest::qWaitFor([&] { return !evs(p, "window.wg").isEmpty(); }, 5000));
        QJsonObject o = QJsonDocument::fromJson(evs(p, "window.wg").toUtf8()).object();
        QVERIFY(o.value("size").isObject() && o.value("size").toObject().value("x").toInt() > 0);
        QCOMPARE(o.value("is-visible").toBool(), true);
        QCOMPARE(o.value("type").toString(), QString("MAIN"));  // a browse() popup is a WINDOW
        QCOMPARE(topics->count(), 0);                           // answered locally, NEVER a Topic
        // the XHR bridge: the reply body is JS calling Byond.__callbacks__[n]
        ev(p,
           "window.got2 = null; Byond.__callbacks__[0] = function (o) { window.got2 = JSON.stringify(o); };"
           "cef_to_byond('byond://winget?id=pop2&property=pos&callback=Byond.__callbacks__[0]')");
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "window.got2 || ''").size() > 0; }, 5000));
        QJsonObject o2 = QJsonDocument::fromJson(evs(p, "window.got2").toUtf8()).object();
        QVERIFY(o2.value("pos").isObject() &&
                o2.value("pos").toObject().contains("x"));  // pos is an OBJECT {x,y}, not "x,y"
        // a callback that is not an identifier path is refused, not evaluated
        ev(p, "window.evil = 0; cef_to_byond('byond://winget?id=pop2&property=size&callback=(evil=1)')");
        QTest::qWait(300);
        QCOMPARE(ev(p, "window.evil").toInt(), 0);
        // an unanswerable winget still answers {name: null}: helpers does props[name]
        ev(p, "window.wg = ''; location = 'byond://winget?id=nonesuch&property=size&callback=gotWinget'");
        QVERIFY(QTest::qWaitFor([&] { return !evs(p, "window.wg").isEmpty(); }, 5000));
        QCOMPARE(evs(p, "window.wg"), QString("{\"size\":null}"));
    }
    void winsetMovesAPopupAndRunsACommand() {
        BrowserPane* p = host->paneFor("pop2");
        QWidget* w = host->popupFor("pop2");
        QVERIFY(w);
        cmds->clear();
        ev(p, "location = 'byond://winset?id=pop2&size=333x222;pos=11,22'");  // ';' separated, tgui sends '&'
        QVERIFY(QTest::qWaitFor([&] { return w->size() == QSize(333, 222); }, 5000));
        ev(p, "location = 'byond://winset?mapwindow.map.focus=true;command=say+hello%20world'");  // goon's shape
        QVERIFY(QTest::qWaitFor([&] { return cmds->count() == 1; }, 5000));
        QCOMPARE(cmds->at(0).at(0).toString(), QString("say hello world"));
        ev(p, "cef_to_byond('byond://winset?id=pop2&size=300x201')");
        QVERIFY(QTest::qWaitFor([&] { return w->size() == QSize(300, 201); }, 5000));
    }
    void hubStorageRoundTrips() {
        BrowserPane* p = host->paneFor("pop2");
        ev(p,
           "window.hs = ''; hubStorage.setItem('k', 'v1').then(function () { return hubStorage.getItem('k'); "
           "}).then(function (v) { window.hs = String(v); })");
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "window.hs") == "v1"; }, 5000));
    }
    void aPageCannotTakeThePaneToAForeignSite() {
        BrowserPane* p = host->paneFor("pop2");
        ev(p, "location = 'https://example.invalid/'");
        QTest::qWait(400);
        QCOMPARE(p->url().host(), QString("127.0.0.1"));
        QCOMPARE(evs(p, "document.getElementById('out').textContent").isEmpty(), false);
    }

    // ---- popup titles (shell/browsehost.cpp popupTitle) --------------------------------------------------------
    // A page with no <title> gets its own address from the engine; stock DreamSeeker shows a blank title instead.
    void anUntitledPopupShowsNoTitleNotItsAddress() {
        host->open("tt1", "window=tt1;size=200x100", "<html><body>no title (vg HTML_SKELETON)</body></html>");
        QWidget* w = host->popupFor("tt1");
        BrowserPane* p = host->paneFor("tt1");
        QVERIFY(w && p);
        QCOMPARE(w->windowTitle(), QString(" "));  // blank: one space, as Qt shows "" as the app name
        // Once loaded, the engine titles the page with its loopback address; that is not the title used.
        QVERIFY(QTest::qWaitFor([&] { return p->page()->title().contains("127.0.0.1"); }, 15000));
        QTest::qWait(50);
        QVERIFY2(w->windowTitle() == " ", qPrintable(w->windowTitle()));
        host->close("tt1");
    }
    void aPopupTakesItsPagesTitleAndFollowsDocumentTitle() {
        host->open("tt2", "window=tt2;size=200x100",
                   "<html><head><title>  Air Alarm  </title></head><body>x</body></html>");
        QWidget* w = host->popupFor("tt2");
        BrowserPane* p = host->paneFor("tt2");
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle() == "Air Alarm"; }, 15000));  // The title is trimmed.
        ev(p, "document.title = 'Changed by JS'");
        QVERIFY2(QTest::qWaitFor([&] { return w->windowTitle() == "Changed by JS"; }, 5000),
                 qPrintable(w->windowTitle()));
        ev(p, "document.title = location.href");  // the address itself, with or without its scheme: blank
        QVERIFY2(QTest::qWaitFor([&] { return w->windowTitle() == " "; }, 5000), qPrintable(w->windowTitle()));
        ev(p, "document.title = 'Back'");
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle() == "Back"; }, 5000));
        ev(p, "document.title = location.href.toUpperCase()");  // http: is compared case-SENSITIVELY: a real title
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle().startsWith("HTTP://127.0.0.1"); }, 5000));
        ev(p, "document.title = location.href.replace(/^[a-z]+:\\/\\//, '') + '/'");  // no scheme, a trailing '/'
        QVERIFY2(QTest::qWaitFor([&] { return w->windowTitle() == " "; }, 5000), qPrintable(w->windowTitle()));
        // re-browsed into the same window with an untitled page: blank again, not the old title, not the address
        ev(p, "document.title = 'Old'");
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle() == "Old"; }, 5000));
        host->open("tt2", "window=tt2", "<html><body>untitled now</body></html>");
        QVERIFY(QTest::qWaitFor([&] { return p->page()->title().contains("127.0.0.1"); }, 15000));
        QTest::qWait(50);
        QVERIFY2(w->windowTitle() == " ", qPrintable(w->windowTitle()));
        host->close("tt2");
    }
    void aBrowseTitleOptionTitlesTheNewWindow() {
        // stock: browse(..., "window=p2;title=ParamTwo32") sets the window name to ParamTwo32
        host->open("tt3", "window=tt3;size=200x100;title=From Params", "<b>x</b>");
        QCOMPARE(host->popupFor("tt3")->windowTitle(), QString("From Params"));
        host->close("tt3");
    }

    // ---- close -------------------------------------------------------------------------------------------------
    void closeRemovesTheWindowAndItsPage() {
        QVERIFY(host->openWindows().contains("pop1"));
        QString file = "pop1.html";
        QVERIFY(host->origin()->hasPage(file));
        host->close("pop1");
        QVERIFY(!host->openWindows().contains("pop1"));
        QVERIFY(!host->origin()->hasPage(file));
        QCOMPARE(get(host->origin()->urlFor(file)).status, 404);
        host->close("pop1");  // closing nothing is harmless
    }
    void playerClosingAPopupLetsTheWorldReopenIt() {
        QSignalSpy closed(host, &BrowseHost::popupClosedByPlayer);
        host->open("pop4", "window=pop4", "<b id=a>one</b>");
        QWidget* w = host->popupFor("pop4");
        QVERIFY(w);
        w->close();
        QCOMPARE(closed.count(), 1);
        QVERIFY(!host->openWindows().contains("pop4"));
        host->open("pop4", "window=pop4", "<b id=a>one</b>");  // the SAME body again: still reopens
        QVERIFY(host->popupFor("pop4") && host->popupFor("pop4")->isVisible());
        host->close("pop4");
    }
    // Ctrl+F in a browse() popup uses the same gate as a skin pane (browserpane.h): the world's browser-options `find`.
    void ctrlFInAPopupFollowsBrowserOptions() {
        auto live = std::make_shared<fugue::skin::LiveSkin>();
        QVERIFY(live->load("window \"mainwindow\"\n\telem \"mainwindow\"\n\t\ttype = MAIN\n\t\tis-default = true\n")
                    .empty());
        host->setLiveSkin(live);
        host->open("popf", "window=popf;size=400x300", kFindPage);
        BrowserPane* p = host->paneFor("popf");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QWidget* f = focusPage(p);
        QVERIFY(f && p->isAncestorOf(f));
        QTest::keyClick(f, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!p->findOpen());  // browser-options "" (the default): no find bar
        live->winset("", "browser-options=find");
        QCOMPARE(live->browserOptions(), 2u);
        QTest::keyClick(qApp->focusWidget(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(p->findOpen());
        QTest::keyClicks(p->findField(), "cherry");
        QVERIFY(QTest::qWaitFor([&] { return p->findStatus() == "1/1"; }, 8000));
        host->close("popf");
        host->setLiveSkin(nullptr);
    }
    // Closing the window runs its on-close winset (nanoclose forgets the ui). A world-side close must not run it.
    void playerClosingAPopupRunsTheWorldsOnClose() {
        auto live = std::make_shared<fugue::skin::LiveSkin>();
        QVERIFY(live->load("window \"mainwindow\"\n\telem \"mainwindow\"\n\t\ttype = MAIN\n\t\tis-default = true\n")
                    .empty());
        host->setLiveSkin(live);
        live->setPopups({"nanoui7", "pop6"});  // what the session thread publishes from the model's open browse windows
        live->winset("nanoui7", "on-close=\"nanoclose [0x21004c7]\"");  // on_close winset, as nanoui sends it
        QSignalSpy oc(host, &BrowseHost::onCloseCommand);
        host->open("nanoui7", "window=nanoui7;size=300x200", "<b>apc</b>");
        host->popupFor("nanoui7")->close();
        QCOMPARE(oc.count(), 1);
        QCOMPARE(oc.at(0).at(0).toString(), QString("nanoui7"));
        QCOMPARE(oc.at(0).at(1).toString(), QString("nanoclose [0x21004c7]"));
        host->open("nanoui7", "window=nanoui7;size=300x200", "<b>apc</b>");
        host->close("nanoui7");  // the world closed it: it already knows, nothing is run
        QCOMPARE(oc.count(), 1);
        host->open("pop6", "window=pop6", "<b>x</b>");
        host->popupFor("pop6")->close();  // no on-close registered: nothing to run
        QCOMPARE(oc.count(), 1);
        host->setLiveSkin(nullptr);
    }
    // Stock DreamSeeker 516 hides a popup the player closed rather than forgetting it: winexists() still answers "MAIN" and is-visible is false.
    // A re-browse of the same id that registers no new on-close does not run the old one again on the next close.
    void aPlayerClosedPopupStaysAWindowAndKeepsItsOnClose() {
        auto live = std::make_shared<fugue::skin::LiveSkin>();
        QVERIFY(live->load("window \"mainwindow\"\n\telem \"mainwindow\"\n\t\ttype = MAIN\n\t\tis-default = true\n")
                    .empty());
        host->setLiveSkin(live);
        live->setPopups({"popa"});
        live->winset("popa", "on-close=\"nanoclose A1\"");
        QSignalSpy oc(host, &BrowseHost::onCloseCommand);
        host->open("popa", "window=popa;size=300x200", "<b>one</b>");
        std::string v;
        QCOMPARE(QString::fromStdString(live->wingetType("popa")), QString("MAIN"));
        QVERIFY(live->winget("popa", "is-visible", v));
        QCOMPARE(QString::fromStdString(v), QString("true"));
        host->popupFor("popa")->close();
        QCOMPARE(oc.count(), 1);
        QCOMPARE(QString::fromStdString(live->wingetType("popa")),
                 QString("MAIN"));  // winexists after the close: still a window
        QVERIFY(live->winget("popa", "is-visible", v));
        QCOMPARE(QString::fromStdString(v), QString("false"));
        host->open("popa", "window=popa;size=300x200", "<b>two</b>");  // re-browsed, no on-close winset this time
        QVERIFY(live->winget("popa", "is-visible", v));
        QCOMPARE(QString::fromStdString(v), QString("true"));
        host->popupFor("popa")->close();
        QCOMPARE(oc.count(), 2);
        QCOMPARE(oc.at(1).at(1).toString(), QString("nanoclose A1"));
        host->setLiveSkin(nullptr);
    }
    // A byond:// on-close link is sent as a Topic, never as a verb, as stock DreamSeeker does.
    void aByondLinkOnCloseIsATopicNotACommand() {
        auto live = std::make_shared<fugue::skin::LiveSkin>();
        QVERIFY(live->load("window \"mainwindow\"\n\telem \"mainwindow\"\n\t\ttype = MAIN\n\t\tis-default = true\n")
                    .empty());
        host->setLiveSkin(live);
        live->setPopups({"popb"});
        live->winset("popb", "on-close=\"byond://?src=[0x21000000]&action=onclose\"");
        QSignalSpy oc(host, &BrowseHost::onCloseCommand);
        topics->clear();
        host->open("popb", "window=popb", "<b>b</b>");
        host->popupFor("popb")->close();
        QCOMPARE(oc.count(), 0);
        QCOMPARE(topics->count(), 1);
        QCOMPARE(
            QString::fromUtf8(topics->at(0).at(0).toUrl().toEncoded()),
            QString("byond://?src=[0x21000000]&action=onclose"));  // DreamSeeker's own Topic carried the raw brackets
        host->setLiveSkin(nullptr);
    }
    // Quitting the client closes its windows. That is not the player closing a machine window, so it runs no on-close.
    // Keep this test last in the class: the shared host stays quitting.
    void aPopupClosedByAppQuitRunsNoOnClose() {
        auto live = std::make_shared<fugue::skin::LiveSkin>();
        QVERIFY(live->load("window \"mainwindow\"\n\telem \"mainwindow\"\n\t\ttype = MAIN\n\t\tis-default = true\n")
                    .empty());
        host->setLiveSkin(live);
        live->setPopups({"popq", "popl"});
        live->winset("popq", "on-close=\"nanoclose Q1\"");
        live->winset("popl", "on-close=\"byond://?src=1&action=onclose\"");
        QSignalSpy oc(host, &BrowseHost::onCloseCommand);
        host->open("popq", "window=popq", "<b>q</b>");
        host->open("popl", "window=popl", "<b>l</b>");
        topics->clear();
        host->setQuitting();
        host->popupFor("popq")->close();
        host->popupFor("popl")->close();
        QCOMPARE(oc.count(), 0);
        QCOMPARE(topics->count(), 0);
        host->setLiveSkin(nullptr);
    }
};

// ---- docked controls and the skin's winset ------------------------------------------------------
class DockTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
private slots:
    void init() {
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kDockSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
    }
    void cleanup() {
        delete win;
        win = nullptr;
    }

    void browserInnerBackground() {
        // Hidden skin-declared browser: adopt the existing document on show.
        const auto skin = QString::fromUtf8(kDockSkin).replace(
            "elem \"ghost\"\n\t\ttype = BROWSER",
            "elem \"ghost\"\n\t\ttype = BROWSER\n\t\tinner-background-color = transparent");
        QVERIFY(win->loadSkin(skin.toStdString()).isEmpty());
        win->showBrowse("ghost", "window=ghost", "<script>window.retained=42</script>");
        auto* pane = win->browseHost()->paneFor("ghost");
        QVERIFY(pane);
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(), 8000);
        auto live = win->live();
        live->winset("ghost", "is-visible=true");
        win->syncFromLive();
        QCOMPARE(win->browseHost()->paneFor("ghost"), pane);
        QCOMPARE(pane->page()->backgroundColor(), QColor(Qt::transparent));
        auto* ordinary = qobject_cast<BrowserPane*>(win->controlWidget("mainwindow.brw"));
        QVERIFY(ordinary);
        QCOMPARE(ordinary->page()->backgroundColor(), QColor(Qt::white));
        const auto loads = win->browseHost()->origin()->stats().pages;
        auto set = [&](const char* target, const char* value) {
            auto result = live->winset(target, std::string("inner-background-color=") + value);
            QVERIFY(result.unhandled.empty());
            win->syncFromLive();
        };
        set("mainwindow.ghost", "#123456");
        QCOMPARE(pane->page()->backgroundColor(), QColor(0x12, 0x34, 0x56));
        set("ghost", "transparent");  // bare and qualified names: last writer wins
        QCOMPARE(pane->page()->backgroundColor(), QColor(Qt::transparent));
        std::string value;
        QVERIFY(live->winget("ghost", "inner-background-color", value));
        QCOMPARE(value, std::string("transparent"));
        set("ghost", "#12345678");
        QCOMPARE(pane->page()->backgroundColor(),
                 QColor(Qt::white));  // unsupported partial backing alpha must not reach Chromium
        set("ghost", "none");
        QCOMPARE(pane->page()->backgroundColor(), QColor(Qt::white));
        QCOMPARE(evs(pane, "retained"), QString("42"));
        QCOMPARE(win->browseHost()->origin()->stats().pages, loads);
    }
    void localOutputMacroReachesTheAddressedBrowser() {
        const QString html = R"(<script>
          window.calls=[];
          window.update=function(s){calls.push(JSON.parse(s).type)};
          window.api={collect:function(){calls.push(Array.from(arguments))}};
          window.output=api.collect;
        </script>)";
        win->showBrowse("brw", "window=brw", html);
        win->showBrowse("ghost", "window=ghost", html);
        auto* h = win->browseHost();
        auto* ghost = h->paneFor("ghost");
        auto* other = h->paneFor("brw");
        QVERIFY(ghost && other);
        QVERIFY(!ghost->ready());
        auto command = [&](const std::string& text) {
            const auto r = routeCommand(text, "", false);
            return r.kind == Route::Output &&
                   win->clientOutput(QString::fromStdString(r.text), QString::fromStdString(r.arg));
        };
        // Output before READY goes to the hidden page, not to the first browser in mainwindow.
        const std::string toggle = "%7B%22type%22%3A%22toggle%22%2C%22payload%22%3Anull%7D";
        QVERIFY(command(".output mainwindow.ghost:update " + toggle));
        QVERIFY(QTest::qWaitFor([&] { return ghost->ready() && other->ready(); }, 15000));
        QVERIFY(QTest::qWaitFor([&] { return evs(ghost, "JSON.stringify(calls)") == "[\"toggle\"]"; }, 5000));
        QCOMPARE(evs(other, "JSON.stringify(calls)"), QString("[]"));
        // TG's Escape order: show/adopt the browser, then deliver the toggle locally.
        const auto lines = macroCommandLines(
            ".winset \"mainwindow.ghost.is-visible=true\"\r\n.output mainwindow.ghost:update " + toggle);
        for (const auto& line : lines) {
            const auto r = routeCommand(line, "", false);
            if (r.kind == Route::Winset) {
                win->live()->clientWinset(r.winsetBody);
                win->syncFromLive();
            } else {
                QVERIFY(command(line));
            }
        }
        QCOMPARE(h->paneFor("ghost"), ghost);  // same document after adoption
        QVERIFY(
            QTest::qWaitFor([&] { return evs(ghost, "JSON.stringify(calls)") == "[\"toggle\",\"toggle\"]"; }, 5000));
        QCOMPARE(evs(other, "JSON.stringify(calls)"), QString("[]"));
        QVERIFY(command(".output mainwindow.brw:api.collect a%26b&%2526&%22%3Bwindow.injected%3Dtrue%3B%2F%2F&"));
        QVERIFY(QTest::qWaitFor([&] { return ev(other, "calls.length").toInt() == 1; }, 5000));
        const auto args = QJsonDocument::fromJson(evs(other, "JSON.stringify(calls[0])").toUtf8()).array();
        QCOMPARE(args.size(), 4);
        QCOMPARE(args[0].toString(), QString("a&b"));
        QCOMPARE(args[1].toString(), QString("%26"));
        QCOMPARE(args[2].toString(), QString("\";window.injected=true;//"));
        QCOMPARE(args[3].toString(), QString());
        QVERIFY(!ev(other, "Boolean(window.injected)").toBool());
        QVERIFY(command(".output mainwindow.brw:output"));
        QVERIFY(QTest::qWaitFor([&] { return ev(other, "calls.length").toInt() == 2; }, 5000));
        QCOMPARE(evs(other, "JSON.stringify(calls[1])"), QString("[\"\"]"));
        QVERIFY(command(".output mainwindow.brw:__proto__.collect ignored"));
        QCOMPARE(ev(other, "calls.length").toInt(), 2);  // existing function-path safety retained
        QVERIFY(!win->clientOutput("", "x"));
        QVERIFY(!win->clientOutput("mainwindow.brw", "x"));  // non-call .output forms remain unsupported
    }
    void dynamicBrowserPreloadAndDottedCalls() {
        auto live = win->live();
        auto* host = win->browseHost();
        // Actual server ordering: parent comes after type, size and pos.
        auto result = live->winset(
            "floating",
            "type=browser;size=1x1;pos=0,0;parent=mainwindow;anchor1=0,0;is-visible=0;is-disabled=1;use-title=1");
        QVERIFY(result.unhandled.empty());
        win->showBrowse("floating", "window=floating", R"(<style>body{background:red}</style><script>
          window.controller = new class { constructor(){this.calls=[];}
            init(s){this.calls.push('init:'+s)} update(s){this.calls.push('update:'+s)} hide(){this.calls.push('hide')} };
        </script>)");
        auto* p = host->paneFor("floating");
        QVERIFY(p);
        QVERIFY(!host->popupFor("floating"));
        QVERIFY(p->isHidden());
        QCOMPARE(p->size(), QSize(1, 1));
        const QString hostile = "quotes '\" \\ ; & \\n </script> \u2028";
        win->pageCall("floating", "controller.init", {hostile});
        win->pageCall("floating", "controller.update", {"second"});
        win->pageCall("floating", "controller.hide", {});
        for (const auto& path : {"controller.init();evil=1", "controller.__proto__.init", "controller.constructor",
                                 "controller..init", "controller['init']"})
            win->pageCall("floating", path, {});
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 8000);
        auto calls = QJsonDocument::fromJson(evs(p, "JSON.stringify(controller.calls)").toUtf8()).array();
        QCOMPARE(calls.size(), 3);
        QCOMPARE(calls[0].toString(), "init:" + hostile);
        QCOMPARE(calls[1].toString(), QString("update:second"));
        QCOMPARE(calls[2].toString(), QString("hide"));
        QCOMPARE(p->callsRun(), 3);
        ev(p,
           R"(window.answer=''; BYOND.winset('floating',{isVisible:true,size:'120x60',pos:'15,30'}).then(()=>BYOND.winget('floating',['size','isVisible','pos'])).then(v=>answer=JSON.stringify(v)))");
        QTRY_VERIFY_WITH_TIMEOUT(!evs(p, "answer").isEmpty(), 5000);
        auto answer = QJsonDocument::fromJson(evs(p, "answer").toUtf8()).object();
        QVERIFY(answer["size"].isObject());
        QVERIFY(answer["is-visible"].toBool());
        QTRY_VERIFY(!p->isHidden());
        QCOMPARE(p->size(), QSize(qRound(120 / win->devicePixelRatioF()), qRound(60 / win->devicePixelRatioF())));
        QVERIFY(p->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(p->focusPolicy(), Qt::NoFocus);
        const auto pages = host->origin()->stats().pages;
        live->winset("floating", "is-visible=false");
        win->syncFromLive();
        QVERIFY(p->isHidden());
        live->winset("floating", "is-visible=true");
        win->syncFromLive();
        QCOMPARE(host->paneFor("floating"), p);
        QCOMPARE(host->origin()->stats().pages, pages);
        QCOMPARE(evs(p, "controller.calls.length"), QString("3"));
        // A callback from a retired document cannot change the control.
        const auto oldDocument = p->document();
        win->showBrowse("floating", "window=floating", "<b>replacement</b>");
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 5000);
        ev(p, QString("fetch('/%1/__bridge?pane=%2',{method:'POST',body:'byond://"
                      "winset?id=floating&is-visible=false&__fugueDocument=%3'})")
                  .arg(host->origin()->secret())
                  .arg(p->paneId())
                  .arg(oldDocument));
        QTest::qWait(150);
        QVERIFY(!p->isHidden());
        ev(p, "domainStorage.setItem('cache','value');window.syncValue=domainStorage.getItem('cache')");
        QCOMPARE(evs(p, "syncValue"), QString("value"));
        QPointer<BrowserPane> old = p;
        live->winset("floating", "parent=null");
        win->syncFromLive();
        QVERIFY(old.isNull());
        QVERIFY(!host->paneFor("floating"));
        QVERIFY(!win->declaresBrowser("floating"));
        live->winset("floating", "parent=mainwindow;type=browser;size=1x1;is-visible=0");
        win->showBrowse("floating", "window=floating", "<script>window.fresh=true</script>");
        p = host->paneFor("floating");
        QVERIFY(p);
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 5000);
        QCOMPARE(ev(p, "fresh").toBool(), true);
        QCOMPARE(ev(p, "typeof controller").toString(), QString("undefined"));
        live->winset("floating", "parent=none");
        win->syncFromLive();
        QVERIFY(!host->paneFor("floating"));
    }
    void embeddedMapLifecycle() {
        auto* host = win->browseHost();
        win->live()->setPopups({"previewhost"});
        win->showBrowse("previewhost", "window=previewhost;size=300x300", "<p>MAP host</p>");
        auto* pane = host->paneFor("previewhost");
        QVERIFY(pane);
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(), 8000);
        const auto before = win->mapRoutes()->snapshot().size();
        const QString create = "byond://winset?id=preview&parent=previewhost&type=map&pos=12,24&size=120x160";
        host->handleCall(pane, fugue::model::parseByondUrl(create.toStdString()));
        win->syncFromLive();
        QPointer<QWidget> map = win->controlWidget("previewhost.preview");
        QVERIFY(map);
        QVERIFY(dynamic_cast<MapWidget*>(map.data()));
        QCOMPARE(map->parentWidget(), pane);
        const auto dpr = win->devicePixelRatioF();
        QCOMPARE(map->pos(), QPoint(qRound(12 / dpr), qRound(24 / dpr)));
        QCOMPARE(map->size(), QSize(qRound(120 / dpr), qRound(160 / dpr)));
        QCOMPARE(win->mapRoutes()->snapshot().size(), before + 1);
        win->live()->winset("preview", "parent=");
        win->syncFromLive();
        QVERIFY(map.isNull());
        QCOMPARE(win->mapRoutes()->snapshot().size(), before);
        host->handleCall(pane, fugue::model::parseByondUrl(create.toStdString()));
        win->syncFromLive();
        map = win->controlWidget("previewhost.preview");
        QVERIFY(map);
        win->closeBrowse("previewhost");
        QTRY_VERIFY(map.isNull());  // popup deletion is also route retirement
        QCOMPARE(win->mapRoutes()->snapshot().size(), before);
    }
    void runtimeQueuesAndRetirement() {
        auto live = win->live();
        auto* host = win->browseHost();
        live->winset("queued", "parent=mainwindow;type=browser;is-visible=0;size=1x1");
        const QString html = "<script>window.n=0;window.tick=()=>n++;</script>";
        win->showBrowse("queued", "window=queued", html);
        auto* pane = host->paneFor("queued");
        QVERIFY(pane);
        for (int i = 0; i < 1100; ++i) pane->callPage("tick", {});
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(), 8000);
        QCOMPARE(pane->callsRun(), 1024);
        QTRY_COMPARE(evs(pane, "n"), QString("1024"));
        win->showBrowse("queued", "window=queued", html);
        pane->callPage("tick", {});  // queued for a document that never becomes ready
        win->showBrowse("queued", "window=queued", html);
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(), 8000);
        QCOMPARE(evs(pane, "n"), QString("0"));
        ev(pane, "window.rejected=0;for(let i=0;i<300;i++)BYOND.winget('queued',['size']).catch(()=>rejected++);");
        QTRY_COMPARE(evs(pane, "rejected"), QString("44"));
        QPointer<BrowserPane> old = pane;
        live->clearRuntimeBrowsers();
        win->syncFromLive();
        QVERIFY(old.isNull());
        // Disconnect/recreate while the replacement document is still loading.
        live->winset("queued", "parent=mainwindow;type=browser;is-visible=0;size=1x1");
        win->showBrowse("queued", "window=queued", html);
        old = host->paneFor("queued");
        QVERIFY(old);
        win->pageCall("queued", "tick", {});
        live->clearRuntimeBrowsers();
        win->syncFromLive();
        QVERIFY(old.isNull());
        QVERIFY(!host->paneFor("queued"));
        live->winset("queued", "parent=mainwindow;type=browser;is-visible=0;size=1x1");
        win->showBrowse("queued", "window=queued", html);
        old = host->paneFor("queued");
        QVERIFY(win->loadSkin(kDockSkin).isEmpty());
        win->syncFromLive();
        QVERIFY(old.isNull());
    }
    // Stock 516.1688: a browser in an unattached pane runs even with its own is-visible=true.
    // Its ready handshake may be what tells the server to attach the pane (TG's chat startup).
    void anUnattachedPaneRunsBeforeAttachment() {
        const char* skin = R"(window "main"
    elem "main"
        type = MAIN
        size = 400x300
        is-default = true
    elem "split"
        type = CHILD
        size = 400x300
window "chatpane"
    elem "chatpane"
        type = MAIN
        size = 400x300
        is-pane = true
    elem "chatbrowser"
        type = BROWSER
        size = 400x300
)";
        QVERIFY(win->loadSkin(skin).isEmpty());
        auto* h = win->browseHost();
        QVERIFY(!win->declaresHiddenBrowser("chatbrowser"));
        QVERIFY(!win->controlWidget("chatpane.chatbrowser"));
        QSignalSpy topics(h, &BrowseHost::byondLink);
        h->open("chatbrowser", "window=chatbrowser",
                "<p id=state>waiting</p><script>window.n=0;"
                "window.tick=()=>document.getElementById('state').textContent=String(++n);"
                "window.onload=()=>location.href='byond://?ready=unattached';</script>");
        auto* p = h->paneFor("chatbrowser");
        QVERIFY(p);
        QVERIFY(h->isHiddenPage("chatbrowser"));
        QVERIFY(!h->popupFor("chatbrowser"));
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 15000);
        QTRY_COMPARE(topics.count(), 1);
        h->pageCall("chatbrowser", "tick", {});
        QTRY_COMPARE(evs(p, "n"), QString("1"));
        win->live()->winset("main.split", "left=chatpane");
        win->syncFromLive();
        QCOMPARE(win->controlWidget("chatpane.chatbrowser"), static_cast<QWidget*>(p));
        QVERIFY(p->isVisible());
        QVERIFY(!h->isHiddenPage("chatbrowser"));
        QCOMPARE(evs(p, "document.getElementById('state').textContent"), QString("1"));
        QCOMPARE(topics.count(), 1);  // adoption did not reload/re-run the startup script
    }
    void aDeclaredNameGoesIntoTheControlAnUndeclaredOneIntoAWindow() {
        BrowseHost* h = win->browseHost();
        QVERIFY(win->declaresBrowser("brw"));
        QVERIFY(win->declaresBrowser("mainwindow.brw"));
        QVERIFY(!win->declaresBrowser("pda"));
        h->open("brw", "window=brw", "<div id=d>docked</div>");
        QVERIFY(!h->popupFor("brw"));  // not a top-level window
        BrowserPane* p = h->paneFor("brw");
        QVERIFY(p);
        QCOMPARE(p->parentWidget(), win);  // it lives in the skin
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QCOMPARE(evs(p, "document.getElementById('d').textContent"), QString("docked"));
        h->open("mainwindow.brw", "window=mainwindow.brw",
                "<div id=d>second</div>");  // the "window.control" spelling: same control
        QVERIFY(QTest::qWaitFor(
            [&] { return evs(h->paneFor("brw"), "(document.getElementById('d')||{}).textContent") == "second"; },
            15000));
        h->open("pda", "window=pda;size=100x100", "<i>x</i>");
        QVERIFY(h->popupFor("pda"));
        // closing a docked control clears it but it stays a control
        h->close("brw");
        QVERIFY(win->browseHost()->popupFor("brw") == nullptr);
        h->close("pda");
    }
    // A browse() with display=0 and no window= is saved for the pages, never shown, and does not replace the default browser control.
    void aDisplayZeroBrowseIsSavedNotShown() {
        BrowseHost* h = win->browseHost();
        h->open("brw", "window=brw", "<div id=d>chat</div>");
        BrowserPane* p = h->paneFor("brw");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        h->open("", "file=asset_data.json&display=0", "{\"tgui.bundle.js\":\"d263\"}");
        QVERIFY(h->origin()->hasPage("asset_data.json"));
        QTest::qWait(300);
        QCOMPARE(evs(h->paneFor("brw"), "(document.getElementById('d')||{}).textContent"), QString("chat"));
    }
    // Stock DreamSeeker 516 re-enables Ctrl+F only when the world sets browser-options `find`; otherwise Ctrl+F is the page's key.
    // Either way a world macro bound to CTRL+F does not fire from a browser pane, and the find bar's field fires none either.
    void ctrlFOpensTheFindBarOnlyWhenTheWorldEnabledFind() {
        FocusFilter filt;
        fugue::skin::MacroSet game, global;
        fugue::skin::editUserMacro(global, "", "CTRL+F", "ctrlf macro");
        fugue::skin::editUserMacro(global, "", "A", "a macro");
        filt.setUserMacros(game, global);
        qApp->installEventFilter(&filt);
        QSignalSpy mac(&filt, &FocusFilter::macroFired);
        BrowseHost* h = win->browseHost();
        h->open("brw", "window=brw", kFindPage);
        BrowserPane* p = h->paneFor("brw");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        // Macros are live here: CTRL+F in an input-zone widget fires a macro, so the zero counts below are meaningful.
        auto* bar = new QLineEdit(win);
        bar->show();
        bar->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == bar; }));
        QTest::keyClick(bar, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(mac.count(), 1);
        QCOMPARE(mac.at(0).at(1).toString(), QString("ctrlf macro"));
        delete bar;
        mac.clear();

        // 1. browser-options "" : Ctrl+F goes to the page, no find bar, no macro
        QWidget* f = focusPage(p);
        QVERIFY(f && p->isAncestorOf(f));
        QTest::keyClick(f, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!p->findOpen());
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "kd.join(',')").contains("f^"); }, 5000));
        QCOMPARE(mac.count(), 0);

        // 2. the world enables find (vg's login.dm list, as the wire delivers it): the bar takes Ctrl+F
        win->live()->winset("", "browser-options=find,refresh,byondstorage");
        QVERIFY(win->live()->browserOptions() & 2u);
        ev(p, "kd=[]");
        QTest::keyClick(qApp->focusWidget(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(p->findOpen());
        QCOMPARE(qApp->focusWidget(), static_cast<QWidget*>(p->findField()));
        QCOMPARE(mac.count(), 0);
        QTest::qWait(300);
        QVERIFY(!evs(p, "kd.join(',')").contains("f^"));  // eaten: the page saw the Ctrl press, never the F
        // incremental search, Enter / Shift+Enter, no-match; typing 'a' in the field fires no macro.
        QTest::keyClicks(p->findField(), "apple");
        QVERIFY(QTest::qWaitFor([&] { return p->findStatus() == "1/3"; }, 8000));
        QCOMPARE(mac.count(), 0);
        QTest::keyClick(p->findField(), Qt::Key_Return);
        QVERIFY(QTest::qWaitFor([&] { return p->findStatus() == "2/3"; }, 8000));
        QTest::keyClick(p->findField(), Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(QTest::qWaitFor([&] { return p->findStatus() == "1/3"; }, 8000));
        QTest::keyClicks(p->findField(), "zzz");
        QVERIFY(QTest::qWaitFor([&] { return p->findStatus() == "No results"; }, 8000));
        // Ctrl+F again while open: stays open, field reselected
        QTest::keyClick(p->findField(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(p->findOpen());
        QCOMPARE(p->findField()->selectedText(), QString("applezzz"));
        // Esc closes and hands the keyboard back to the page
        QTest::keyClick(p->findField(), Qt::Key_Escape);
        QVERIFY(!p->findOpen());
        QVERIFY(QTest::qWaitFor(
            [&] {
                QWidget* w = qApp->focusWidget();
                return w && p->isAncestorOf(w);
            },
            5000));
        QCOMPARE(mac.count(), 0);
        // the platform's Find shortcuts open it too (Cmd+F on macOS)
        for (const QKeySequence& ks : QKeySequence::keyBindings(QKeySequence::Find)) {
            QTest::keySequence(qApp->focusWidget(), ks);
            QVERIFY(p->findOpen());
            QTest::keyClick(p->findField(), Qt::Key_Escape);
            QVERIFY(QTest::qWaitFor(
                [&] {
                    QWidget* w = qApp->focusWidget();
                    return w && p->isAncestorOf(w);
                },
                5000));
        }
        QCOMPARE(mac.count(), 0);
        // page inputs still type: plain keys reach the page's own <input>
        ev(p, "document.getElementById('i').focus()");
        QTest::keyClicks(qApp->focusWidget(), "xy");
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('i').value") == "xy"; }, 5000));
        QCOMPARE(mac.count(), 0);

        // 3. `-find` turns it off again
        win->live()->winset("", "browser-options=-find");
        QTest::keyClick(qApp->focusWidget(), Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!p->findOpen());
        qApp->removeEventFilter(&filt);
    }
    void propertySnapshotsRetainDistinctWriteRevisions() {
        fugue::skin::PropStore props;
        props.set("Probe", "is-visible", "0");
        const auto first = props.revision("probe", "IS-VISIBLE");
        QVERIFY(first != 0);
        const auto snapshot = props;
        props.set("probe", "is-visible", "0");
        QVERIFY(props.revision("probe", "is-visible") > first);
        QCOMPARE(snapshot.revision("probe", "is-visible"), first);
        props.set("other", "text", "unrelated");
        const auto beforeClear = props.revision();
        props.clear();
        QCOMPARE(props.revision("probe", "is-visible"), uint64_t(0));
        props.set("probe", "is-visible", "0");
        QVERIFY(props.revision("probe", "is-visible") > beforeClear);
    }
    void clonedPopupVisibilityHonorsRevisions() {
        fugue::skin::LiveSkin live;
        QVERIFY(live.load("window \"main\"\n elem \"main\"\n  type = MAIN\n  is-default = true\n").empty());
        live.cloneWindow("Cloned");
        QVERIFY(live.setClonePage("Cloned.browser", "<b>clone</b>"));
        live.winset("Cloned", "is-visible=1");
        std::map<std::string, uint64_t> open;
        auto actions = live.cloneActions(open);
        QCOMPARE(actions.size(), size_t(1));
        QVERIFY(actions[0].open);
        const auto beforeHide = live.propertyRevision();
        live.winset("Cloned", "is-visible=0");
        live.setPopupShown("cloned", true, beforeHide);  // stale, mixed-case GUI callback
        QVERIFY(!live.cloneWindows()[0].shown);
        actions = live.cloneActions(open);
        QCOMPARE(actions.size(), size_t(1));
        QVERIFY(!actions[0].open);
        live.winset("Cloned", "is-visible=1");
        live.setPopupShown("CLONED", false, live.propertyRevision());  // accepted player close
        QVERIFY(!live.cloneWindows()[0].shown);
        QCOMPARE(live.wingetParams("Cloned", "is-visible"), std::string("false"));
        live.winset("Cloned", "is-visible=");
        QVERIFY(!live.cloneWindows()[0].shown);  // normalized empty write also updates popup state
    }
    void coalescedBrowsePreservesLaterPopupWinsets() {
        auto live = win->live();
        live->setPopups({"probe"});
        const auto atBrowse = live->propertyRevision();
        const auto older = live->snapshot().props;
        live->winset("probe", "is-visible=0");  // arrives after browse, before the GUI handles either
        win->showBrowse("probe", "window=probe", "<title>Probe</title><b>hidden</b>", atBrowse);
        auto* h = win->browseHost();
        auto* w = h->popupFor("probe");
        QVERIFY(w && !w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("false"));
        h->syncPopupProps(older);
        QVERIFY(!w->isVisible());
        live->setPopupShown("probe", true, atBrowse);  // stale GUI publication cannot overwrite the world
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("false"));
        live->winset("probe", "is-visible=1");
        win->syncFromLive();
        QVERIFY(w->isVisible());
        h->close("probe");
    }
    // Stock 516.1688: tests/data/popup-visibility-world. Hiding retains the document and does not run on-close.
    void popupVisibilityFollowsWorldAndPageWrites() {
        auto live = win->live();
        auto* h = win->browseHost();
        live->setPopups({"probe"});
        const QString html = "<title>Visibility Fixture</title><script>window.n=0;window.tick=()=>++n;</script>";
        h->open("probe", "window=probe;size=240x160;can_resize=0", html);
        auto* p = h->paneFor("probe");
        auto* w = h->popupFor("probe");
        QVERIFY(p && w);
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 15000);
        QSignalSpy closed(h, &BrowseHost::popupClosedByPlayer);
        QSignalSpy onClose(h, &BrowseHost::onCloseCommand);
        live->winset("probe", "on-close=probe-closed;is-visible=0");
        win->syncFromLive();
        QVERIFY(!w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("false"));
        h->pageCall("probe.browser", "tick", {});
        QTRY_COMPARE(evs(p, "n"), QString("1"));
        QCOMPARE(h->paneFor("probe"), p);

        auto pageVisible = [&](bool shown) {
            ev(p, QString("location.href='byond://winset?id=probe&is-visible=%1'").arg(shown ? "true" : "false"));
        };
        pageVisible(true);
        QTRY_VERIFY(w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("true"));
        live->winset("mainwindow", "title=unrelated");
        win->syncFromLive();
        QVERIFY(w->isVisible());
        // Identical world value is a NEW write, not an unchanged snapshot to ignore.
        live->winset("probe", "is-visible=0");
        win->syncFromLive();
        QVERIFY(!w->isVisible());
        h->open("probe", "window=probe;size=240x160;can_resize=0", html);
        QTRY_VERIFY_WITH_TIMEOUT(p->ready(), 15000);
        QVERIFY(w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("true"));
        live->winset("mainwindow", "title=unrelated-again");
        win->syncFromLive();
        QVERIFY(w->isVisible());

        live->winset("probe", "is-visible=1");
        win->syncFromLive();
        pageVisible(false);
        QTRY_VERIFY(!w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("false"));
        live->winset("probe", "is-visible=1");
        win->syncFromLive();
        QVERIFY(w->isVisible());
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("true"));
        QCOMPARE(closed.count(), 0);
        QCOMPARE(onClose.count(), 0);
        h->close("probe");
        live->setPopups({});
        QCOMPARE(live->wingetParams("probe", "is-visible"), std::string("false"));
        QCOMPARE(live->wingetParams("probe", "title"), std::string());
        QCOMPARE(onClose.count(), 0);
    }
    // Stock DreamSeeker 516: winset(M, "p3", "title=Winset332") retitles an open popup. The last writer wins against the page's own title
    // (both set the title): unchanged snapshots must not replay old writes, but repeated world writes do apply.
    void aWorldWinsetOfTitleRetitlesAPopup() {
        BrowseHost* h = win->browseHost();
        win->live()->setPopups({"pda"});
        h->open("pda", "window=pda;size=100x100", "<html><head><title>Page</title></head><body>x</body></html>");
        QWidget* w = h->popupFor("pda");
        QVERIFY(w);
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle() == "Page"; }, 15000));
        win->live()->winset("pda", "title=\"From World\"");
        win->syncFromLive();
        QCOMPARE(w->windowTitle(), QString("From World"));
        ev(h->paneFor("pda"), "document.title = 'Page again'");
        QVERIFY(QTest::qWaitFor([&] { return w->windowTitle() == "Page again"; }, 5000));
        win->live()->winset("mainwindow", "title=\"other\"");  // some unrelated change bumps the skin's generation
        win->syncFromLive();
        QCOMPARE(w->windowTitle(), QString("Page again"));
        QCOMPARE(win->live()->wingetParams("pda", "title"), std::string("Page again"));
        win->live()->winset("pda", "title=\"From World\"");
        win->syncFromLive();
        QCOMPARE(w->windowTitle(), QString("From World"));
        QCOMPARE(win->live()->wingetParams("pda", "title"), std::string("From World"));
        h->close("pda");
    }
    void aPageForAnUnplacedControlIsHeldUntilItExists() {
        BrowseHost* h = win->browseHost();
        QVERIFY(win->declaresBrowser("ghost"));
        QVERIFY(!win->controlWidget("mainwindow.ghost"));  // is-visible = false: no widget yet
        h->open("ghost", "window=ghost", "<div id=g>held</div>");
        QVERIFY(!h->popupFor("ghost"));  // NOT a floating window
        win->live()->winset("mainwindow.ghost", "is-visible=true");
        win->syncFromLive();
        BrowserPane* p = h->paneFor("ghost");
        QVERIFY2(p, "the held page opened when the control appeared");
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QCOMPARE(evs(p, "document.getElementById('g').textContent"), QString("held"));
    }
    // vg's credits: output(file, "mapwindow.credits") into an is-visible=false BROWSER, set up by a page call while hidden;
    // the page then makes its own control visible. The control shows that same page, state and all, not a reload.
    void aHiddenControlKeepsItsPageWhenShown() {
        BrowseHost* h = win->browseHost();
        QVERIFY(!win->controlWidget("mainwindow.ghost"));
        win->showOutputFile(
            "mainwindow.ghost",
            "<div id=g>default</div><script>function setup(t){document.getElementById('g').textContent=t;}"
            "function start(){location='byond://winset?id=mainwindow.ghost;is-visible=true';}</script>");
        QVERIFY(h->isHiddenPage("mainwindow.ghost"));
        QVERIFY(!h->popupFor("mainwindow.ghost"));
        BrowserPane* p = h->paneFor("mainwindow.ghost");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        win->pageCall("mainwindow.ghost", "setup", {"rolled"});
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('g').textContent") == "rolled"; }, 5000));
        win->pageCall("mainwindow.ghost", "start", {});
        QVERIFY(QTest::qWaitFor(
            [&] {
                win->syncFromLive();
                return win->controlWidget("mainwindow.ghost") != nullptr;
            },
            5000));
        QCOMPARE(win->controlWidget("mainwindow.ghost"), static_cast<QWidget*>(p));  // the same browser
        QVERIFY(!p->isHidden());
        QVERIFY(!h->isHiddenPage("mainwindow.ghost"));
        QCOMPARE(evs(p, "document.getElementById('g').textContent"), QString("rolled"));
        win->showOutputFile("mainwindow.nosuch",
                            "<div>x</div>");  // not a browser control of the skin: no window of its own
        QVERIFY(!h->popupFor("mainwindow.nosuch") && !h->paneFor("mainwindow.nosuch"));
    }
    // A link() (vg links every client back to its own world and reboots while the credits roll) or a reboot sends the same
    // skin again. Through the app's skinLoaded handling, stock DreamSeeker keeps the shown control, its running page and
    // the macro set the world switched to.
    // tg's tgui_say: browse(html, "window=tgui_say;...") names a hidden skin WINDOW that holds a browser. Its page runs
    // there, windowless, not in a 480x600 popup at 0,0.
    void aHiddenSkinWindowWithABrowserRunsItsPageWindowless() {
        const std::string say =
            std::string(kDockSkin) +
            "\nwindow \"saybox\"\n\telem \"saybox\"\n\t\ttype = MAIN\n\t\tsize = 231x30\n\t\tis-visible = "
            "false\n\telem \"browser\"\n\t\ttype = BROWSER\n\t\tsize = 231x30\n";
        QVERIFY(win->loadSkin(say).isEmpty());
        QVERIFY(win->hiddenWindowBrowser("saybox"));
        QVERIFY(!win->hiddenWindowBrowser("mainwindow"));  // the main window is never hidden this way
        QVERIFY(!win->hiddenWindowBrowser("nosuch"));
        BrowseHost* h = win->browseHost();
        win->showBrowse("saybox", "window=saybox;titlebar=0;can_resize=0", "<div id=s>say</div>");
        QVERIFY(!h->popupFor("saybox"));
        QVERIFY(h->isHiddenPage("saybox"));
        BrowserPane* p = h->paneFor("saybox");
        QVERIFY(p && p->isHidden());
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        QCOMPARE(evs(p, "document.getElementById('s').textContent"), QString("say"));
        QCOMPARE(h->paneForTarget("saybox"), p);  // output(v, "saybox:fn") reaches that page
    }
    // Web storage outlives the run (tgui's chat panel stays blank until it finds its saved state), and a second
    // client on the same directory keeps none rather than share Chromium's stores.
    void webStorageIsKeptButNeverShared() {
        QTemporaryDir d;
        QVERIFY(d.isValid());
        BrowseHost::setStorageDir(d.path());
        {
            QWidget a, b;
            BrowseHost first(&a), second(&b);
            QVERIFY(!first.profile()->isOffTheRecord());
            QCOMPARE(first.profile()->persistentStoragePath(), d.path());
            QVERIFY(second.profile()->isOffTheRecord());
        }
        BrowseHost::setStorageDir(QString());
        QWidget c;
        BrowseHost off(&c);
        QVERIFY(off.profile()->isOffTheRecord());
    }
    // tg's say box: a winset of is-visible shows the floating window, which takes over the page that ran windowless
    // (no reload), and hiding it again keeps the page running.
    void aFloatingSkinWindowShowsItsPage() {
        const std::string say = std::string(kDockSkin) +
                                "\nwindow \"saybox\"\n\telem \"saybox\"\n\t\ttype = MAIN\n\t\tpos = 40,50\n\t\tsize = "
                                "231x30\n\t\tis-visible = "
                                "false\n\telem \"browser\"\n\t\ttype = BROWSER\n\t\tsize = 231x30\n";
        QVERIFY(win->loadSkin(say).isEmpty());
        BrowseHost* h = win->browseHost();
        win->showBrowse("saybox", "window=saybox", "<div id=s>say</div>");
        BrowserPane* p = h->paneFor("saybox");
        QVERIFY(p && h->isHiddenPage("saybox"));
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        ev(p, "window.kept = 'yes'");
        QVERIFY(!win->controlWidget("saybox.browser"));
        win->live()->winset("", "saybox.is-visible=true;saybox.size=300x40;saybox.browser.size=300x40");
        win->syncFromLive();
        QWidget* w = win->controlWidget("saybox.browser");
        QCOMPARE(w, static_cast<QWidget*>(p));
        QVERIFY(w->window() != win);
        QVERIFY(w->window()->isVisible());
        QCOMPARE(w->window()->size(), QSize(300, 40));
        QCOMPARE(w->size(), QSize(300, 40));
        QCOMPARE(evs(p, "window.kept"), QString("yes"));
        QCOMPARE(h->paneForTarget("saybox.browser"), p);  // `.output saybox.browser:fn` reaches it
        win->live()->winset("", "saybox.is-visible=false");
        win->syncFromLive();
        QVERIFY(!w->window()->isVisible());
        std::string v;
        QVERIFY(win->live()->winget("saybox", "is-visible", v));
        QCOMPARE(QString::fromStdString(v), QString("false"));
        // a later browse() into the window reloads the same pane
        win->showBrowse("saybox", "window=saybox", "<div id=s>again</div>");
        QCOMPARE(h->paneFor("saybox"), p);
    }
    // tg's chat: browseroutput lives in a pane no splitter shows until its own page swaps it in, so the page must run
    // before its control is on screen; the control then takes it over.
    void aBrowserInAnUnplacedPaneRunsItsPage() {
        const char* skin = R"SKIN(window "mainwindow"
	elem "mainwindow"
		type = MAIN
		size = 640x480
		is-default = true
	elem "sel"
		type = CHILD
		pos = 0,0
		size = 640x480
		anchor1 = 0,0
		anchor2 = 100,100
		left = "legacy"
window "legacy"
	elem "legacy"
		type = MAIN
		size = 640x480
		is-pane = true
	elem "output"
		type = OUTPUT
		size = 640x480
		anchor1 = 0,0
		anchor2 = 100,100
window "fancy"
	elem "fancy"
		type = MAIN
		size = 640x480
		is-pane = true
	elem "browseroutput"
		type = BROWSER
		size = 640x480
		anchor1 = 0,0
		anchor2 = 100,100
)SKIN";
        QVERIFY(win->loadSkin(skin).isEmpty());
        BrowseHost* h = win->browseHost();
        win->showBrowse("browseroutput", "", "<div id=c>chat</div>");
        BrowserPane* p = h->paneFor("browseroutput");
        QVERIFY(p && h->isHiddenPage("browseroutput"));
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        win->live()->winset("mainwindow.sel", "left=fancy");
        win->syncFromLive();
        QCOMPARE(win->controlWidget("fancy.browseroutput"), static_cast<QWidget*>(p));
        QVERIFY(p->isVisible());
        QCOMPARE(evs(p, "document.getElementById('c').textContent"), QString("chat"));
    }
    // --size WxH outlives the world's skin: tg's mainwindow is 640x440 and must not shrink a 1500x950 window.
    void aUserSizeIsKeptWhenTheWorldSkinLoads() {
        FocusFilter filt;
        bool sized = false;
        QVERIFY(!fugue::shell::applyUserSize(*win, "big", sized));
        QVERIFY(!sized);
        QVERIFY(fugue::shell::applyUserSize(*win, "900x700", sized));
        QVERIFY(sized);
        fugue::shell::worldSkinLoaded(*win, filt, sized);
        QCOMPARE(win->size(), QSize(900, 700));
    }
    void theSameSkinAgainKeepsTheWindowsState() {
        FocusFilter filt;
        qApp->installEventFilter(&filt);
        QSignalSpy mac(&filt, &FocusFilter::macroFired);
        bool sized = true;
        BrowseHost* h = win->browseHost();
        win->showOutputFile("mainwindow.ghost", "<div id=g>default</div>");
        BrowserPane* p = h->paneFor("mainwindow.ghost");
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        ev(p, "document.getElementById('g').textContent = 'rolling'");
        win->live()->winset("mainwindow.ghost", "is-visible=true");
        win->live()->winset("mainwindow", "macro=hotkeymode");
        fugue::shell::worldSkinLoaded(*win, filt, sized);
        QCOMPARE(win->controlWidget("mainwindow.ghost"), static_cast<QWidget*>(p));
        QCOMPARE(win->activeMacro(), QString("hotkeymode"));
        fugue::skin::SkinFeed feed(win->live());  // the next session's skin frame
        std::string body = std::string(kDockSkin) + '\0';
        feed.onFrame(fugue::S2C::SkinText, fugue::ByteSpan(reinterpret_cast<const uint8_t*>(body.data()), body.size()));
        QCOMPARE(feed.counters().skinLoads, uint64_t(1));
        fugue::shell::worldSkinLoaded(*win, filt, sized);
        std::string v;
        QVERIFY(win->live()->winget("mainwindow.ghost", "is-visible", v));
        QCOMPARE(QString::fromStdString(v), QString("true"));
        QCOMPARE(win->controlWidget("mainwindow.ghost"), static_cast<QWidget*>(p));
        QVERIFY(!p->isHidden());
        QCOMPARE(evs(p, "document.getElementById('g').textContent"), QString("rolling"));
        QCOMPARE(win->activeMacro(), QString("hotkeymode"));
        auto* bar = new QLineEdit(win);
        bar->show();
        bar->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == bar; }));
        QTest::keyClick(bar, Qt::Key_K, Qt::ControlModifier);  // bound in hotkeymode only
        QCOMPARE(mac.count(), 1);
        QCOMPARE(mac.at(0).at(1).toString(), QString("kk"));
        delete bar;
        qApp->removeEventFilter(&filt);
    }
    // A hidden 1x1 BROWSER fed by browse() with no window of its own, then driven by output() to SetMusic. The page runs
    // windowless, and its play() starts with no user gesture. The page reports "playing" back as a Topic.
    void aHiddenControlsPageRunsWindowlessAndAutoplays_data() {
        QTest::addColumn<bool>("localResource");
        QTest::newRow("data-url") << false;
        QTest::newRow("browse-rsc") << true;
    }
    void aHiddenControlsPageRunsWindowlessAndAutoplays() {
        QFETCH(bool, localResource);
        BrowseHost* h = win->browseHost();
        QVERIFY(win->declaresHiddenBrowser("ghost"));
        QVERIFY(win->declaresHiddenBrowser("mainwindow.ghost2"));
        QVERIFY(!win->declaresHiddenBrowser("brw"));
        // a 0.5 s 440 Hz mono 16-bit WAV
        std::string wav;
        auto le = [&](uint32_t v, int n) {
            for (int i = 0; i < n; i++) wav.push_back(char((v >> (8 * i)) & 0xff));
        };
        const int rate = 8000, n = rate / 2;
        wav += "RIFF";
        le(36 + n * 2, 4);
        wav += "WAVEfmt ";
        le(16, 4);
        le(1, 2);
        le(1, 2);
        le(rate, 4);
        le(rate * 2, 4);
        le(2, 2);
        le(16, 2);
        wav += "data";
        le(n * 2, 4);
        for (int i = 0; i < n; i++) le(uint32_t(int16_t(8000 * std::sin(2 * M_PI * 440 * i / rate))) & 0xffff, 2);
        // Exercise both streamed media and a named browse_rsc asset (TG menu's path).
        // The latter deliberately arrives after Chromium requests it; no data-URL bypass.
        const QString url =
            localResource ? "menu-tone.wav"
                          : "data:audio/wav;base64," + QString::fromLatin1(QByteArray::fromStdString(wav).toBase64());
        if (localResource) h->store()->declare("menu-tone.wav", 9001);
        QSignalSpy topics(win, &SkinWindow::byondLink);
        // A jukebox page, reduced: SetMusic(url, time, volume) calls play() with no gesture and reports which page played.
        auto page = [](const char* who) {
            return QString(R"(<video id=player></video><script>
function SetMusic(url, time, volume) {
  var v = document.getElementById('player');
  v.src = url; v.volume = volume / 100;
  v.play().then(function () { location = '?jukebox=playing&who=%1'; }, function (e) { location = '?jukebox=refused&e=' + encodeURIComponent(e.name); });
}</script>)")
                .arg(who);
        };
        // both spellings vg could use: the window-qualified one (vg's own) and the bare control
        win->showBrowse("mainwindow.ghost", "window=mainwindow.ghost", page("one"));
        win->showBrowse("ghost2", "window=ghost2", page("two"));
        for (const char* name : {"mainwindow.ghost", "ghost2"}) {
            QVERIFY(h->isHiddenPage(name));
            QVERIFY(!h->popupFor(name));  // NOT a floating window
            BrowserPane* p = h->paneFor(name);
            QVERIFY2(p, "a hidden page has a pane");
            QVERIFY(p->isHidden());
            QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        }
        QVERIFY(!win->controlWidget("mainwindow.ghost") &&
                !win->controlWidget("mainwindow.ghost2"));  // no rectangle in the skin
        // output(list2params(url, time, volume), "mainwindow.ghost2:SetMusic") as the session delivers it: the target names the control;
        // "mainwindow" alone names neither page.
        if (localResource) {
            const auto route = routeCommand(".output mainwindow.ghost2:SetMusic menu-tone.wav&0&50", "", false);
            QCOMPARE(route.kind, Route::Output);
            QVERIFY(win->clientOutput(QString::fromStdString(route.text), QString::fromStdString(route.arg)));
        } else {
            win->pageCall("mainwindow.ghost2", "SetMusic", {url, "0", "50"});
        }
        if (localResource) {
            QVERIFY(QTest::qWaitFor([&] { return h->origin()->held() > 0; }, 3000));
            h->store()->put(9001, "sound/generated-tone.wav", wav);
        }
        auto saw = [&](const char* q) {
            for (auto& a : topics)
                if (a.at(0).toUrl().toEncoded().contains(q)) return true;
            return false;
        };
        QVERIFY(QTest::qWaitFor([&] { return saw("jukebox="); }, 10000));
        QVERIFY2(saw("jukebox=playing&who=two"),
                 "play() was refused (autoplay must need no user gesture) or reached the wrong page");
        topics.clear();
        win->pageCall("mainwindow.ghost", "SetMusic", {url, "0", "50"});
        QVERIFY(QTest::qWaitFor([&] { return saw("jukebox=playing&who=one"); }, 10000));
        QVERIFY(h->paneFor("ghost2")->isHidden());  // still never shown
        // browse(null), then a fresh body, as a login does
        h->close("ghost2");
        QVERIFY(!h->paneFor("ghost2"));
        win->showBrowse("ghost2", "window=ghost2", "<div id=g>again</div>");
        BrowserPane* p = h->paneFor("ghost2");
        QVERIFY(p && h->isHiddenPage("ghost2"));
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "(document.getElementById('g')||{}).textContent") == "again"; },
                                15000));
    }
    void pageWinsetReachesTheLiveSkin() {
        BrowseHost* h = win->browseHost();
        h->open("brw", "window=brw", "<div>x</div>");
        BrowserPane* p = h->paneFor("brw");
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        ev(p, "location = 'byond://winset?id=mainwindow&macro=hotkeymode'");
        QVERIFY(QTest::qWaitFor([&] { return win->activeMacro() == "hotkeymode"; }, 5000));
        // winget of a skin control, through the live skin
        ev(p,
           "window.wg=''; window.gotWinget = function (o) { window.wg = JSON.stringify(o); }; location = "
           "'byond://winget?id=mainwindow.brw&property=type,is-visible&callback=gotWinget'");
        QVERIFY(QTest::qWaitFor([&] { return !evs(p, "window.wg").isEmpty(); }, 5000));
        QJsonObject o = QJsonDocument::fromJson(evs(p, "window.wg").toUtf8()).object();
        QCOMPARE(o.value("type").toString(), QString("BROWSER"));
        QCOMPARE(o.value("is-visible").toBool(), true);
        // the macro set, flat, keyed set.macro.property (goon's wingetMacros regex runs over these keys)
        ev(p, "window.wg=''; location = 'byond://winget?callback=gotWinget&id=hotkeymode.*&property=command'");
        QVERIFY(QTest::qWaitFor([&] { return !evs(p, "window.wg").isEmpty(); }, 5000));
        o = QJsonDocument::fromJson(evs(p, "window.wg").toUtf8()).object();
        QCOMPARE(o.value("hotkeymode.T.command").toString(), QString("say"));
    }
};

// ---- a BROWSER control a winset creates inside a skin window (vg's tooltips) --------------------------------------
// vg's code/modules/tooltip: winset(tooltipN, "parent=mapwindow;type=browser;pos=0,0;size=1x1;anchor1=0,0;
// is-visible=0;is-disabled=1"), then browse(html, "window=tooltipN"), then output() calls to tooltip.init / tooltip.hide;
// the page sizes, places and shows its own control with BYOND.winset and reads the map with BYOND.winget.
static const char* kTooltipPage =
    "<body style='margin:0'><div id=s>idle</div><script>"
    "window.tooltip = { init: function (a) { this.last = 'init:' + a; document.getElementById('s').textContent = "
    "this.last; }, hide: function () { document.getElementById('s').textContent = 'hidden'; } };"
    "</script></body>";

class RuntimeBrowserTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
    const std::string create =
        "parent=mapwindow;type=browser;pos=0,0;size=1x1;anchor1=0,0;is-visible=0;is-disabled=1;background-color=%23000";
private slots:
    void init() {
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kLiveSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
    }
    void cleanup() {
        delete win;
        win = nullptr;
    }

    // The whole life of one tooltip: created hidden in mapwindow, its page held until the control exists and run while
    // hidden, shown and placed by its own page inside the map's pane, hidden again, and destroyed with parent=none.
    void aTooltipLivesInsideTheMapPane() {
        const QString id = "tooltip07045140117552442";
        BrowseHost* h = win->browseHost();
        win->live()->winset(id.toStdString(), create, true);
        // The browse() arrives before the GUI synced the winset: it is held for the control, never a window.
        QVERIFY(win->declaresBrowser(id));
        win->showBrowse(id, "window=" + id, kTooltipPage);
        QVERIFY(!h->popupFor(id));
        BrowserPane* p = win->runtimeBrowser(id);
        QVERIFY2(p, "the control is built at the winset, hidden");
        QCOMPARE(p->parentWidget(), win);
        QVERIFY(p->isHidden());
        QVERIFY(
            p->testAttribute(Qt::WA_TransparentForMouseEvents));  // is-disabled: no hit testing, the page still runs
        QCOMPARE(h->paneFor(id), p);
        QVERIFY2(QTest::qWaitFor([&] { return p->ready(); }, 15000), "its page runs while hidden");
        // output(.., "tooltipN:tooltip.init"): a dotted function name, called on its object
        h->pageCall(id, "tooltip.init", {"{\"a\":1}"});
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('s').textContent") == "init:{\"a\":1}"; },
                                5000));
        // The page shows itself: BYOND.winset with camelCase names, pos/size in the window's coordinates.
        ev(p, "BYOND.winset('" + id + "', { isVisible: true, size: '120x40', pos: '10,20' })");
        QWidget* map = win->controlWidget("mapwindow.map");
        QVERIFY(map);
        QVERIFY(QTest::qWaitFor(
            [&] {
                win->syncFromLive();
                return p->isVisible();
            },
            5000));
        QCOMPARE(p->geometry(), QRect(map->geometry().topLeft() + QPoint(10, 20), QSize(120, 40)));
        QVERIFY2(win->children().indexOf(p) > win->children().indexOf(map), "stacked above the map");
        std::string v;
        QVERIFY(win->live()->winget(id.toStdString(), "is-visible", v));
        QCOMPARE(v, std::string("true"));
        // The tooltip hides itself.
        ev(p, "BYOND.winset('" + id + "', { isVisible: false })");
        QVERIFY(QTest::qWaitFor(
            [&] {
                win->syncFromLive();
                return p->isHidden();
            },
            5000));
        h->pageCall(id, "tooltip.hide", {""});
        QVERIFY(QTest::qWaitFor([&] { return evs(p, "document.getElementById('s').textContent") == "hidden"; }, 5000));
        // A browse() or output() naming the window mapwindow is not this control's.
        QVERIFY(!h->paneFor("mapwindow"));
        QVERIFY(!h->paneForTarget("mapwindow.map"));
        // parent=none destroys it and its page; the name is then no control at all.
        QPointer<BrowserPane> gone(p);
        win->live()->winset(id.toStdString(), "parent=none", true);
        win->syncFromLive();
        QVERIFY(!win->runtimeBrowser(id));
        QVERIFY(gone.isNull());
        QVERIFY(!h->paneFor(id));
        QVERIFY(!win->declaresBrowser(id));
    }

    // A page sent first and a control created after it: the page goes to the control once the control exists, as for a
    // .dmf control the layout has not placed yet. A world that never makes the control gets its window.
    void aControlBuiltAfterTheSyncTakesTheHeldPage() {
        const QString id = "tooltip1";
        win->live()->winset(id.toStdString(), create, true);
        win->syncFromLive();
        BrowserPane* p = win->runtimeBrowser(id);
        QVERIFY(p);
        QVERIFY(p->name.isEmpty());  // no page yet
        win->showBrowse(id, "window=" + id, kTooltipPage);
        QCOMPARE(win->browseHost()->paneFor(id), p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        // a second tooltip with the same creation gets its own control
        win->live()->winset("tooltip2", create, true);
        win->syncFromLive();
        QVERIFY(win->runtimeBrowser("tooltip2") && win->runtimeBrowser("tooltip2") != p);
        // and vg's clearAll finds both through winget(mapwindow.*, id)
        std::string ids = win->live()->wingetWildcard("mapwindow.*", "id");
        QVERIFY2(ids.find("tooltip1") != std::string::npos && ids.find("tooltip2") != std::string::npos, ids.c_str());
        // a browse() into a name nothing created is still its own window
        win->showBrowse("tooltip3", "window=tooltip3", kTooltipPage);
        QVERIFY(win->browseHost()->popupFor("tooltip3"));
        win->closeBrowse("tooltip3");
    }

    // vg's tooltip.js reads the map's `size` and `view-size` with BYOND.winget as {x, y} objects; view-size is the
    // picture inside the control, letterbox excluded.
    void byondWingetAnswersTheMapsSizes() {
        const QString id = "tooltip9";
        win->live()->winset(id.toStdString(), create, true);
        win->showBrowse(id, "window=" + id, kTooltipPage);
        BrowserPane* p = win->runtimeBrowser(id);
        QVERIFY(p);
        QVERIFY(QTest::qWaitFor([&] { return p->ready(); }, 15000));
        auto* map = dynamic_cast<MapWidget*>(win->controlWidget("mapwindow.map"));
        QVERIFY(map);
        const QSize shown = map->presentedViewSize();
        ev(p,
           "window.ws = ''; BYOND.winget('mapwindow.map', ['size', 'viewSize']).then(function (o) { window.ws = "
           "JSON.stringify(o); })");
        QVERIFY(QTest::qWaitFor([&] { return !evs(p, "window.ws").isEmpty(); }, 5000));
        QJsonObject o = QJsonDocument::fromJson(evs(p, "window.ws").toUtf8()).object();
        QCOMPARE(o.value("size").toObject().value("x").toInt(), map->width());
        QCOMPARE(o.value("size").toObject().value("y").toInt(), map->height());
        QCOMPARE(o.value("view-size").toObject().value("x").toInt(), shown.width());
        QCOMPARE(o.value("view-size").toObject().value("y").toInt(), shown.height());
        // BYOND.command runs a command as if typed; BYOND.topic is a Topic
        QSignalSpy cmd(win, &SkinWindow::commandEntered);
        QSignalSpy topic(win, &SkinWindow::byondLink);
        ev(p, "BYOND.command('say hi'); BYOND.topic({src: '[0x1]', action: 'loaded'})");
        QVERIFY(QTest::qWaitFor([&] { return cmd.count() == 1 && topic.count() == 1; }, 5000));
        QCOMPARE(cmd.at(0).at(0).toString(), QString("say hi"));
        QVERIFY(topic.at(0).at(0).toUrl().toString(QUrl::FullyDecoded).contains("action=loaded"));
    }
};

template<class Test>
static int runBrowserTest(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    Test test;
    return QTest::qExec(&test, argc, argv);
}

int FUGUE_TEST_ENTRY(int argc, char** argv) {
    return runBrowserTest<BrowseTest>(argc, argv);
}

int fugue_run_browse_dock(int argc, char** argv) {
    return runBrowserTest<DockTest>(argc, argv);
}

int fugue_run_browse_runtime(int argc, char** argv) {
    return runBrowserTest<RuntimeBrowserTest>(argc, argv);
}
#include "browse_test.moc"
