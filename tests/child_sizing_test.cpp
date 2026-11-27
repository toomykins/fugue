// Independent stock CHILD geometry: tests/data/child-sizing-world/README.md.
#include <QtTest>
#include "tests/testmain.h"
#include "tests/recordedinput.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "skin/liveskin.h"

using namespace fugue::skin;

static bool fixture(QByteArray& source, QString& error) {
    source.clear();
    error.clear();
    QFile file(QStringLiteral(FUGUE_TEST_DATA_DIR "/child-sizing-world/skin.dmf"));
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.fileName() + ": " + file.errorString();
        return false;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = file.fileName() + ": " + file.errorString();
        return false;
    }
    if (bytes.isEmpty()) {
        error = file.fileName() + ": Empty skin fixture";
        return false;
    }
    source = bytes;
    return true;
}

static bool geometry(const QJsonValue& value) {
    static const QRegularExpression size("^([1-9][0-9]*)x([1-9][0-9]*)\\z");
    const auto match = size.match(value.toString());
    if (!match.hasMatch()) return false;
    bool width = false, height = false;
    const int w = match.captured(1).toInt(&width), h = match.captured(2).toInt(&height);
    return width && height && w > 0 && h > 0;
}

static void phase(LiveSkin& live, const QString& name) {
    if (name == "reset" || name == "clear") {
        live.winset("main.leftonly", "left=first;right=\"\"");
        live.winset("main.rightonly", "left=\"\";right=second");
    } else if (name == "both") {
        live.winset("main.leftonly", "right=extra_right");
        live.winset("main.rightonly", "left=extra_left");
    } else if (name == "swap") {
        live.winset("main.leftonly", "left=\"\";right=first");
        live.winset("main.rightonly", "right=\"\";left=second");
    } else if (name == "fraction-winset") {
        live.winset("main.fraction", "splitter=97.5");
    }
}

class ChildSizingTest : public QObject {
    Q_OBJECT
    bool dataReady_ = false;
private slots:
    void stockGeometry_data() {
        dataReady_ = false;
        QTest::addColumn<QJsonArray>("history");
        const QString path = QStringLiteral(FUGUE_TEST_DATA_DIR "/child-sizing-world/stock.json");
        QJsonDocument document;
        QString error;
        QVERIFY2(fugue::testinput::readRecordedJson(path, document, error), qPrintable(error));
        const auto rows = document.object().value("rows").toArray();
        const QStringList phases{"startup-ready", "reset", "both", "clear", "swap", "fraction-winset", "resize"};
        const QStringList controls{"chat.browser", "first.content", "lower.content", "second.content", "upper.content"};
        const QRegularExpression percentage("^[0-9]+\\.[0-9]{6}\\z");
        QCOMPARE(rows.size(), phases.size());
        for (qsizetype index = 0; index < rows.size(); ++index) {
            const auto row = rows[index].toObject();
            const auto context = path + ": " + phases[index];
            QVERIFY2(row.value("phase").toString() == phases[index], qPrintable(context + ": missing or out-of-order phase"));
            QVERIFY2(geometry(row.value("client")), qPrintable(context + ": invalid client geometry"));
            const auto sizes = row.value("controls").toObject();
            QVERIFY2(sizes.keys() == controls, qPrintable(context + ": expected all five named controls"));
            for (auto it = sizes.begin(); it != sizes.end(); ++it)
                QVERIFY2(geometry(it.value()), qPrintable(context + ": invalid geometry for " + it.key()));
            const auto fraction = row.value("fraction").toObject();
            QVERIFY2(fraction.keys() == (QStringList{"is-vert", "splitter"}), qPrintable(context + ": expected splitter and is-vert observations"));
            const auto splitter = fraction.value("splitter").toString();
            bool valid = false;
            const auto number = splitter.toDouble(&valid);
            QVERIFY2(percentage.match(splitter).hasMatch() && valid && number >= 0 && number <= 100,
                     qPrintable(context + ": invalid splitter percentage"));
            const auto vertical = fraction.value("is-vert").toString();
            QVERIFY2(vertical == "true" || vertical == "false", qPrintable(context + ": invalid is-vert observation"));
        }
        QJsonArray history;
        for (const auto& value : rows) {
            history.append(value);
            QTest::newRow(value.toObject().value("phase").toString().toUtf8().constData()) << history;
        }
        dataReady_ = true;
    }
    void stockGeometry() {
        // QtTest can still enter the body after a failing _data() function.
        QVERIFY2(dataReady_, "No validated child-sizing observations");
        QFETCH(QJsonArray, history);
        LiveSkin live;
        QByteArray source;
        QString error;
        QVERIFY2(fixture(source, error), qPrintable(error));
        const auto loadError = live.load(source.toStdString());
        QVERIFY2(loadError.empty(), loadError.c_str());
        for (const auto& value : history) {
            const auto row = value.toObject();
            const auto extent = row.value("client").toString().split('x');
            QCOMPARE(extent.size(), 2);
            live.setClient(extent[0].toInt(), extent[1].toInt());
            phase(live, row.value("phase").toString());
        }
        const auto row = history.last().toObject();
        const auto controls = row.value("controls").toObject();
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            std::string size;
            QVERIFY(live.winget(it.key().toStdString(), "size", size));
            QCOMPARE(QString::fromStdString(size), it.value().toString());
        }
        const auto fraction = row.value("fraction").toObject();
        for (auto it = fraction.begin(); it != fraction.end(); ++it) {
            std::string value;
            QVERIFY(live.winget("main.fraction", it.key().toStdString(), value));
            QCOMPARE(QString::fromStdString(value), it.value().toString());
        }
        // Empty slots must not leave an invisible or draggable divider.
        const auto snapshot = live.snapshot();
        QCOMPARE(snapshot.tree.splitters.size(), row.value("phase") == "both" ? size_t(2) : size_t(0));
    }
    void inferredPreviewVersusLiveEmptySide() {
        QByteArray source;
        QString error;
        QVERIFY2(fixture(source, error), qPrintable(error));
        source.replace("\"leftonly\"", "\"primary\""); // no suffix overlap with wrapper.only
        // Offline preview may infer a pane from its own on-show; LiveSkin must not.
        source.replace("window \"extra_right\"\n    elem \"extra_right\"\n        type = MAIN",
                       "window \"extra_right\"\n    elem \"extra_right\"\n        type = MAIN\n        on-show = \".winset primary.right=extra_right\"");
        const auto parsed = parseDmf(source.toStdString());
        QVERIFY2(parsed.ok, qPrintable(QString("skin.dmf:%1: %2").arg(parsed.errLine).arg(QString::fromStdString(parsed.error))));
        auto preview = layoutSkin(parsed.file, {0, 0, 640, 480});
        QVERIFY(preview.byName("extra_right.content"));
        QVERIFY(preview.byName("first.content"));
        QCOMPARE(preview.byName("first.content")->rect.dx(), 74);
        auto live = layoutSkin(parsed.file, {0, 0, 640, 480}, {}, nullptr, false);
        QVERIFY(!live.byName("extra_right.content"));
        QVERIFY(live.byName("first.content"));
        QCOMPARE(live.byName("first.content")->rect.dx(), 300);
        // The offline preview also fills a genuinely unclaimed singleton.
        QVERIFY(preview.byName("chat.browser"));
        QCOMPARE(preview.byName("chat.browser")->rect, (Rect{0, 160, 300, 310}));
    }
};
FUGUE_TEST_GUILESS_MAIN(ChildSizingTest)
#include "child_sizing_test.moc"
