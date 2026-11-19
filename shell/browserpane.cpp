#include "shell/browserpane.h"
#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QToolButton>
#include <QWebEngineFindTextResult>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <cstdio>
#include "shell/localorigin.h"
#include "shell/paste.h"

namespace fugue::shell {

void registerSchemes() {
    QWebEngineUrlScheme b("byond");
    b.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    b.setFlags(QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(b);
}

namespace {
// Registering the scheme is not enough from an http(s) or data page: Chromium blocks a byond:// navigation before
// acceptNavigationRequest runs unless the profile also has a handler for the scheme. The handler itself never runs,
// because acceptNavigationRequest cancels the navigation first.
struct ByondStub : QWebEngineUrlSchemeHandler {
    void requestStarted(QWebEngineUrlRequestJob* job) override { job->fail(QWebEngineUrlRequestJob::RequestAborted); }
};
void ensureByondHandler(QWebEngineProfile* p) {
    static ByondStub stub;
    if (p->property("fugueByondHandler").toBool()) return;
    p->installUrlSchemeHandler("byond", &stub);
    p->setProperty("fugueByondHandler", true);
}

// Ctrl+F (and the platform's Find shortcut, Cmd+F on macOS), app-wide. The key arrives at Chromium's focus-proxy child (or the find bar's field), not at the
// BrowserPane, so it is caught here before delivery. Only a pane whose world enabled `find` consumes it; otherwise
// the event passes through and the page sees it.
struct FindKeyFilter : QObject {
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::KeyPress || o != QApplication::focusWidget()) return false;
        auto* k = static_cast<QKeyEvent*>(e);
        if (!k->matches(QKeySequence::Find) &&
            (k->key() != Qt::Key_F || (k->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier |
                                                         Qt::MetaModifier)) != Qt::ControlModifier))
            return false;
        BrowserPane* pane = nullptr;
        for (QObject* p = o; p && !pane; p = p->parent()) pane = qobject_cast<BrowserPane*>(p);
        if (!pane || !pane->findAllowed || !pane->findAllowed()) return false;
        if (!k->isAutoRepeat()) pane->openFind();  // auto-repeats are swallowed and do not reopen the bar
        return true;
    }
};
void ensureFindKeys() {
    static FindKeyFilter* f = nullptr;
    if (f || !qApp) return;
    f = new FindKeyFilter;
    f->setParent(qApp);
    qApp->installEventFilter(f);
}

// The find bar's field: Enter and Shift+Enter step through matches, Esc closes (as the Edge find bar does).
struct FindFieldKeys : QObject {
    BrowserPane* pane;
    explicit FindFieldKeys(BrowserPane* p) : QObject(p), pane(p) {}
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() != QEvent::KeyPress) return false;
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape) {
            pane->closeFind();
            return true;
        }
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            pane->findNext(k->modifiers() & Qt::ShiftModifier);
            return true;
        }
        return false;
    }
};
}  // namespace

bool BrowserPage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) {
    if (url.scheme() == "byond") {
        std::fprintf(stderr, "[shell] byond:// intercepted (%s): %s\n",
                     type == NavigationTypeRedirect      ? "redirect"
                     : type == NavigationTypeLinkClicked ? "click"
                                                         : "other",
                     url.toString().left(200).toUtf8().constData());
        emit byondLink(url);
        return false;
    }
    if (!isMainFrame) return true;
    const QString sc = url.scheme();
    if (sc == "about" || sc == "data" || sc == "blob" || sc == "qrc") return true;
    if (origin_ && origin_->owns(url)) {
        if (url.hasQuery() && origin_->hasPage(origin_->fileOf(url))) {
            std::fprintf(stderr, "[shell] page navigation with a query is a Topic: %s\n",
                         url.query().left(160).toUtf8().constData());
            emit topicQuery("?" + QString::fromLatin1(url.query(QUrl::FullyEncoded).toLatin1()));
            return false;
        }
        // A real load of one of our documents (a reload, or a re-browse via openFile) voids the old READY. Not
        // loadStarted: Chromium fires that even for navigations acceptNavigationRequest cancels, such as every Topic.
        // A fragment-only change is the same document.
        if (type == NavigationTypeReload ||
            url.adjusted(QUrl::RemoveFragment) != this->url().adjusted(QUrl::RemoveFragment))
            emit documentChanging();
        return true;
    }
    if (sc == "http" || sc == "https" || sc == "mailto" || sc == "ftp") {
        // A script-assigned location arrives as a link click too, so a page can open the desktop browser without a
        // gesture, as in stock DreamSeeker. Rate-limited so a page cannot spam tabs.
        static QElapsedTimer lastOpen;
        if (type == NavigationTypeLinkClicked && (!lastOpen.isValid() || lastOpen.elapsed() > 1000)) {
            lastOpen.restart();
            QDesktopServices::openUrl(url);
        } else
            std::fprintf(stderr, "[shell] refused non-click navigation to %s\n",
                         url.toString().left(120).toUtf8().constData());
        return false;
    }
    return !origin_;  // with an origin, nothing else may take the pane (file:, javascript:)
}

void BrowserPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& msg, int line,
                                           const QString& src) {
    std::fprintf(stderr, "[page] %s (%s:%d)\n", msg.toUtf8().constData(), src.toUtf8().constData(), line);
}

BrowserPane::BrowserPane(QWidget* parent, QWebEngineProfile* profile) : QWebEngineView(parent) {
    if (!profile) profile = QWebEngineProfile::defaultProfile();
    ensureByondHandler(profile);
    page_ = new BrowserPage(profile, this);
    setPage(page_);
    connect(page_, &BrowserPage::byondLink, this, &BrowserPane::byondLink);
    connect(page_, &BrowserPage::topicQuery, this, &BrowserPane::topicQuery);
    connect(page_, &BrowserPage::documentChanging, this, [this] { ready_ = false; });
    connect(page_, &QWebEnginePage::findTextFinished, this, &BrowserPane::findResult);
    ensureFindKeys();
}

// Find bar (Ctrl+F; see browserpane.h): a floating box at the pane's top-right, built on first use.
void BrowserPane::ensureFindBar() {
    if (findBar_) return;
    auto* bar = new QFrame(this);
    bar->setObjectName("fugueFindBar");
    bar->setProperty("fugueZone", "other");  // no world macro fires while typing in it (focusfilter.cpp)
    bar->setFrameShape(QFrame::StyledPanel);
    bar->setAutoFillBackground(true);
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(2);
    findEdit_ = wirePaste(new QLineEdit(bar));
    findEdit_->setPlaceholderText("Find on page");
    findEdit_->setMinimumWidth(160);
    findEdit_->installEventFilter(new FindFieldKeys(this));
    findCount_ = new QLabel(bar);
    findCount_->setMinimumWidth(56);
    auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        lay->addWidget(b);
        return b;
    };
    lay->addWidget(findEdit_);
    lay->addWidget(findCount_);
    connect(button(QString::fromUtf8("\u2191"), "Previous result (Shift+Enter)"), &QToolButton::clicked, this,
            [this] { findNext(true); });
    connect(button(QString::fromUtf8("\u2193"), "Next result (Enter)"), &QToolButton::clicked, this,
            [this] { findNext(false); });
    connect(button(QString::fromUtf8("\u2715"), "Close (Esc)"), &QToolButton::clicked, this, [this] { closeFind(); });
    // Incremental: every edit searches from the top again.
    connect(findEdit_, &QLineEdit::textEdited, this, [this](const QString& t) {
        if (t.isEmpty()) {
            page_->findText(QString());
            findCount_->clear();
            return;
        }
        page_->findText(t);
    });
    bar->hide();
    findBar_ = bar;
}

void BrowserPane::placeFindBar() {
    if (!findBar_) return;
    findBar_->adjustSize();
    QSize s = findBar_->sizeHint().boundedTo(size());
    findBar_->setGeometry(qMax(0, width() - s.width() - 8), 4, s.width(), s.height());
    findBar_->raise();
}

void BrowserPane::resizeEvent(QResizeEvent* e) {
    QWebEngineView::resizeEvent(e);
    placeFindBar();
}

