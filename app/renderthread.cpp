#include "app/renderthread.h"
#include <chrono>
#include <cstdio>
#include <cctype>

namespace {
std::string mapKey(std::string id) {
    for (char& c : id) c = char(std::tolower(static_cast<unsigned char>(c)));
    return id;
}
}  // namespace

void RenderThread::setUseGpu(bool enabled) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (useGpu_ == enabled) return;
        useGpu_ = enabled;
        rendererDirty_ = true;
    }
    cv_.notify_one();
}

RenderThread::Metrics RenderThread::metrics() const {
    std::lock_guard<std::mutex> g(metricsMu_);
    auto result = metrics_;
    result.dropped = dropped_;
    result.unchanged = unchanged_;
    const auto now = fugue::app::MetricsClock::now();
    for (const auto& [id, history] : mapHistory_) result.maps.emplace(id, history.snapshot(now));
    return result;
}

RenderThread::DirectFrames RenderThread::takeDirectFrames() {
    std::lock_guard<std::mutex> g(mu_);
    DirectFrames result;
    result.swap(directFrames_);
    return result;
}

void RenderThread::reportSubmission(const std::string& id, double cpuMs, double lastGpuMs, const std::string& backend,
                                    const fugue::app::RenderPhases& phases) {
    if (id.empty()) executed_++;
    if (gpuExecuted_++ == 0)
        std::fprintf(stderr, "[render] first direct GPU frame submitted (no per-frame readback)\n");
    std::lock_guard<std::mutex> g(metricsMu_);
    (id.empty() ? metrics_.mainFrames : metrics_.auxFrames)++;
    metrics_.gpuFrames++;
    metrics_.lastRenderMs = cpuMs;
    metrics_.lastGpuMs = lastGpuMs;
    metrics_.lastGpu = metrics_.lastAsync = true;
    metrics_.backend = backend;
    metrics_.fallbackReason.clear();
    auto& map = mapHistory_[mapKey(id)];
    ++map.frames;
    ++map.gpuFrames;
    map.lastGpu = true;
    map.lastGpuMs = lastGpuMs;
    map.backend = backend;
    map.fallbackReason.clear();
    map.cpuSubmit.add(cpuMs);
    map.gpu.add(lastGpuMs);
    for (size_t i = 0; i < phases.wallMs.size(); ++i) map.phases[i].add(phases.wallMs[i]);
    for (size_t i = 0; i < phases.recordMs.size(); ++i) map.recording[i].add(phases.recordMs[i]);
}

void RenderThread::prepared(const std::string& id, double ms) {
    if (!std::isfinite(ms) || ms < 0) return;
    std::lock_guard<std::mutex> g(metricsMu_);
    auto& map = mapHistory_[id];
    ++map.prepared;
    map.preparation.add(ms);
}

void RenderThread::countDropped(const std::string& id) {
    if (id.empty()) ++dropped_;
    std::lock_guard<std::mutex> g(metricsMu_);
    ++mapHistory_[id].dropped;
}

void RenderThread::submitFallback(const std::string& id, GpuDrawList list, std::string reason) {
    {
        std::lock_guard<std::mutex> g(mu_);
        fallbacks_[mapKey(id)] = Fallback{std::move(list), std::move(reason)};
    }
    cv_.notify_one();
}

void RenderThread::submit(fugue::render::DrawList dl, double prepareMs) {
    prepared({}, prepareMs);
    {
        std::lock_guard<std::mutex> g(mu_);
        if (slot_) countDropped({});
        slot_ = std::move(dl);
    }
    cv_.notify_one();
}

void RenderThread::submitAux(const std::string& id, fugue::render::DrawList dl, double prepareMs) {
    const auto key = mapKey(id);
    prepared(key, prepareMs);
    {
        std::lock_guard<std::mutex> g(mu_);
        if (auxSlots_.count(key)) countDropped(key);
        auxSlots_[key] = std::move(dl);
    }
    cv_.notify_one();
}

void RenderThread::forgetAux(const std::string& id) {
    {
        std::lock_guard<std::mutex> g(mu_);
        auxForget_.insert(mapKey(id));
    }
    cv_.notify_one();
}

void RenderThread::removeAux(const std::string& id) {
    {
        std::lock_guard<std::mutex> g(mu_);
        const auto key = mapKey(id);
        auxRemove_.insert(key);
        auxForget_.erase(key);
        auxSlots_.erase(key);
        directFrames_.erase(key);
        fallbacks_.erase(key);
    }
    cv_.notify_one();
}

void RenderThread::requestStop() {
    {
        std::lock_guard<std::mutex> g(mu_);
        stop_ = true;
    }
    cv_.notify_all();
}

