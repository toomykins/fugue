#include <QtTest>
#include "tests/testmain.h"
#include "platform/socket.h"

class PlatformTest : public QObject {
    Q_OBJECT
private slots:
    void socketFlags() {
        auto fd = fugue::socket::create(AF_INET, SOCK_STREAM, 0);
        QVERIFY(fd >= 0);
        QCOMPARE(fugue::socket::nonblocking(fd, true), 0);
        int descriptorFlags = ::fcntl(fd, F_GETFD);
        int statusFlags = ::fcntl(fd, F_GETFL);
        ::close(fd);
        QVERIFY(descriptorFlags & FD_CLOEXEC);
        QVERIFY(statusFlags & O_NONBLOCK);
    }
    void pipeFlagsAndIO() {
        int fds[2];
        QCOMPARE(fugue::platform::pipeCloseOnExec(fds), 0);
        int readFlags = ::fcntl(fds[0], F_GETFD);
        int writeFlags = ::fcntl(fds[1], F_GETFD);
        ssize_t sent = ::write(fds[1], "x", 1);
        char b = 0;
        ssize_t received = ::read(fds[0], &b, 1);
        ::close(fds[0]);
        ::close(fds[1]);
        QVERIFY(readFlags & FD_CLOEXEC);
        QVERIFY(writeFlags & FD_CLOEXEC);
        QCOMPARE(sent, ssize_t(1));
        QCOMPARE(received, ssize_t(1));
        QCOMPARE(b, 'x');
    }
    void closedSocketDoesNotKillProcess() {
        int fds[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        QCOMPARE(fugue::socket::send(fds[0], "x", 1), ssize_t(1));
        char b;
        QCOMPARE(::read(fds[1], &b, 1), ssize_t(1));
        ::close(fds[1]);
        ssize_t sent = fugue::socket::send(fds[0], "x", 1);
        int error = errno;
        ::close(fds[0]);
        QCOMPARE(sent, ssize_t(-1));
        QCOMPARE(error, EPIPE);
    }
};
FUGUE_TEST_GUILESS_MAIN(PlatformTest)
#include "platform_test.moc"