void BrowserPane::openFind() {
    ensureFindBar();
    findBar_->show();
    placeFindBar();
    findEdit_->setFocus(Qt::ShortcutFocusReason);
    findEdit_->selectAll();
    if (!findEdit_->text().isEmpty()) page_->findText(findEdit_->text());  // reopened: highlight the last term
}

void BrowserPane::closeFind() {
    if (!findBar_ || findBar_->isHidden()) return;
    page_->findText(QString());  // clears the highlight and the active match
    findCount_->clear();
    findBar_->hide();
    setFocus(Qt::OtherFocusReason);
}

void BrowserPane::findNext(bool backward) {
    if (!findEdit_ || findEdit_->text().isEmpty()) return;
    page_->findText(findEdit_->text(), backward ? QWebEnginePage::FindBackward : QWebEnginePage::FindFlags());
}

bool BrowserPane::findOpen() const { return findBar_ && !findBar_->isHidden(); }

QString BrowserPane::findStatus() const { return findCount_ ? findCount_->text() : QString(); }

void BrowserPane::findResult(const QWebEngineFindTextResult& r) {
    if (!findCount_ || !findEdit_ || findEdit_->text().isEmpty()) return;
    findCount_->setText(r.numberOfMatches() ? QString("%1/%2").arg(r.activeMatch()).arg(r.numberOfMatches())
                                            : QString("No results"));
}

void BrowserPane::showHtml(const QString& html) { setHtml(html); }

// The script stock DreamSeeker's embedded browser gives every page. It runs at DocumentCreation, before the page's
// own scripts, because tgui's helpers decide `isByond` once, at load, from window.cef_to_byond.
static QString bridgeScript(const QString& bridgeUrl, const QString& host) {
    return QString(R"JS((function () {
  'use strict';
  if (location.host !== '%1') return;                 // a foreign page in a subframe gets nothing
  if (window.__fugueBridge) return; window.__fugueBridge = 1;
  var BR = '%2';
  function post(url) {
    try {
      var x = new XMLHttpRequest();
      x.open('POST', BR, true);
      x.onload = function () { var t = x.responseText; if (t) { try { (0, eval)(t); } catch (e) {} } };
      x.send(String(url));
    } catch (e) {}
  }
  window.cef_to_byond = post;
  function hubCall(op, key, value) {
    return new Promise(function (resolve) {
      var cbs = window.Byond && window.Byond.__callbacks__;
      if (!cbs) { resolve(null); return; }
      var i = cbs.length; cbs.push(resolve);
      post('byond://hubStorage?op=' + encodeURIComponent(op) + '&key=' + encodeURIComponent(key == null ? '' : key) +
           '&value=' + encodeURIComponent(value == null ? '' : value) + '&callback=Byond.__callbacks__[' + i + ']');
    });
  }
  window.hubStorage = {
    getItem: function (k) { return hubCall('get', k); }, setItem: function (k, v) { return hubCall('set', k, v); },
    removeItem: function (k) { return hubCall('remove', k); }, clear: function () { return hubCall('clear'); }
  };
  // The BYOND object (DM reference, "BYOND object (JavaScript)"): winset and winget take camelCase names as their
  // hyphenated form; winget resolves to an object keyed by the hyphenated names. A size ("WxH") comes back as {x, y}
  // and a colour as {value, red, green, blue, isDefault}: vg's tooltip.js reads map `size` and `view-size` as .x/.y.
  var enc = encodeURIComponent, cbq = window.__fugueByond = [];
  function kebab(k) { return String(k).replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); }); }
  function wsval(k, v) {
    if (v === true || v === false) return String(v);
    if (v == null) return '';
    if (typeof v === 'object') {
      if ('value' in v) return String(v.value);
      if ('x' in v && 'y' in v) return v.x + (/size$/.test(k) ? 'x' : ',') + v.y;
    }
    return String(v);
  }
  function wgval(v) {
    if (typeof v !== 'string') return v;
    var m = /^(-?\d+)x(-?\d+)$/.exec(v);
    if (m) return { x: +m[1], y: +m[2] };
    m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(v);
    if (m) return { value: v, red: parseInt(m[1], 16), green: parseInt(m[2], 16), blue: parseInt(m[3], 16), isDefault: false };
    return v;
  }
  var BYOND = {
    winset: function (id, params) {
      var q = [];
      if (id != null && id !== '' && id !== 0) q.push('id=' + enc(id));
      for (var k in params || {}) { var kk = kebab(k); q.push(enc(kk) + '=' + enc(wsval(kk, params[k]))); }
      post('byond://winset?' + q.join('&'));
    },
    winget: function (id, props) {
      var list = props == null ? ['*'] : (Array.isArray(props) ? props : [props]);
      return new Promise(function (resolve) {
        var i = cbq.length;
        cbq.push(function (o) { var r = {}; for (var k in o || {}) r[k] = wgval(o[k]); resolve(r); });
        post('byond://winget?id=' + enc(id == null ? '' : id) + '&property=' + enc(list.map(kebab).join(',')) +
             '&callback=' + enc('__fugueByond[' + i + ']'));
      });
    },
    command: function (c) { BYOND.winset(null, { command: c }); },
    topic: function (params) {
      var q = [];
      for (var k in params || {}) q.push(enc(k) + '=' + enc(params[k] == null ? '' : params[k]));
      post('byond://?' + q.join('&'));
    }
  };
  window.BYOND = BYOND;
  try { document.dispatchEvent(new Event('byond-ready')); window.dispatchEvent(new Event('byond-ready')); } catch (e) {}
  if (window.top !== window) return;
  // READY: queued BEHIND the page's own ready handlers (jQuery runs them in registration order; this fires from
  // DOMContentLoaded, which is before jQuery's own listener, so jQuery(fn) lands after everything the page registered
  // while parsing). NOT the load event: load waits for every subresource, and on a cold join a chat page's fonts
  // and stylesheets are still arriving.
  function ready() { post('byond://__ready'); }
  document.addEventListener('DOMContentLoaded', function () { if (window.jQuery) window.jQuery(ready); else ready(); });
})();)JS")
        .arg(host, bridgeUrl);
}

