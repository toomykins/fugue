// The server a window's per-server state (the player's own macro list, skin/usermacros.h) is filed under. A link() that
// reaches the same server keeps it: the same port and the same host name, both loopback, or a common resolved address.
// vg links every client to byond://[world.address]:[world.port], an IP, at the end of each round, whatever name the
// player joined by. Names are resolved by QHostInfo, off the session thread; literal IPs skip DNS with queued completion.
#pragma once
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

namespace fugue::shell {

// Two resolved address lists name the same server: they share an address, or both are loopback.
bool sameServerAddresses(const QList<QHostAddress>& a, const QList<QHostAddress>& b);

class ServerKey : public QObject {
    Q_OBJECT
public:
    ServerKey(QString host, quint16 port, QObject* parent = nullptr)
        : QObject(parent), host_(std::move(host)), port_(port) {}
    QString host() const { return host_; }
    quint16 port() const { return port_; }
    // The window's session now talks to host:port. A different server changes the key (changed), the same one keeps it
    // (kept), at once or once both names have resolved. A later move overtakes a lookup still in flight.
    void moved(const QString& host, quint16 port);

signals:
    void changed(const QString& host, quint16 port);
    void kept(const QString& host, quint16 port);  // host:port is the keyed server

private:
    void rekey(const QString& host, quint16 port);
    QString host_;
    quint16 port_;
    quint64 seq_ = 0;
};

}  // namespace fugue::shell
