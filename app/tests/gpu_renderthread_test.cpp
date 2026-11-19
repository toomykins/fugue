#include <QtTest>
#include <QSignalSpy>
#include "app/renderthread.h"

using namespace fugue::render;
using fugue::shell::HitPtr;

namespace {
DrawList picture(uint8_t red = 201) {
    auto sprite = std::make_shared<Sprite>();
    sprite->w = 2;
    sprite->h = 1;
    sprite->px = {red, 31, 55, 255, 0, 170, 220, 128};
    DrawLayer layer;
    layer.sprite = sprite;
    DrawList dl;
    dl.outW = 3;
    dl.outH = 2;
    dl.planes.push_back(PlanePass{0, {layer}, {}});
    dl.hit = std::make_shared<HitScene>();
    return dl;
}
struct Stop {
    RenderThread& thread;
    ~Stop() {
        thread.requestStop();
        thread.wait();
    }
};
QImage expected(const DrawList& dl) {
    auto image = execute(dl);
    return QImage(image.px.data(), image.w, image.h, image.w * 4, QImage::Format_RGBX8888).copy();
}
}  // namespace

// GPU execution/presentation is covered by gpu_present; the worker only routes GPU lists now.
class GpuRenderThreadTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<HitPtr>();
        qRegisterMetaType<GpuDrawList>();
    }

    void switchWhileIdleRedrawsMainAuxAndLatestHits() {
        RenderThread thread;
        Stop stop{thread};
        QSignalSpy frames(&thread, &RenderThread::frame), aux(&thread, &RenderThread::auxFrame);
        QSignalSpy scenes(&thread, &RenderThread::sceneOnly);
        auto dl = picture();
        thread.submit(dl);
        thread.submitAux("Camera", dl);
        thread.start();
        QTRY_COMPARE(frames.size(), 1);
        QTRY_COMPARE(aux.size(), 1);
        QCOMPARE(thread.metrics().cpuFrames, uint64_t(2));
        dl.hit = std::make_shared<HitScene>();
        thread.submit(dl);
        QTRY_COMPARE(scenes.size(), 1);
        for (int i = 0; i < 2; ++i) {
            thread.setUseGpu(true);
            RenderThread::DirectFrames pending;
            auto drain = [&] {
                for (auto& [id, frame] : thread.takeDirectFrames()) pending[id] = std::move(frame);
                return pending.size() == 2;
            };
            QTRY_VERIFY(drain());
            QCOMPARE(pending.at("").hit, dl.hit);
            QVERIFY(sameDrawList(*pending.at("camera").list, dl));
            QCOMPARE(frames.size(), i + 1);
            QCOMPARE(aux.size(), i + 1);
            // Counts describe actual submissions, not merely worker handoff.
            for (const auto& [id, frame] : pending) thread.reportSubmission(id, 1, -1, "test backend");
            QCOMPARE(thread.metrics().mainFrames, uint64_t(2 * i + 2));
            QCOMPARE(thread.metrics().auxFrames, uint64_t(2 * i + 2));
            QVERIFY(thread.metrics().lastAsync);
            thread.setUseGpu(false);
            QTRY_COMPARE(frames.size(), i + 2);
            QTRY_COMPARE(aux.size(), i + 2);
            QCOMPARE(qvariant_cast<QImage>(frames.back()[0]), expected(dl));
            QCOMPARE(qvariant_cast<HitPtr>(frames.back()[1]), dl.hit);
            QVERIFY(!thread.metrics().lastGpu);
        }
        QCOMPARE(thread.gpuExecuted(), uint64_t(4));
        QCOMPARE(thread.metrics().cpuFrames, uint64_t(6));
        QCOMPARE(thread.cpuFallbacks(), uint64_t(0));
    }

    void latestWinsPerViewportAndAuxiliaryRemount() {
        RenderThread thread(true);
        std::atomic<int> notifications{0};
        Stop stop{thread};
        connect(&thread, &RenderThread::gpuListReady, &thread, [&] { ++notifications; }, Qt::DirectConnection);
        QSignalSpy frames(&thread, &RenderThread::frame), aux(&thread, &RenderThread::auxFrame);
        for (int i = 0; i < 10; ++i) {
            thread.submit(picture(uint8_t(i)));
            thread.submitAux("Camera", picture(uint8_t(100 + i)));
            thread.submitAux("radar", picture(uint8_t(200 + i)));
        }
        thread.start();
        QTRY_COMPARE(notifications.load(), 1);
        // main is published after both auxiliaries; wait until all three slots are populated.
        RenderThread::DirectFrames pending;
        auto drain = [&] {
            for (auto& [id, frame] : thread.takeDirectFrames()) pending[id] = std::move(frame);
            return pending.size() == 3;
        };
        QTRY_VERIFY(drain());
        QVERIFY(sameDrawList(*pending.at("").list, picture(9)));
        QVERIFY(sameDrawList(*pending.at("camera").list, picture(109)));
        QVERIFY(sameDrawList(*pending.at("radar").list, picture(209)));
        QCOMPARE(thread.dropped(), uint64_t(9));
        QCOMPARE(frames.size(), 0);
        QCOMPARE(aux.size(), 0);
        QCOMPARE(thread.gpuExecuted(), uint64_t(0));
        thread.forgetAux("CAMERA");
        thread.submitAux("camera", picture(109));
        pending.clear();
        QTRY_VERIFY(([&] {
            for (auto& [id, frame] : thread.takeDirectFrames()) pending[id] = std::move(frame);
            return !pending.empty();
        })());
        QCOMPARE(pending.size(), size_t(1));
        QVERIFY(pending.count("camera"));
        QVERIFY(sameDrawList(*pending.at("camera").list, picture(109)));
    }

    void independentFallbackSlotsAndMetrics() {
        RenderThread thread(true);
        Stop stop{thread};
        QSignalSpy fallback(&thread, &RenderThread::fallbackFrame);
        auto main = std::make_shared<const DrawList>(picture(11));
        auto camera = std::make_shared<const DrawList>(picture(22));
        auto radar = std::make_shared<const DrawList>(picture(33));
        thread.submitFallback("", main, "main limit");
        thread.submitFallback("camera", camera, "camera limit");
        thread.submitFallback("radar", radar, "radar limit");
        thread.start();
        QTRY_COMPARE(fallback.size(), 3);
        QCOMPARE(thread.cpuFallbacks(), uint64_t(3));
        QCOMPARE(thread.metrics().mainFrames, uint64_t(1));
        QCOMPARE(thread.metrics().auxFrames, uint64_t(2));
        for (const auto& args : fallback) {
            const auto list = qvariant_cast<GpuDrawList>(args[1]);
            QCOMPARE(qvariant_cast<QImage>(args[2]), expected(*list));
            if (args[0].toString().isEmpty())
                QCOMPARE(list, main);
            else if (args[0].toString() == "camera")
                QCOMPARE(list, camera);
            else
                QCOMPARE(list, radar);
        }
        thread.reportSubmission("camera", 2, 1, "test backend");
        QVERIFY(thread.metrics().lastAsync);
        QVERIFY(thread.metrics().fallbackReason.empty());
        QCOMPARE(thread.metrics().auxFrames, uint64_t(3));
        QCOMPARE(thread.metrics().gpuFrames, uint64_t(1));
    }
};
QTEST_MAIN(GpuRenderThreadTest)
#include "gpu_renderthread_test.moc"
