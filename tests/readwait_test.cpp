#include "platform/readwait.h"
#include <QtTest>
#include "tests/testmain.h"
#include <thread>
using namespace fugue;
using namespace std::chrono_literals;
namespace {
struct Pair {
    socket::Handle reader = -1, writer = -1;
    Pair() {
        auto listener = socket::create(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(listener, 1) != 0) {
            socket::close(listener); return;
        }
        socket::Length length = sizeof address;
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length);
        writer = socket::create(AF_INET, SOCK_STREAM, 0);
        if (socket::connect(writer, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0)
            reader = ::accept(listener, nullptr, nullptr);
        socket::close(listener);
    }
    ~Pair() { if (reader >= 0) socket::close(reader); if (writer >= 0) socket::close(writer); }
};
}
class ReadWaitTest : public QObject {
    Q_OBJECT
private slots:
    void timeoutReadAndClose() {
        Pair pair;
        QVERIFY(pair.reader >= 0);
        socket::ReadWait wait;
        QCOMPARE(wait.wait(pair.reader, 0us), 0);
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i) QCOMPARE(wait.wait(pair.reader, 5ms), 0);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        QVERIFY(ms >= 90); // tolerate clock jitter, but never a spin/immediate-return timer
        qInfo("5 ms read deadline: average %.3f ms", ms / 20);
        char value = 0;
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(socket::send(pair.writer, "x", 1), 1);
            QCOMPARE(wait.wait(pair.reader, 100ms), 1);
            QCOMPARE(socket::recv(pair.reader, &value, 1, 0), 1);
            QCOMPARE(value, 'x');
            QCOMPARE(wait.wait(pair.reader, 1ms), 0); // no stale timer/event readiness
        }
        socket::close(pair.writer); pair.writer = -1;
        QCOMPARE(wait.wait(pair.reader, 100ms), 1);
        QCOMPARE(socket::recv(pair.reader, &value, 1, 0), 0);
    }
    void socketInterruptsDeadlineAndReset() {
        Pair pair, replacement;
        QVERIFY(pair.reader >= 0 && replacement.reader >= 0);
        socket::ReadWait wait;
        QCOMPARE(wait.wait(pair.reader, 1ms), 0);
        std::thread sender([&] { std::this_thread::sleep_for(5ms); socket::send(pair.writer, "a", 1); });
        int ready = wait.wait(pair.reader, 100ms);
        sender.join();
        QCOMPARE(ready, 1);
        char value = 0;
        QCOMPARE(socket::recv(pair.reader, &value, 1, 0), 1);
        wait.reset();
        // Reset restores blocking mode for relogin/other session operations.
        std::thread later([&] { std::this_thread::sleep_for(5ms); socket::send(pair.writer, "b", 1); });
        auto count = socket::recv(pair.reader, &value, 1, 0);
        later.join();
        QCOMPARE(count, 1);
        QCOMPARE(value, 'b');
        QCOMPARE(socket::send(replacement.writer, "c", 1), 1);
        QCOMPARE(wait.wait(replacement.reader, 100ms), 1);
        QCOMPARE(socket::recv(replacement.reader, &value, 1, 0), 1);
        QCOMPARE(value, 'c');
    }
};
FUGUE_TEST_GUILESS_MAIN(ReadWaitTest)
#include "readwait_test.moc"