void RenderThread::run() {
    bool useGpu = false;
    GpuDrawList lastDirectList;
    std::set<std::string> invalidAux;
    auto execute = [&](const fugue::render::DrawList& dl, const std::string& id, std::string reason = {}) {
        const bool main = id.empty();
        const auto start = std::chrono::steady_clock::now();
        if (useGpu) {
            cpuFallbacks_++;
            if (reason.empty()) reason = "GPU support was not built";
        }
        auto image = fugue::render::execute(dl);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        {
            std::lock_guard<std::mutex> g(metricsMu_);
            (main ? metrics_.mainFrames : metrics_.auxFrames)++;
            metrics_.cpuFrames++;
            if (useGpu) metrics_.fallbacks++;
            metrics_.lastRenderMs = ms;
            metrics_.lastGpu = metrics_.lastAsync = false;
            metrics_.lastGpuMs = -1;
            metrics_.backend.clear();
            metrics_.fallbackReason = reason;
            auto& map = mapHistory_[id];
            ++map.frames;
            ++map.cpuFrames;
            if (useGpu) ++map.fallbacks;
            map.lastGpu = false;
            map.lastGpuMs = -1;
            map.backend.clear();
            map.fallbackReason = std::move(reason);
            map.cpuRender.add(ms);
        }
        return image;
    };
    auto publish = [&](const std::string& id, GpuDrawList list, fugue::shell::HitPtr hit) {
        bool wake;
        {
            std::lock_guard<std::mutex> g(mu_);
            wake = directFrames_.empty();
            auto it = directFrames_.find(id);
            if (it != directFrames_.end() && it->second.list != list) countDropped(id);
            directFrames_[id] = DirectFrame{std::move(list), std::move(hit)};
        }
        if (wake) emit gpuListReady();
    };
    for (;;) {
        fugue::render::DrawList dl;
        std::map<std::string, fugue::render::DrawList> aux;
        std::map<std::string, Fallback> fallbacks;
        bool haveMain = false, force = false;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [&] {
                return stop_ || rendererDirty_ || slot_ || !auxSlots_.empty() || !fallbacks_.empty() ||
                       !auxForget_.empty() || !auxRemove_.empty();
            });
            if (stop_) break;
            force = rendererDirty_;
            rendererDirty_ = false;
            useGpu = useGpu_;
            if (slot_) {
                dl = std::move(*slot_);
                slot_.reset();
                haveMain = true;
            }
            aux.swap(auxSlots_);
            // Rebuilds invalidate unchanged skips, not the lists needed for idle renderer switching.
            for (const std::string& id : auxRemove_) {
                auxLast_.erase(id);
                invalidAux.erase(id);
                std::lock_guard<std::mutex> metricsLock(metricsMu_);
                mapHistory_.erase(id);
            }
            auxRemove_.clear();
            invalidAux.insert(auxForget_.begin(), auxForget_.end());
            auxForget_.clear();
            fallbacks.swap(fallbacks_);
            if (force) {
                if (!haveMain && last_) {
                    dl = *last_;
                    dl.hit = lastHit_.lock();
                    haveMain = true;
                }
                for (const auto& [id, cached] : auxLast_)
                    if (!aux.count(id)) aux.emplace(id, *cached);
            }
        }
        bool direct = false;
#ifdef FUGUE_GPU
        direct = useGpu;
        if (force) std::fprintf(stderr, "[render] requested renderer: %s\n", useGpu ? "GPU" : "CPU");
#endif
        if (direct)
            for (auto& [id, fallback] : fallbacks) {
                auto image = execute(*fallback.list, id, fallback.reason);
                if (id.empty()) executed_++;
                if (image.w <= 0 || image.h <= 0) continue;
                QImage q(image.px.data(), image.w, image.h, image.w * 4, QImage::Format_RGBX8888);
                emit fallbackFrame(QString::fromStdString(id), fallback.list, q.copy());
            }
        for (auto& [id, al] : aux) {
            auto hit = al.hit;
            auto last = auxLast_.find(id);
            const bool invalid = invalidAux.erase(id) != 0;
            if (!force && !invalid && last != auxLast_.end() && fugue::render::sameDrawList(al, *last->second)) {
                {
                    std::lock_guard<std::mutex> g(metricsMu_);
                    ++mapHistory_[id].unchanged;
                }
                if (direct)
                    publish(id, last->second, std::move(hit));  // the picture stands; the hit scene may not
                else if (hit)
                    emit auxSceneOnly(QString::fromStdString(id), std::move(hit));
                continue;
            }
            auto list = std::make_shared<const fugue::render::DrawList>(std::move(al));
            auxLast_[id] = list;
            if (direct) {
                publish(id, list, std::move(hit));
                continue;
            }
            auto img = execute(*list, id);
            if (img.w <= 0 || img.h <= 0) continue;
            QImage q(img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBX8888);
            emit auxFrame(QString::fromStdString(id), q.copy(), std::move(hit));
        }
        if (!haveMain) continue;
        auto hit = dl.hit;
        lastHit_ = hit;
        // An unchanged picture is not executed again: the executor is a pure function of the list (render/ir.h), so the
        // image on screen is still this list's output. Only the hit scene is refreshed, since atom refs and names can
        // change without a pixel changing. Most hand-offs repeat the previous list, and executing one is a real cost.
        const bool unchanged = !force && last_ && fugue::render::sameDrawList(dl, *last_);
        if (unchanged) {
            unchanged_++;
            {
                std::lock_guard<std::mutex> g(metricsMu_);
                ++mapHistory_[""].unchanged;
            }
            if (!direct) {
                emit sceneOnly(std::move(hit));
                continue;
            }
        }
        if (direct) {
            if (!unchanged || !lastDirectList) {
                last_ = dl;
                last_->hit.reset();
                lastDirectList = std::make_shared<const fugue::render::DrawList>(std::move(dl));
            }
            publish({}, lastDirectList, std::move(hit));
            continue;
        }
        auto img = execute(dl, {});
        executed_++;
        last_ = std::move(dl);
        last_->hit.reset();  // only the pixels are compared; do not keep an old hit scene alive
        if (img.w <= 0 || img.h <= 0) continue;
        // The map is opaque after the black-ground flatten (alpha == 255 everywhere), so RGBX8888 is safe.
        // copy() detaches from the vector's storage before it dies.
        QImage q(img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBX8888);
        emit frame(q.copy(), std::move(hit));
    }
    if (gpuExecuted_ || cpuFallbacks_)
        std::fprintf(stderr, "[render] session totals: GPU=%llu CPU-fallback=%llu\n",
                     (unsigned long long)gpuExecuted_.load(), (unsigned long long)cpuFallbacks_.load());
}
