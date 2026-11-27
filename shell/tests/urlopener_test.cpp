#include <QApplication>
#include <QtTest>
#include "tests/testmain.h"
#include <memory>
#include "shell/urlopener.h"

class UrlOpenerTest : public QObject {
    Q_OBJECT
private slots:
    void nativeLinks() {
        QList<QUrl> received;
        fugue::shell::UrlOpener opener(this, [&](const QUrl& url) { received.push_back(url); });
        const QUrl first("byond://localhost:1234"), second("byond://example.org:5678");
        QFileOpenEvent one(first), two(second), other(QUrl("https://example.org"));
        QCoreApplication::sendEvent(qApp, &one);
        QCoreApplication::sendEvent(qApp, &two);
        QCoreApplication::sendEvent(qApp, &other);
        QVERIFY(received.isEmpty());  // delivery deferred until startup has finished
        QTRY_COMPARE(received.size(), 2);
        QCOMPARE(received, QList<QUrl>({first, second}));
    }
    void ownerLifetime() {
        bool called = false;
        auto owner = std::make_unique<QObject>();
        new fugue::shell::UrlOpener(owner.get(), [&](const QUrl&) { called = true; });
        QFileOpenEvent event(QUrl("byond://localhost:1234"));
        QCoreApplication::sendEvent(qApp, &event);
        owner.reset();
        QCoreApplication::processEvents();
        QVERIFY(!called);
    }
};
FUGUE_TEST_MAIN(UrlOpenerTest)
#include "urlopener_test.moc"
