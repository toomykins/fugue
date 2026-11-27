// BrowserPane: a QWebEngineView for a skin BROWSER control or a browse() window.
//
// Navigation policy (acceptNavigationRequest), where a page talks to the client:
//   * byond://...        a Topic, winset, winget or command. Registered before QApplication; see registerSchemes().
//                        Cancelled and emitted.
//   * origin URL + ?query  a main-frame navigation to a published page with a query is a Topic: cancelled and
//                        emitted, and the page stays where it is.
//   * other http(s)/mailto  a clicked link opens in the desktop browser; any other main-frame navigation is refused.
//   * subframes          allowed (byond:// excepted).
// The page gets `window.cef_to_byond` and `window.hubStorage` from an injected user script; the User-Agent is untouched.
//
// Server-to-page calls (output(msg, "win.ctl:fn")) go through callPage(): queued until the page reports READY (its
// DOM-ready handlers have run), then flushed in order.
//
// Ctrl+F (find bar) works only when the world enables it: browser-options `find` (bit 2). Stock DreamSeeker keeps
// the WebView2 accelerators off by default and re-enables them per browser-options bit, so Ctrl+F reaches the page
// unless the world set that bit. findAllowed() gates an app-level Ctrl+F filter; when allowed, the key opens this
// pane's find bar and is consumed, so neither the page nor a world macro sees it. A macro never fires from a
// browser pane (shell/focusfilter.h), and the bar is a `fugueZone=other` widget for the same reason.
#pragma once
#include <QStringList>
#include <functional>
#include <QWebEnginePage>
#include <QWebEngineView>

class QWebEngineProfile;
class QLabel;
class QTimer;
class QLineEdit;
class QWebEngineFindTextResult;

namespace fugue::shell {

class LocalOrigin;

// MUST be called before the QApplication is constructed (otherwise Chromium drops byond:// silently). It is NOT
// sufficient from an http/data page: every BrowserPane also installs a stub handler for the scheme on its profile
// (see ensureByondHandler in browserpane.cpp) or the navigation is blocked before acceptNavigationRequest runs.
void registerSchemes();

class BrowserPage : public QWebEnginePage {
    Q_OBJECT
public:
    using QWebEnginePage::QWebEnginePage;
    void setOrigin(LocalOrigin* o) { origin_ = o; }
signals:
    void byondLink(const QUrl& url);       // a byond:// navigation was intercepted
    void topicQuery(const QString& href);  // an origin-page navigation with a query: "?..." Topic
    void documentChanging();               // a navigation to one of our documents was accepted: the old READY is void
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override;
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& msg, int line,
                                  const QString& src) override;

private:
    LocalOrigin* origin_ = nullptr;
};

class BrowserPane : public QWebEngineView {
    Q_OBJECT
public:
    // profile == nullptr: the default profile. The host passes its own no-cache profile so a re-minted asset is never served stale.
    explicit BrowserPane(QWidget* parent = nullptr, QWebEngineProfile* profile = nullptr);
    void showHtml(const QString& html);
    BrowserPage* bpage() const { return page_; }

    // Wire this pane to a LocalOrigin: its bridge id and the injected cef_to_byond / hubStorage / READY script.
    void attachOrigin(LocalOrigin* o, int paneId);
    int paneId() const { return paneId_; }
    void openFile(const QString& file);  // load origin->urlFor(file); READY is cleared
    // Call window.fn(args...) in the page (JSON-quoted string arguments); queued until READY.
    void callPage(const QString& fn, const QStringList& args);
    void evalWhenReady(const QString& js);
    void markReady();
    bool ready() const { return ready_; }
    quint64 document() const { return document_; }
    int callsRun() const { return callsRun_; }
    QString name;  // the browse window / control name this pane shows

    // Ctrl+F (see the file comment). findAllowed unset means the key always belongs to the page.
    std::function<bool()> findAllowed;
    void openFind();   // show the bar, focus and select its text
    void closeFind();  // hide it, clear the highlight, give the page its focus back
    void findNext(bool backward);
    bool findOpen() const;
    QLineEdit* findField() const { return findEdit_; }
    QString findStatus() const;  // "2/5", "No results", or "" (nothing searched)

signals:
    void byondLink(const QUrl& url);
    void topicQuery(const QString& href);
    void becameReady();

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void installBridge();
    quint64 document_ = 0;
    void flush();
    void ensureFindBar();
    void placeFindBar();
    void findResult(const QWebEngineFindTextResult& r);
    QWidget* findBar_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLabel* findCount_ = nullptr;
    QTimer* findTimer_ = nullptr;  // the pause after typing before the page is searched
    BrowserPage* page_;
    LocalOrigin* origin_ = nullptr;
    int paneId_ = 0, callsRun_ = 0;
    bool ready_ = false;
    QStringList pending_;
    qsizetype pendingChars_ = 0;
};

}  // namespace fugue::shell
