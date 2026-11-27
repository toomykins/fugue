#include "shell/serverkey.h"

#include <QHostInfo>
#include <memory>

namespace fugue::shell {

namespace {
bool loopbackName(const QString& h) {
    return h.compare("localhost", Qt::CaseInsensitive) == 0 || QHostAddress(h).isLoopback();
}
}  // namespace

bool sameServerAddresses(const QList<QHostAddress>& a, const QList<QHostAddress>& b) {
    bool aLoop = false, bLoop = false;
    for (const QHostAddress& x : a) aLoop |= x.isLoopback();
    for (const QHostAddress& y : b) bLoop |= y.isLoopback();
    if (aLoop && bLoop) return true;
    for (const QHostAddress& x : a)
        for (const QHostAddress& y : b)
            if (x.isEqual(y, QHostAddress::TolerantConversion)) return true;
    return false;
}

void ServerKey::rekey(const QString& host, quint16 port) {
    host_ = host;
    port_ = port;
    emit changed(host_, port_);
}

void ServerKey::moved(const QString& host, quint16 port) {
    const quint64 seq = ++seq_;
    if (port != port_) {
        rekey(host, port);
        return;
    }
    if (host.compare(host_, Qt::CaseInsensitive) == 0 || (loopbackName(host) && loopbackName(host_))) {
        emit kept(host, port);
        return;
    }
    struct Pending {
        QList<QHostAddress> from, to;
        int left = 2;
    };
    auto p = std::make_shared<Pending>();
    auto done = [this, p, seq, host, port] {
        if (--p->left || seq != seq_) return;
        if (sameServerAddresses(p->from, p->to))
            emit kept(host, port);
        else
            rekey(host, port);
    };
    auto resolve = [this](const QString& name, auto ready) {
        QHostAddress address;
        if (address.setAddress(name)) {
            // Literal addresses need no reverse DNS. Keep completion queued so
            // a later move can still overtake it, just like a hostname lookup.
            QMetaObject::invokeMethod(
                this, [ready, address] { ready(QList<QHostAddress>{address}); }, Qt::QueuedConnection);
        } else {
            QHostInfo::lookupHost(name, this, [ready](const QHostInfo& i) { ready(i.addresses()); });
        }
    };
    resolve(host_, [p, done](const QList<QHostAddress>& addresses) {
        p->from = addresses;
        done();
    });
    resolve(host, [p, done](const QList<QHostAddress>& addresses) {
        p->to = addresses;
        done();
    });
}

}  // namespace fugue::shell
