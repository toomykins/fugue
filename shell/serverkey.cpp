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
    QHostInfo::lookupHost(host_, this, [p, done](const QHostInfo& i) {
        p->from = i.addresses();
        done();
    });
    QHostInfo::lookupHost(host, this, [p, done](const QHostInfo& i) {
        p->to = i.addresses();
        done();
    });
}

}  // namespace fugue::shell
