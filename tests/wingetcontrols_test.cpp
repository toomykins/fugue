// Stock DreamSeeker 516.1688 answers from tests/data/winget-controls-world.
// Regression: tgstation's fit_viewport queries "mainwindow.split;mapwindow", "size".
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include "skin/skinfeed.h"

using namespace fugue::skin;

namespace {
QString fixture(const char* name) {
    return QStringLiteral(FUGUE_TEST_DATA_DIR "/winget-controls-world/") + QString::fromLatin1(name);
}
std::string skinText() {
    QFile f(fixture("skin.dmf"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll().toStdString();
}
}

class WingetControls : public QObject {
    Q_OBJECT
    bool dataReady_ = false;
private slots:
    void stockAnswers_data() {
        dataReady_ = false;
        QTest::addColumn<QString>("control");
        QTest::addColumn<QString>("params");
        QTest::addColumn<QString>("answer");
        QFile f(fixture("dreamseeker-answers.jsonl"));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()+": "+f.errorString()));
        QJsonArray rows;
        QSet<QPair<QString,QString>> queries;
        int lineNumber = 0;
        while (!f.atEnd()) {
            ++lineNumber;
            const QByteArray line = f.readLine().trimmed();
            const auto context = f.fileName()+':'+QString::number(lineNumber);
            QVERIFY2(f.error()==QFileDevice::NoError, qPrintable(context+": "+f.errorString()));
            if (line.isEmpty()) continue;
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(line,&error);
            QVERIFY2(doc.isObject(), qPrintable(context+": expected a JSON object: "+error.errorString()));
            const QJsonObject row = doc.object();
            // Empty strings are retained observations, including unknown controls
            // and properties; absent fields must not acquire the same meaning.
            for (const char* field : {"control","params","answer"})
                QVERIFY2(row[field].isString(), qPrintable(context+": missing string field "+field));
            const auto query = qMakePair(row["control"].toString(),row["params"].toString());
            QVERIFY2(!queries.contains(query), qPrintable(context+": duplicate retained query"));
            queries.insert(query);
            rows.append(row);
        }
        QCOMPARE(rows.size(),120);
        for (const auto& value : rows) {
            const auto row = value.toObject();
            const QString control = row.value("control").toString();
            const QString params = row.value("params").toString();
            const QByteArray name = (control + " / " + params).toUtf8();
            QTest::newRow(name.constData()) << control << params << row.value("answer").toString();
        }
        dataReady_ = true;
    }
    void stockAnswers() {
        // QtTest still enters the body after a failed _data() validation.
        QVERIFY2(dataReady_,"No validated stock winget answers");
        QFETCH(QString, control);
        QFETCH(QString, params);
        QFETCH(QString, answer);
        auto live = std::make_shared<LiveSkin>();
        const auto source = skinText();
        QVERIFY2(!source.empty(),qPrintable(fixture("skin.dmf")+": missing or empty skin"));
        QCOMPARE(live->load(source), std::string());
        live->setClient(640, 480);
        SkinFeed feed(live);
        WingetQuery q{control.toStdString(), params.toStdString(), 42};
        int replies = 0;
        feed.submitWinget(q, 0, [&](uint32_t id, const std::string& value) {
            ++replies;
            QCOMPARE(id, uint32_t(42));
            QCOMPARE(QString::fromStdString(value), answer);
        });
        QCOMPARE(replies, 1);
        QCOMPARE(feed.held(), size_t(0));
        // A present zero flag is the same named-property arm.
        q.hasFlag = true;
        q.flag = 0;
        QCOMPARE(QString::fromStdString(feed.answer(q)), answer);
    }
    void deferredUntilLayout() {
        const auto source = skinText();
        QVERIFY2(!source.empty(),qPrintable(fixture("skin.dmf")+": missing or empty skin"));
        auto live = std::make_shared<LiveSkin>();
        SkinFeed feed(live);
        const WingetQuery q{"alpha;beta", "size", 7};
        int replies = 0;
        auto reply = [&](uint32_t id, const std::string& value) {
            ++replies;
            QCOMPARE(id, uint32_t(7));
            QCOMPARE(value, std::string("alpha.size=100x40;beta.size=80x30"));
        };
        feed.submitWinget(q, 0, reply);
        QCOMPARE(feed.held(), size_t(1));
        QCOMPARE(replies, 0);
        QCOMPARE(live->load(source), std::string());
        feed.pump(1, reply);
        QCOMPARE(replies, 0);
        live->setClient(640, 480);
        feed.pump(2, reply);
        QCOMPARE(replies, 1);
        QCOMPARE(feed.held(), size_t(0));
        feed.pump(3, reply);
        QCOMPARE(replies, 1);
    }
};
FUGUE_TEST_GUILESS_MAIN(WingetControls)
#include "wingetcontrols_test.moc"
