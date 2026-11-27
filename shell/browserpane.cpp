#include "shell/browserpane.h"
#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QWebEngineFindTextResult>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
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

// Ctrl+F (and the platform's Find shortcut, Cmd+F on macOS). The key arrives at Chromium's focus-proxy child (or the find bar's field), not at the
// BrowserPane, so filter the focus widget, after the app-level client shortcut router. Only a pane whose world enabled `find` consumes it; otherwise
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
    if (auto* focused = QApplication::focusWidget()) focused->installEventFilter(f);
    QObject::connect(qApp, &QApplication::focusChanged, f, [](QWidget* before, QWidget* now) {
        if (before) before->removeEventFilter(f);
        if (now) now->installEventFilter(f);
    });
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
    // Incremental: an edit searches from the top again, once the typing pauses. A search for every keystroke sends
    // them back to back, and on a page that has just loaded the answer to the last one can be lost, leaving no count.
    findTimer_ = new QTimer(this);
    findTimer_->setSingleShot(true);
    findTimer_->setInterval(60);
    connect(findTimer_, &QTimer::timeout, this, [this] {
        const QString t = findEdit_->text();
        page_->findText(t);
        if (t.isEmpty()) findCount_->clear();
    });
    connect(findEdit_, &QLineEdit::textEdited, this, [this] { findTimer_->start(); });
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
    if (findTimer_) findTimer_->stop();
    page_->findText(QString());  // clears the highlight and the active match
    findCount_->clear();
    findBar_->hide();
    setFocus(Qt::OtherFocusReason);
}

void BrowserPane::findNext(bool backward) {
    if (!findEdit_ || findEdit_->text().isEmpty()) return;
    findTimer_->stop();  // this search is for the text as it is now
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
static QString bridgeScript(const QString& bridgeUrl, const QString& host, quint64 document) {
    return QString(R"JS((function () {
  'use strict';
  if (location.host !== '%1') return;                 // a foreign page in a subframe gets nothing
  if (window.__fugueBridge) return; window.__fugueBridge = 1;
  if (!location.pathname.startsWith('%4')) return;
  var BR = '%2', DOC = '%3';
  function scoped(url) { return String(url) + (String(url).includes('?') ? '&' : '?') + '__fugueDocument=' + DOC; }
  function post(url) {
    try {
      var x = new XMLHttpRequest();
      x.open('POST', BR, true);
      x.onload = function () { var t = x.responseText; if (t) { try { (0, eval)(t); } catch (e) {} } };
      x.send(scoped(url));
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
  // The native uppercase API is independent of tgui's mixed-case Byond helper.
  // Serialize requests so winset/hide and geometry queries retain page order.
  var tail = Promise.resolve(), serial = 0;
  window.__fugueCallbacks = Object.create(null);
  function request(url, answer) {
    return tail = tail.catch(function () {}).then(function () {
      return new Promise(function (resolve, reject) {
        var key = 'c' + (++serial), x = new XMLHttpRequest();
        if (answer) {
          window.__fugueCallbacks[key] = resolve;
          url += '&callback=__fugueCallbacks.' + key;
        }
        x.open('POST', BR, true); x.timeout = 10000;
        function clean() { delete window.__fugueCallbacks[key]; }
        x.onload = function () {
          try { if (answer) { if (!x.responseText) throw new Error('Stale browser document'); (0, eval)(x.responseText); } else resolve(); }
          catch (e) { reject(e); } finally { clean(); }
        };
        x.onerror = x.ontimeout = function () { clean(); reject(new Error('Browser bridge unavailable')); };
        x.send(scoped(url));
      });
    });
  }
  function property(k) { return String(k).replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); }); }
  // An object value is a size or position {x, y}, or a colour {value}.
  function wsval(k, v) {
    if (v == null) return '';
    if (typeof v === 'object') {
      if ('value' in v) return String(v.value);
      if ('x' in v && 'y' in v) return v.x + (/size$/.test(k) ? 'x' : ',') + v.y;
    }
    return String(v);
  }
  function pairs(o) {
    return Object.keys(o || {}).map(function (k) {
      var key = property(k);
      return encodeURIComponent(key) + '=' + encodeURIComponent(wsval(key, o[k]));
    }).join('&');
  }
  var queued = 0;
  function bounded(url, answer) {
    if (queued >= 256 || url.length > 65536) return Promise.reject(new Error('Browser bridge queue full'));
    queued++;
    return request(url, answer).finally(function () { queued--; });
  }
  window.BYOND = {
    winget: function (id, props) {
      return bounded('byond://winget?id=' + encodeURIComponent(id || '') + '&property=' +
        encodeURIComponent((Array.isArray(props) ? props : [props]).map(property).join(';')), true);
    },
    winset: function (id, props) { return bounded('byond://winset?id=' + encodeURIComponent(id || '') + '&' + pairs(props), false); },
    command: function (c) { return window.BYOND.winset(null, { command: c }); },
    topic: function (params) {
      return bounded('byond://?' + Object.keys(params || {}).map(function (k) {
        return encodeURIComponent(k) + '=' + encodeURIComponent(params[k] == null ? '' : params[k]);
      }).join('&'), false);
    }
  };
  // Synchronous, shared by trusted pages at this private world origin. This is
  // session caching, not hubStorage's asynchronous, persisted account store.
  window.domainStorage = window.localStorage;
  document.dispatchEvent(new Event('byond-ready')); window.dispatchEvent(new Event('byond-ready'));
  if (window.top !== window) return;
  // READY: queued BEHIND the page's own ready handlers (jQuery runs them in registration order; this fires from
  // DOMContentLoaded, which is before jQuery's own listener, so jQuery(fn) lands after everything the page registered
  // while parsing). NOT the load event: load waits for every subresource, and on a cold join a chat page's fonts
  // and stylesheets are still arriving.
  function ready() { post('byond://__ready'); }
  document.addEventListener('DOMContentLoaded', function () { if (window.jQuery) window.jQuery(ready); else ready(); });
})();)JS")
        .arg(host, bridgeUrl, QString::number(document), bridgeUrl.section("/__bridge", 0, 0) + '/');
}

