#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include "audio/sink.h"

class AudioSinkTest : public QObject {
    Q_OBJECT
private slots:
    void silentAndWav() {
        using namespace fugue::audio;
        std::string err;
        const float samples[] = {0.0f, 0.5f, -0.5f, 0.0f};
        auto silent = openSink("none", 44100, &err);
        QVERIFY(dynamic_cast<NullSink*>(silent.get()));
        QVERIFY(err.empty());
        QVERIFY(silent->write(samples, 2, &err));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto wav = openSink("wav:" + (dir.path() + "/mix.wav").toStdString(), 44100, &err);
        QVERIFY2(err.empty(), err.c_str());
        QVERIFY(wav->write(samples, 2, &err));
        wav->close();
        wav->close();
        QFile file(dir.path() + "/mix.wav");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("RIFF"));
        QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(bytes.size(), qsizetype(44 + 8));
    }
#ifndef WIN32
    void commandPipe() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        std::string err;
        auto sink = fugue::audio::openExecSink({"/bin/dd", "of=" + (dir.path() + "/pcm").toStdString()}, &err);
        QVERIFY2(sink, err.c_str());
        const float silence[8]{};
        QVERIFY2(sink->write(silence, 4, &err), err.c_str());
        sink->close();
        sink->close();
        QFile file(dir.path() + "/pcm");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(16, '\0'));
        QVERIFY(!fugue::audio::openExecSink({"/nonexistent/fugue-player"}, &err));
        QVERIFY(!err.empty());
    }
#endif
#ifdef FUGUE_TEST_APPLE
    void invalidCoreAudioRate() {
        std::string err;
        QVERIFY(!fugue::audio::openCoreAudioSink(0, &err));
        QVERIFY(!err.empty());
    }
#endif
};

#if defined(WIN32) || defined(FUGUE_TEST_APPLE)
class AudioDeviceTest : public QObject {
    Q_OBJECT
private slots:
#ifdef FUGUE_TEST_APPLE
    void coreAudioDevice() {
        std::string err;
        auto sink = fugue::audio::openSink("auto", 44100, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(sink->name(), std::string("coreaudio"));
        const float silence[2048]{};
        for (int i = 0; i < 8; ++i) QVERIFY2(sink->write(silence, 1024, &err), err.c_str());
        sink->close();
        sink->close();
    }
#endif
#ifdef WIN32
    void nativeWindowsDevice() {
        std::string err;
        auto sink = fugue::audio::openSink("auto", 44100, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(sink->name(), std::string("waveout"));
        float silence[2048]{};
        for (int i = 0; i < 12; ++i) QVERIFY2(sink->write(silence, 1024, &err), err.c_str());
        sink->close();
        sink->close();
        QVERIFY(!sink->write(silence, 1024, &err));
    }
#endif
};

int fugue_run_audio_device(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    AudioDeviceTest test;
    QTest::setMainSourcePath(__FILE__);
    return QTest::qExec(&test, argc, argv);
}
#endif

FUGUE_TEST_GUILESS_MAIN(AudioSinkTest)
#include "audio_sink_test.moc"
