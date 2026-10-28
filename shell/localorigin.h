// LocalOrigin: the loopback HTTP origin every browse() page is served from. Custom-scheme fetch() is unsupported in
// Qt 6.4 and a scheme handler cannot set an HTTP status, so pages need a real origin for <script src>, fetch() and 404s.
//
//   http://127.0.0.1:<random port>/<per-session secret>/<file>
//
// The secret path prefix keeps other local processes, and browser tabs using a rebinding trick, from reading world
// assets or posting Topics: anything outside the prefix is a 404, and the Host header must be loopback.
//
//   GET  <file>            a published browse page (text/html), else a browse_rsc file from the BrowseRscStore.
//                          A page request with a QUERY is a Topic: answered 204 so the document is kept, and
//                          handed to onTopic.
//   GET  <missing file>    404 with the MIME type of the name asked for: a text/html 404 body makes a stylesheet
//                          request render as a white panel. A name the world announced (0x00fe) is held open up to
//                          kMissWaitMs; an unknown name is held up to kUnnamedWaitMs, and only while an unnamed record
//                          is outstanding. At most kMaxHeld requests are held at once, but an announced name is never
//                          refused for the cap (it evicts an unknown-name hold, or is held over the cap).
//                          Chromium opens 6 connections per host, and __bridge needs one.
//   POST __bridge?pane=N   a page's `cef_to_byond(url)`: the body is the byond:// URL, the reply is JavaScript for the
//                          page to evaluate (a winget answer is `try{(cb)({...})}catch(e){}`), empty for nothing.
//
// GUI-thread object (QTcpServer lives where it was created).
#pragma once
#include "render/dmi.h"
#include <QHash>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <functional>
#include <memory>
#include <vector>
#include "shell/browsestore.h"

namespace fugue::shell {

class LocalOrigin : public QObject {
    Q_OBJECT
public:
    explicit LocalOrigin(std::shared_ptr<BrowseRscStore> store, QObject* parent = nullptr);
    bool listening() const { return srv_.isListening(); }
    quint16 port() const { return srv_.serverPort(); }
    QString secret() const { return secret_; }
    // "http://127.0.0.1:PORT/SECRET/" - every URL the shell builds starts with it.
    QString base() const;
    QString urlFor(const QString& file) const;
    bool owns(const QUrl& u) const;                  // under base()?
    QString fileOf(const QUrl& u) const;             // the path after base(), decoded

    void setPage(const QString& file, const QString& html);
    void removePage(const QString& file);
    bool hasPage(const QString& file) const { return pages_.contains(file); }

    // A page's bridge call -> the JS to reply with. GUI thread.
    using BridgeFn = std::function<QString(int pane, const QByteArray& body)>;
    void setBridge(BridgeFn f) { bridge_ = std::move(f); }

    // How long a request is held before it 404s (localorigin.cpp serveFile): a browse_rsc name the world announced
    // whose bytes have not arrived, or a name nobody announced while an unnamed record is outstanding.
    static constexpr int kMissWaitMs = 20000;
    static constexpr int kUnnamedWaitMs = 2000;
    // Requests held at once, and of those, requests for unknown names. Chromium opens at most 6 connections per host,
    // so at most 4 are held and two stay free for files that are present and for __bridge POSTs.
    static constexpr int kMaxHeld = 4;
    static constexpr int kMaxHeldUnnamed = 2;
    static QString mimeFor(const QString& name);
    // A browse_rsc'd icon as a page sees it: its first cell as a plain PNG (see localorigin.cpp).
    static QByteArray browserImage(const QByteArray& bytes);
    // The icon's first cell (first state, dir, frame) as a PNG; empty when it has none.
    static QByteArray cellPng(const render::Icon& icon);
    // waited: requests held open. heldRefused: 404ed at once because kMaxHeld or kMaxHeldUnnamed was reached, or
    // evicted for an announced name. heldPeak: most held at once.
    struct Stats { int pages = 0, rsc = 0, notFound = 0, topics = 0, bridge = 0, refused = 0, waited = 0, heldRefused = 0, heldPeak = 0; };
    int held() const { return int(held_.size()); }
    const Stats& stats() const { return st_; }
signals:
    void topic(const QString& href);                 // "?_src_=..." (leading '?')
private:
    struct Conn;
    using ConnP = std::shared_ptr<Conn>;
    void onConn();
    void process(const ConnP& c);
    void respond(const ConnP& c, int status, const QByteArray& type, const QByteArray& body, bool head = false);
    void serveFile(const ConnP& c, const QString& file, bool head);
    bool admit(const ConnP& c, BrowseRscStore::Wait w);
    void release(const ConnP& c);

    QTcpServer srv_;
    QString secret_;
    QHash<QString, QByteArray> pages_;
    std::shared_ptr<BrowseRscStore> store_;
    BridgeFn bridge_;
    Stats st_;
    std::vector<ConnP> held_;   // requests being held open, oldest first
};

}  // namespace fugue::shell