void BrowserPane::attachOrigin(LocalOrigin* o, int paneId) {
    origin_ = o;
    paneId_ = paneId;
    page_->setOrigin(o);
    installBridge();
}
void BrowserPane::installBridge() {
    for (const auto& old : page_->scripts().find("fugue-bridge")) page_->scripts().remove(old);
    QWebEngineScript s;
    s.setName("fugue-bridge");
    s.setInjectionPoint(QWebEngineScript::DocumentCreation);
    s.setWorldId(QWebEngineScript::MainWorld);
    s.setRunsOnSubFrames(true);
    QString bridge = QString("/%1/__bridge?pane=%2").arg(origin_->secret()).arg(paneId_);
    s.setSourceCode(bridgeScript(bridge, QString("127.0.0.1:%1").arg(origin_->port()), document_));
    page_->scripts().insert(s);
}

void BrowserPane::openFile(const QString& file) {
    ready_ = false;
    // Calls queued for a previous document must not execute in its replacement.
    if (document_) {
        pending_.clear();
        pendingChars_ = 0;
    }
    ++document_;
    installBridge();
    load(QUrl(origin_->urlFor(file)));
}

void BrowserPane::callPage(const QString& fn, const QStringList& args) {
    static const QRegularExpression identifier("^[A-Za-z_$][A-Za-z0-9_$]*$");
    const auto parts = fn.split('.');
    if (fn.size() > 128 || parts.size() > 8) return;
    QJsonArray path, a;
    for (const auto& part : parts) {
        if (!identifier.match(part).hasMatch() || part == "__proto__" || part == "prototype" || part == "constructor")
            return;
        path.append(part);
    }
    for (const QString& s : args) a.append(s);
    auto json = [](const QJsonArray& v) { return QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact)); };
    evalWhenReady(
        QString("(function(){const p=%1;let o=window;for(let i=0;i<p.length-1;i++){if(o==null)return;o=o[p[i]];}"
                "if(o==null)return;const f=o[p[p.length-1]];if(typeof f==='function')f.apply(o,%2);})()")
            .arg(json(path), json(a)));
}

void BrowserPane::evalWhenReady(const QString& js) {
    if (js.size() > (1 << 20) || pending_.size() >= 1024 || pendingChars_ + js.size() > (4 << 20)) return;
    pendingChars_ += js.size();
    pending_ << js;
    if (ready_) flush();
}

void BrowserPane::flush() {
    QStringList q;
    q.swap(pending_);
    pendingChars_ = 0;
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
