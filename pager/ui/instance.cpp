#include "pager/ui/instance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include "account/store.h"

namespace fugue::pager {

QString instanceName() {
    const QByteArray home = QByteArray::fromStdString(account::fugueDir());
    const QString name =
        "fugue-pager-" +
        QString::fromLatin1(QCryptographicHash::hash(home, QCryptographicHash::Sha256).toHex().left(16));
#ifdef Q_OS_LINUX
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString app = qEnvironmentVariable("FLATPAK_ID");
    if (!app.isEmpty() && !dir.isEmpty()) dir += "/app/" + app;
    if (!dir.isEmpty() && QDir(dir).exists()) return dir + "/" + name;
#endif
    return name;
}

bool forwardToRunning(const QString& name, const QString& link, int timeoutMs) {
    QLocalSocket s;
    s.connectToServer(name);
    if (!s.waitForConnected(timeoutMs)) return false;
    s.write((link.isEmpty() ? QByteArray("show") : "open " + link.toUtf8()) + "\n");
    if (!s.waitForBytesWritten(timeoutMs)) return false;
    while (!s.canReadLine())
        if (!s.waitForReadyRead(timeoutMs)) return false;
    return s.readLine() == "ok\n";
}

InstanceServer::InstanceServer(std::function<void(const QString&)> onRequest, QObject* parent)
    : QObject(parent), server_(new QLocalServer(this)), onRequest_(std::move(onRequest)) {
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* c = server_->nextPendingConnection()) {
            connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            connect(c, &QLocalSocket::readyRead, this, [this, c] {
                if (!c->canReadLine()) return;
                const QString line = QString::fromUtf8(c->readLine()).trimmed();
                if (line != "show" && !line.startsWith("open ")) {
                    c->disconnectFromServer();
                    return;
                }
                c->write("ok\n");
                c->flush();
                const QString link = line == "show" ? QString() : line.mid(5);
                QTimer::singleShot(0, this, [this, link] { onRequest_(link); });
            });
        }
    });
}

bool InstanceServer::listen(const QString& name) {
    QLocalSocket probe;  // listen() would take the name over even from a live pager
    probe.connectToServer(name);
    if (probe.waitForConnected(1000)) return false;
    QLocalServer::removeServer(name);
    return server_->listen(name);
}

}  // namespace fugue::pager
