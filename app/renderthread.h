// RenderThread: executes a DrawList (CPU executor, straight-alpha RGBA8) off the GUI thread and hands the GUI a
// finished opaque QImage. The mailbox is latest-wins: if rendering falls behind, older lists are dropped, not queued.
// The GUI thread only blits (MapWidget::paintEvent). In GPU mode the immutable list goes to the map's direct presenter
// instead, which records it on the GUI thread; each map, main and auxiliary, has its own slot.
#pragma once
#include <QImage>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <string>
#include "render/hittest.h"
#include "shell/mapwidget.h"  // declares the HitPtr metatype the frame signal needs
#include "render/ir.h"
#include "app/rendermetrics.h"

using GpuDrawList = std::shared_ptr<const fugue::render::DrawList>;
Q_DECLARE_METATYPE(GpuDrawList)

class RenderThread : public QThread {
    Q_OBJECT
public:
    explicit RenderThread(bool useGpu = false) : useGpu_(useGpu) {}
    // Any thread. Applied between render batches; cached main/auxiliary lists are redrawn even while idle.
    void setUseGpu(bool enabled);
    struct Metrics {
        uint64_t mainFrames = 0, auxFrames = 0, gpuFrames = 0, cpuFrames = 0, fallbacks = 0;
        uint64_t dropped = 0, unchanged = 0;  // main-map mailbox / unchanged-list skips
        double lastRenderMs = 0;  // last execution, or CPU submission time when lastAsync; not end-to-end latency
        bool lastGpu = false, lastAsync = false;
        double lastGpuMs = -1;  // last available GPU timestamp, not necessarily the submitted frame
        std::string backend, fallbackReason;
        std::map<std::string, fugue::app::MapMetrics> maps;  // empty key = main, all others independent auxiliary maps
    };
    Metrics metrics() const;
    // Empty id denotes the main map; auxiliary ids are case-normalized control names.
    struct DirectFrame {
        GpuDrawList list;
        fugue::shell::HitPtr hit;
    };
    using DirectFrames = std::map<std::string, DirectFrame>;
    DirectFrames takeDirectFrames();  // GUI drains all viewports with one queued wake-up
    void reportSubmission(const std::string& id, double cpuMs, double lastGpuMs, const std::string& backend,
                          const fugue::app::RenderPhases& phases = {});
    void submitFallback(const std::string& id, GpuDrawList list, std::string reason);
    // Preparation is measured at the producer, including unchanged/dropped scenes; -1 means not instrumented.
    void submit(fugue::render::DrawList dl, double prepareMs = -1);
    // A secondary map control's list (render::assembleSecondaryMap), by control id, with its own latest-wins slot and
    // unchanged-list skip.
    void submitAux(const std::string& id, fugue::render::DrawList dl, double prepareMs = -1);
    // A widget for that map was (re)built with no picture yet: the next list for `id` is executed even when it equals the
    // last one, so the new widget gets a frame. Any thread.
    void forgetAux(const std::string& id);
    void removeAux(const std::string& id);  // release cached/pending work for an unmounted control
    void requestStop();
    uint64_t executed() const { return executed_; }
    uint64_t dropped() const { return dropped_; }
    uint64_t gpuExecuted() const { return gpuExecuted_; }    // main + auxiliary frames
    uint64_t cpuFallbacks() const { return cpuFallbacks_; }  // GPU requested, but this frame used CPU
    uint64_t unchanged() const { return unchanged_; }  // lists identical to the last executed one (not re-executed)
signals:
    // Emitted from this thread; GUI receivers get it queued. `hit` is the scene this image was drawn from (null if the
    // list carried none), so hit-tests match the picture on screen.
    void frame(const QImage& img, std::shared_ptr<const fugue::render::HitScene> hit);
    // The list was identical to the last executed one: the image stands, only the hit scene is newer.
    void sceneOnly(std::shared_ptr<const fugue::render::HitScene> hit);
    // A secondary map's finished picture (submitAux) and the scene it was drawn from.
    void auxFrame(const QString& id, const QImage& img, std::shared_ptr<const fugue::render::HitScene> hit);
    // A secondary map's list was unchanged: only its scene is newer.
    void auxSceneOnly(const QString& id, std::shared_ptr<const fugue::render::HitScene> hit);
    void gpuListReady();  // wake-up only; one pending notification until takeDirectFrames()
    void fallbackFrame(const QString& id, GpuDrawList list, const QImage& image);

protected:
    void run() override;

private:
    bool useGpu_;  // guarded by mu_; the worker snapshots this once per batch
    bool rendererDirty_ = true;
    struct Fallback {
        GpuDrawList list;
        std::string reason;
    };
    std::map<std::string, Fallback> fallbacks_;  // guarded by mu_; GUI rejects superseded results
    DirectFrames directFrames_;                  // guarded by mu_
    std::atomic<uint64_t> gpuExecuted_{0}, cpuFallbacks_{0};
    mutable std::mutex metricsMu_;
    Metrics metrics_;
    std::map<std::string, fugue::app::MapHistory> mapHistory_;  // guarded by metricsMu_
    void prepared(const std::string& id, double ms);
    void countDropped(const std::string& id);
    std::mutex mu_;
    std::condition_variable cv_;
    std::optional<fugue::render::DrawList> slot_;
    bool stop_ = false;
    std::atomic<uint64_t> executed_{0}, dropped_{0}, unchanged_{0};
    std::optional<fugue::render::DrawList> last_;              // the last EXECUTED list (this thread only)
    std::map<std::string, fugue::render::DrawList> auxSlots_;  // guarded by mu_
    std::set<std::string> auxRemove_;                          // guarded by mu_ (removeAux)
    std::set<std::string> auxForget_;                          // guarded by mu_ (forgetAux)
    std::weak_ptr<const fugue::render::HitScene> lastHit_;     // the main map's latest scene, for a renderer switch
    std::map<std::string, GpuDrawList> auxLast_;               // this thread only
};
