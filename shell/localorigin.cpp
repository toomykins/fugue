#include "shell/localorigin.h"
#include "render/dmi.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QPointer>
#include <algorithm>
#include <cstdio>

namespace fugue::shell {

struct LocalOrigin::Conn {
    QPointer<QTcpSocket> sock;
    QByteArray buf;
    bool done = false;
    QElapsedTimer waitTimer;
    BrowseRscStore::Wait held = BrowseRscStore::Wait::No;   // why it is in held_ (No: not held)
    QString file;
    bool head = false;
};

LocalOrigin::LocalOrigin(std::shared_ptr<BrowseRscStore> store, QObject* parent) : QObject(parent), srv_(this), store_(std::move(store)) {
    for (int i = 0; i < 4; i++) secret_ += QString("%1").arg(QRandomGenerator::system()->generate(), 8, 16, QChar('0'));
    srv_.listen(QHostAddress::LocalHost, 0);
    connect(&srv_, &QTcpServer::newConnection, this, &LocalOrigin::onConn);
}

QString LocalOrigin::base() const { return QString("http://127.0.0.1:%1/%2/").arg(port()).arg(secret_); }
QString LocalOrigin::urlFor(const QString& file) const { return base() + QString::fromUtf8(QUrl::toPercentEncoding(file, "/")); }
bool LocalOrigin::owns(const QUrl& u) const {
    return (u.scheme() == "http") && u.host() == "127.0.0.1" && u.port() == port() && u.path().startsWith("/" + secret_ + "/");
}
QString LocalOrigin::fileOf(const QUrl& u) const { return u.path().mid(secret_.size() + 2); }

void LocalOrigin::setPage(const QString& file, const QString& html) { pages_[file] = html.toUtf8(); }
void LocalOrigin::removePage(const QString& file) { pages_.remove(file); }

QString LocalOrigin::mimeFor(const QString& name0) {
    QString n = name0.toLower();
    auto e = [&](const char* s) { return n.endsWith(s); };
    if (e(".js") || e(".mjs")) return "text/javascript; charset=utf-8";
    if (e(".css")) return "text/css; charset=utf-8";
    if (e(".html") || e(".htm")) return "text/html; charset=utf-8";
    if (e(".json")) return "application/json";
    if (e(".png")) return "image/png";
    if (e(".gif")) return "image/gif";
    if (e(".jpg") || e(".jpeg")) return "image/jpeg";
    if (e(".svg")) return "image/svg+xml";
    if (e(".ico")) return "image/x-icon";
    if (e(".woff2")) return "font/woff2";
    if (e(".woff")) return "font/woff";
    if (e(".ttf")) return "font/ttf";
    if (e(".eot")) return "application/vnd.ms-fontobject";
    if (e(".txt")) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

void LocalOrigin::onConn() {
    while (QTcpSocket* s = srv_.nextPendingConnection()) {
        auto c = std::make_shared<Conn>();
        c->sock = s;
        connect(s, &QTcpSocket::readyRead, this, [this, c] { c->buf += c->sock->readAll(); if (c->buf.size() > (2 << 20)) { c->sock->abort(); return; } process(c); });
        connect(s, &QTcpSocket::disconnected, this, [c] { c->done = true; if (c->sock) c->sock->deleteLater(); });
    }
}

void LocalOrigin::respond(const ConnP& c, int status, const QByteArray& type, const QByteArray& body, bool head) {
    if (c->done || !c->sock || c->sock->state() != QAbstractSocket::ConnectedState) return;
    static const QHash<int, const char*> reason = {{200, "OK"}, {204, "No Content"}, {400, "Bad Request"}, {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"}};
    QByteArray h = "HTTP/1.1 " + QByteArray::number(status) + " " + reason.value(status, "OK") + "\r\n";
    h += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
         "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
    c->sock->write(h);
    if (!head) c->sock->write(body);
    c->done = true;
    c->sock->disconnectFromHost();
}

void LocalOrigin::process(const ConnP& c) {
    if (c->done) return;
    int hdrEnd = c->buf.indexOf("\r\n\r\n");
    if (hdrEnd < 0) { if (c->buf.size() > 65536) { respond(c, 400, "text/plain", "bad"); } return; }
    QList<QByteArray> lines = c->buf.left(hdrEnd).split('\n');
    QList<QByteArray> rl = lines.value(0).trimmed().split(' ');
    if (rl.size() < 2) { respond(c, 400, "text/plain", "bad"); return; }
    QByteArray method = rl[0], target = rl[1];
    qint64 clen = 0;
    QByteArray host;
    for (int i = 1; i < lines.size(); i++) {
        QByteArray l = lines[i].trimmed();
        int col = l.indexOf(':');
        if (col < 0) continue;
        QByteArray k = l.left(col).toLower(), v = l.mid(col + 1).trimmed();
        if (k == "content-length") clen = v.toLongLong();
        else if (k == "host") host = v;
    }
    if (c->buf.size() < hdrEnd + 4 + clen) return;   // body not complete yet
    QByteArray body = c->buf.mid(hdrEnd + 4, int(clen));
    // DNS-rebinding guard: only a loopback Host is accepted.
    if (host != "127.0.0.1:" + QByteArray::number(port()) && host != "localhost:" + QByteArray::number(port())) { st_.refused++; respond(c, 403, "text/plain", "forbidden"); return; }
    QUrl u = QUrl::fromEncoded(target, QUrl::TolerantMode);
    QString path = u.path();
    if (!path.startsWith("/" + secret_ + "/")) { st_.refused++; respond(c, 404, "text/plain", "not found"); return; }
    QString file = path.mid(secret_.size() + 2);
    if (method == "POST" && file == "__bridge") {
        st_.bridge++;
        int pane = QUrl(target).query().section("pane=", 1).section('&', 0, 0).toInt();
        QString js = bridge_ ? bridge_(pane, body) : QString();
        respond(c, 200, "text/javascript; charset=utf-8", js.toUtf8());
        return;
    }
    if (method == "OPTIONS") { respond(c, 204, "text/plain", ""); return; }
    if (method != "GET" && method != "HEAD") { respond(c, 405, "text/plain", "method"); return; }
    bool head = method == "HEAD";
    if (pages_.contains(file)) {
        if (u.hasQuery()) {   // a `location='?...'` navigation reached the server: a Topic; 204 keeps the document
            st_.topics++;
            emit topic("?" + QString::fromLatin1(QUrl(target).query(QUrl::FullyEncoded).toLatin1()));
            respond(c, 204, "text/plain", "");
            return;
        }
        st_.pages++;
        respond(c, 200, "text/html; charset=utf-8", pages_.value(file), head);
        return;
    }
    c->waitTimer.start();
    serveFile(c, file, head);
}

QByteArray LocalOrigin::cellPng(const render::Icon& icon) {
    if (icon.states.empty()) return {};
    std::vector<uint8_t> px;
    if (!icon.frame(icon.states[0], 0, 0, px)) return {};
    QImage img(px.data(), icon.width, icon.height, icon.width * 4, QImage::Format_RGBA8888);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG")) return {};
    return out;
}

// browserImage: an icon a page references is served as its first cell (first state, dir, frame) as a plain PNG,
// not the raw .dmi sheet; stock DreamSeeker draws the sheet's top-left cell. Anything that is not a multi-cell
// .dmi comes back unchanged.
QByteArray LocalOrigin::browserImage(const QByteArray& b) {
    if (!b.startsWith("\x89PNG")) return b;
    auto r = render::parseIcon(reinterpret_cast<const uint8_t*>(b.constData()), size_t(b.size()));
    if (!r.ok() || r.icon->plainImage || r.icon->states.empty()) return b;
    int64_t cells = 0;
    for (const auto& s : r.icon->states) cells += s.dirs * s.frames;
    if (cells <= 1) return b;
    QByteArray out = cellPng(*r.icon);
    return out.isEmpty() ? b : out;
}

// A file that is not here yet is polled for, never waited on the GUI thread. A 404'd stylesheet or <img> is never
// retried, and on a remote connection the file can trail the announcement by a round trip, so a name the world
// announced waits up to kMissWaitMs; its 0x00fe jumps the declaration queue, so the wait ends when the bytes land.
// A name nobody announced waits only kUnnamedWaitMs, because on vg most names are unannounced. At most kMaxHeld
// guesses are held at once, since each held request occupies one of Chromium's connections to this origin. An
// announced name is never refused for the cap (admit): it evicts a guess, or is held over the cap.
void LocalOrigin::serveFile(const ConnP& c, const QString& file, bool head) {
    std::string bytes;
    if (store_ && store_->get(file.toStdString(), bytes)) {
        release(c);
        st_.rsc++;
        respond(c, 200, mimeFor(file).toUtf8(), browserImage(QByteArray::fromStdString(bytes)), head);
        return;
    }
    using Wait = BrowseRscStore::Wait;
    const Wait w = store_ ? store_->waitFor(file.toStdString()) : Wait::No;
    const int bound = w == Wait::Announced ? kMissWaitMs : w == Wait::Unnamed ? kUnnamedWaitMs : 0;
    // Timer callbacks can run late: bound the hold by monotonic elapsed time, not the number of polls.
    if (c->waitTimer.elapsed() < bound && (c->held != Wait::No || admit(c, w))) {
        if (w == Wait::Announced) c->held = w;   // announced while held: no longer counted against the unnamed cap
        c->file = file;
        c->head = head;
        QTimer::singleShot(20, this, [this, c, file, head] {
            if (c->done) { release(c); return; }
            serveFile(c, file, head);
        });
        return;
    }
    release(c);
    st_.notFound++;
    respond(c, 404, mimeFor(file).toUtf8(), "", head);
}

bool LocalOrigin::admit(const ConnP& c, BrowseRscStore::Wait w) {
    using Wait = BrowseRscStore::Wait;
    int unnamed = 0;
    for (const ConnP& h : held_) if (h->held == Wait::Unnamed) unnamed++;
    if (w == Wait::Unnamed && (int(held_.size()) >= kMaxHeld || unnamed >= kMaxHeldUnnamed)) { st_.heldRefused++; return false; }
    if (int(held_.size()) >= kMaxHeld) {
        // An announced name outranks a guess: the oldest unknown-name hold gives up its connection and is 404ed.
        // With no guess to evict, an announced name is still held over the cap, because a refused stylesheet or
        // <img> is never retried.
        auto it = std::find_if(held_.begin(), held_.end(), [](const ConnP& h) { return h->held == Wait::Unnamed; });
        if (it == held_.end()) {
            c->held = w;
            held_.push_back(c);
            st_.waited++;
            st_.heldPeak = std::max(st_.heldPeak, int(held_.size()));
            return true;
        }
        ConnP old = *it;
        held_.erase(it);
        old->held = Wait::No;
        st_.heldRefused++;
        st_.notFound++;
        respond(old, 404, mimeFor(old->file).toUtf8(), "", old->head);
    }
    c->held = w;
    held_.push_back(c);
    st_.waited++;
    st_.heldPeak = std::max(st_.heldPeak, int(held_.size()));
    return true;
}

void LocalOrigin::release(const ConnP& c) {
    if (c->held == BrowseRscStore::Wait::No) return;
    c->held = BrowseRscStore::Wait::No;
    held_.erase(std::remove(held_.begin(), held_.end(), c), held_.end());
}

}  // namespace fugue::shell
