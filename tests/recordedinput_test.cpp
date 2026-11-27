#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>
#include <limits>
#include "tests/recordedinput.h"
#include "tests/testmain.h"

using namespace fugue;
using namespace fugue::testinput;

class RecordedInputTest : public QObject {
    Q_OBJECT
private slots:
    void observationTuplesRequireNumbersAndBounds() {
        QVERIFY(isNumberTuple(QJsonArray{-0.5,0,1.25},3));
        QVERIFY(!isNumberTuple(QJsonObject{},3));
        QVERIFY(!isNumberTuple(QJsonArray{0,1},3));
        QVERIFY(!isNumberTuple(QJsonArray{0,1,2,3},3));
        QVERIFY(!isNumberTuple(QJsonArray{0,"1",2},3));
        QVERIFY(!isNumberTuple(QJsonArray{0,true,2},3));
        QVERIFY(!isNumberTuple(QJsonArray{0,QJsonValue::Null,2},3));
        QVERIFY(isIntegerTuple(QJsonArray{0,128,255},3,0,255));
        QVERIFY(isIntegerTuple(QJsonArray{-32,32},2,-32,32));
        QVERIFY(!isIntegerTuple(QJsonObject{},2,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,128},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,128,255,1},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,"128",255},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,true,255},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,QJsonValue::Null,255},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,128.5,255},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{-1,128,255},3,0,255));
        QVERIFY(!isIntegerTuple(QJsonArray{0,128,256},3,0,255));
    }
    void jsonPreservesObjectsAndArrays_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<bool>("array");
        QTest::newRow("object") << QByteArray(R"({"phase":"ready","samples":[[1,2,3]],"empty":[]})") << false;
        QTest::newRow("array") << QByteArray(R"([{"phase":"ready","samples":[[1,2,3]],"empty":[]}])") << true;
        QTest::newRow("empty-object") << QByteArray("{}") << false;
        QTest::newRow("empty-array") << QByteArray("[]") << true;
    }
    void jsonPreservesObjectsAndArrays() {
        QFETCH(QByteArray, contents);
        QFETCH(bool, array);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("observations.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        QJsonDocument document;
        QString error = "previous error";
        QVERIFY2(readRecordedJson(path, document, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(document.isArray(), array);
        if (contents.size() == 2) {
            // Empty JSON is syntactically valid; the scenario must require its observations.
            QVERIFY(array ? document.array().isEmpty() : document.object().isEmpty());
        } else {
            const auto object = array ? document.array().at(0).toObject() : document.object();
            QCOMPARE(object["phase"].toString(), QString("ready"));
            const auto samples = object["samples"].toArray();
            QCOMPARE(samples.size(), 1);
            QCOMPARE(samples.at(0).toArray(), QJsonArray({1,2,3}));
            QVERIFY(object["empty"].isArray());
            QVERIFY(object["empty"].toArray().isEmpty());
        }
    }
    void invalidJsonDiscardsPreviousObservations_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("truncated-object") << QByteArray(R"({"phases":[)");
        QTest::newRow("truncated-array") << QByteArray(R"([{"phase":"ready"})");
        QTest::newRow("trailing-garbage") << QByteArray("{}oops");
    }
    void invalidJsonDiscardsPreviousObservations() {
        QFETCH(QByteArray, contents);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("observations.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        QJsonDocument document(QJsonObject{{"phase", "old"}});
        QString error;
        QVERIFY(!readRecordedJson(path, document, error));
        QVERIFY(document.isNull());
        QVERIFY2(error.startsWith(path + ": Invalid JSON at byte "), qPrintable(error));
    }
    void missingJsonIsNotAnEmptyExpectation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("missing.json");
        QJsonDocument document(QJsonObject{{"phase", "old"}});
        QString error;
        QVERIFY(!readRecordedJson(path, document, error));
        QVERIFY(document.isNull());
        QVERIFY2(error.startsWith(path + ": Cannot read JSON:"), qPrintable(error));
    }
    void retainsOrderedFrames_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("body-length") << QByteArray("0x0027 len=2 aBcd\n0x0018 len=0\n0x0034 len=1 09\n");
        QTest::newRow("body-plus-one") << QByteArray("0x0027 len=3 aBcd\n0x0018 len=1 \n0x0034 len=2 09\n");
        QTest::newRow("headers-crlf-whitespace")
            << QByteArray("=== retained recording ===\r\n\t# independent input\r\n \r\n"
                          " 0x0027\tlen=3\tABcd \r\n0x0018 len=1\t\r\n 0x0034 len=2 09 ");
    }
    void retainsOrderedFrames() {
        QFETCH(QByteArray, contents);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QString::fromUtf8("retained-\xc3\xa8.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error = "previous error";
        QVERIFY2(readRecordedFrames(path, frames, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(frames.size(), size_t(3));
        QCOMPARE(frames[0].type, uint16_t(0x0027));
        QVERIFY(frames[0].body == Bytes({0xab, 0xcd}));
        QCOMPARE(frames[1].type, uint16_t(0x0018));
        QVERIFY(frames[1].body.empty());
        QCOMPARE(frames[2].type, uint16_t(0x0034));
        QVERIFY(frames[2].body == Bytes({0x09}));
    }
    void rejectsIncompleteRecordings_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("reason");
        const QByteArray first = "0x0027 len=3 abcd\n";
        QTest::newRow("odd-hex") << first + "0x0008 len=2 a\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("invalid-hex") << first + "0x0008 len=2 gg\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("truncated-body") << first + "0x0008 len=4 aa\n" << 2 << QString("does not match");
        QTest::newRow("missing-body") << first + "0x0008 len=3\n" << 2 << QString("does not match");
        QTest::newRow("mixed-lengths") << first + "0x0008 len=1 aa\n" << 2 << QString("Mixed len conventions");
        QTest::newRow("invalid-type") << first + "0x00gg len=1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("wide-type") << first + "0x10000 len=1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("negative-length") << first + "0x0008 len=-1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("overflow-length")
            << first + "0x0008 len=18446744073709551616\n" << 2 << QString("out of range");
        QTest::newRow("unexpected-text") << first + "record omitted\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("inline-comment") << first + "0x0008 len=2 aa # note\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("empty") << QByteArray() << 1 << QString("no frames");
        QTest::newRow("header-only") << QByteArray("=== recording ===\n# no input\n") << 2 << QString("no frames");
    }
    void rejectsIncompleteRecordings() {
        QFETCH(QByteArray, contents);
        QFETCH(int, line);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("frames.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error;
        QVERIFY(!readRecordedFrames(path, frames, error));
        QVERIFY(frames.empty()); // neither old output nor a valid prefix is a usable recording
        QVERIFY2(error.startsWith(path + ':' + QString::number(line) + ':'), qPrintable(error));
        QVERIFY2(error.contains(reason), qPrintable(error));
    }
    void missingFileIsNotAnEmptyRecording() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("missing.txt");
        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error;
        QVERIFY(!readRecordedFrames(path, frames, error));
        QVERIFY(frames.empty());
        QVERIFY2(error.startsWith(path + ":0: Cannot read recording:"), qPrintable(error));
    }
    void retainsTimedFramesAndEqualTimestampOrder_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<qint64>("lastTime");
        QTest::newRow("retained-bare-types")
            << QByteArray("0 AbCd aBcd\n0 0018\n42 0034 09\n") << qint64(42);
        QTest::newRow("prefixed-types")
            << QByteArray("0 0xAbCd aBcd\n0 0x0018\n42 0x0034 09\n") << qint64(42);
        QTest::newRow("comments-crlf-whitespace")
            << QByteArray("=== retained recording ===\r\n\t# milliseconds type hex\r\n \r\n"
                          " 0\tAbCd\tABcd \r\n0 0018\t\r\n# between equal-time frames and later input\r\n 42 0034 09 ")
            << qint64(42);
        QTest::newRow("maximum-milliseconds")
            << QByteArray("0 AbCd abcd\n0 0018\n9223372036854775807 0034 09\n")
            << std::numeric_limits<qint64>::max();
    }
    void retainsTimedFramesAndEqualTimestampOrder() {
        QFETCH(QByteArray, contents);
        QFETCH(qint64, lastTime);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QString::fromUtf8("timed-\xc3\xa8.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<TimedRecordedFrame> frames{{99, {0xffff, {0xff}}}};
        QString error = "previous error";
        QVERIFY2(readTimedRecordedFrames(path, frames, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(frames.size(), size_t(3));
        QCOMPARE(frames[0].elapsedMs, int64_t(0));
        QCOMPARE(frames[0].frame.type, uint16_t(0xabcd));
        QVERIFY(frames[0].frame.body == Bytes({0xab, 0xcd}));
        QCOMPARE(frames[1].elapsedMs, int64_t(0));
        QCOMPARE(frames[1].frame.type, uint16_t(0x0018));
        QVERIFY(frames[1].frame.body.empty());
        QCOMPARE(frames[2].elapsedMs, int64_t(lastTime));
        QCOMPARE(frames[2].frame.type, uint16_t(0x0034));
        QVERIFY(frames[2].frame.body == Bytes({0x09}));
    }
    void rejectsMalformedTimedRecordings_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("reason");
        const QByteArray first = "10 0027 abcd\n";
        QTest::newRow("odd-hex") << first + "11 0008 a\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("invalid-hex") << first + "11 0008 gg\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("invalid-type") << first + "11 00gg aa\n" << 2 << QString("four-digit hex type");
        QTest::newRow("short-type") << first + "11 008 aa\n" << 2 << QString("four-digit hex type");
        QTest::newRow("wide-type") << first + "11 10000 aa\n" << 2 << QString("four-digit hex type");
        QTest::newRow("missing-type") << first + "11\n" << 2 << QString("four-digit hex type");
        QTest::newRow("negative-time") << first + "-1 0008 aa\n" << 2 << QString("nonnegative elapsed milliseconds");
        QTest::newRow("decreasing-time") << first + "9 0008 aa\n" << 2 << QString("must not decrease");
        QTest::newRow("fractional-time") << first + "10.5 0008 aa\n" << 2 << QString("nonnegative elapsed milliseconds");
        QTest::newRow("signed-time") << first + "+11 0008 aa\n" << 2 << QString("nonnegative elapsed milliseconds");
        QTest::newRow("exponent-time") << first + "1e2 0008 aa\n" << 2 << QString("nonnegative elapsed milliseconds");
        QTest::newRow("overflow-time") << first + "9223372036854775808 0008 aa\n" << 2 << QString("out of range");
        QTest::newRow("extra-body-token") << first + "11 0008 aa bb\n" << 2 << QString("optional hex body");
        QTest::newRow("inline-comment") << first + "11 0008 aa # note\n" << 2 << QString("optional hex body");
        QTest::newRow("physical-line-diagnostic")
            << first + "# retained comment\n\nnot a frame\n" << 4 << QString("nonnegative elapsed milliseconds");
        QTest::newRow("empty") << QByteArray() << 1 << QString("no frames");
        QTest::newRow("comments-only") << QByteArray("=== recording ===\n# no input\n") << 2 << QString("no frames");
    }
    void rejectsMalformedTimedRecordings() {
        QFETCH(QByteArray, contents);
        QFETCH(int, line);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("timed.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<TimedRecordedFrame> frames{{99, {0xffff, {0xff}}}};
        QString error;
        QVERIFY(!readTimedRecordedFrames(path, frames, error));
        QVERIFY(frames.empty()); // neither old output nor the valid prefix survives
        QVERIFY2(error.startsWith(path + ':' + QString::number(line) + ':'), qPrintable(error));
        QVERIFY2(error.contains(reason), qPrintable(error));
    }
    void missingTimedFileIsNotAnEmptyRecording() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("missing.txt");
        std::vector<TimedRecordedFrame> frames{{99, {0xffff, {0xff}}}};
        QString error;
        QVERIFY(!readTimedRecordedFrames(path, frames, error));
        QVERIFY(frames.empty());
        QVERIFY2(error.startsWith(path + ":0: Cannot read recording:"), qPrintable(error));
    }
    void resourcesRetainIdsAndBytes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile first(dir.filePath("0aBcDe01"));
        QVERIFY(first.open(QIODevice::WriteOnly));
        QCOMPARE(first.write(QByteArray::fromHex("00ff1020")), qint64(4));
        first.close();
        QFile second(dir.filePath("ffffffff"));
        QVERIFY(second.open(QIODevice::WriteOnly));
        QCOMPARE(second.write("second"), qint64(6));
        second.close();

        std::map<uint32_t, QByteArray> resources{{1, "old"}};
        QString error = "previous error";
        QVERIFY2(readRecordedResources(dir.path(), resources, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(resources.size(), size_t(2));
        QCOMPARE(resources.at(0x0abcde01), QByteArray::fromHex("00ff1020"));
        QCOMPARE(resources.at(0xffffffff), QByteArray("second"));
    }
    void invalidResourcesDiscardTheWholeInput_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<bool>("directory");
        QTest::addColumn<QString>("reason");
        QTest::newRow("non-hex-name") << QString("not-hex!") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("short-id") << QString("abcdef") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("wide-id") << QString("1abcdef01") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("empty-file") << QString("abcdef01") << QByteArray() << false << QString("empty");
        QTest::newRow("directory-entry") << QString("abcdef01") << QByteArray() << true << QString("Expected a resource file");
    }
    void invalidResourcesDiscardTheWholeInput() {
        QFETCH(QString, name);
        QFETCH(QByteArray, contents);
        QFETCH(bool, directory);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile valid(dir.filePath("00000001"));
        QVERIFY(valid.open(QIODevice::WriteOnly));
        QCOMPARE(valid.write("valid"), qint64(5));
        valid.close();
        if (directory) {
            QVERIFY(QDir(dir.path()).mkdir(name));
        } else {
            QFile invalid(dir.filePath(name));
            QVERIFY(invalid.open(QIODevice::WriteOnly));
            QCOMPARE(invalid.write(contents), qint64(contents.size()));
        }

        std::map<uint32_t, QByteArray> resources{{2, "old"}};
        QString error;
        QVERIFY(!readRecordedResources(dir.path(), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.startsWith(dir.filePath(name) + ':'), qPrintable(error));
        QVERIFY2(error.contains(reason), qPrintable(error));
    }
    void missingAndEmptyResourceDirectoriesFail() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        std::map<uint32_t, QByteArray> resources{{1, "old"}};
        QString error;
        QVERIFY(!readRecordedResources(dir.path(), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.contains("contains no files"), qPrintable(error));
        resources.emplace(1, "old");
        QVERIFY(!readRecordedResources(dir.filePath("missing"), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.contains("Missing resource directory"), qPrintable(error));
    }
};

FUGUE_TEST_GUILESS_MAIN(RecordedInputTest)
#include "recordedinput_test.moc"