void BrowserPane::attachOrigin(LocalOrigin* o, int paneId) {
    origin_ = o;
    paneId_ = paneId;
    page_->setOrigin(o);
    QWebEngineScript s;
    s.setName("fugue-bridge");
    s.setInjectionPoint(QWebEngineScript::DocumentCreation);
    s.setWorldId(QWebEngineScript::MainWorld);
    s.setRunsOnSubFrames(true);
    QString bridge = QString("/%1/__bridge?pane=%2").arg(o->secret()).arg(paneId);
    s.setSourceCode(bridgeScript(bridge, QString("127.0.0.1:%1").arg(o->port())));
    page_->scripts().insert(s);
}

void BrowserPane::openFile(const QString& file) {
    ready_ = false;
    load(QUrl(origin_->urlFor(file)));
}

void BrowserPane::callPage(const QString& fn, const QStringList& args) {
    QJsonArray a;
    for (const QString& s : args) a.append(s);
    QString lit = QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
    lit = lit.mid(1, lit.size() - 2);  // the array body is a valid argument list
    QString js =
        // A dotted name is a path from window, called on its owner: vg's output(.., "tooltipN:tooltip.init").
        QString(
            "(function(){var n=%1,f=window[n],t=window;if(typeof f!=='function'){f=window;"
            "n.split('.').forEach(function(k){t=f;f=f==null?f:f[k];});}if(typeof f==='function')f.call(t,%2);})()")
            .arg(QString::fromUtf8(QJsonDocument(QJsonArray{fn}).toJson(QJsonDocument::Compact)).mid(1).chopped(1),
                 lit);
    evalWhenReady(js);
}

void BrowserPane::evalWhenReady(const QString& js) {
    pending_ << js;
    if (ready_) flush();
}

void BrowserPane::flush() {
    QStringList q;
    q.swap(pending_);
    for (const QString& js : q) {
        page_->runJavaScript(js);
        callsRun_++;
    }
}

void BrowserPane::markReady() {
    if (ready_) return;
    ready_ = true;
    flush();
    emit becameReady();
}

}  // namespace fugue::shell
