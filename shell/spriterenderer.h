// QRhi sprite resources are GUI-thread-only; the source draw list is immutable.
#pragma once
#include "shell/mapsprite.h"
#include "shell/surfaceplan.h"
#include "shell/compositionmetrics.h"
#include <memory>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiViewport;
namespace fugue::shell {
class CompositionPlan;
class SpriteRenderer {
public:
    // Full-plane reference is retained for exact native differential tests.
    explicit SpriteRenderer(bool boundedMaterials = true);
    ~SpriteRenderer();
    bool initialize(QRhi* rhi, QRhiRenderPassDescriptor* pass, int samples);
    // Returns uploaded texture bytes. Positions/tints use a separate dynamic vertex buffer. worldClip, in the source's
    // own pixels (its top left at 0,0), is where world sprites are drawn (mapWorldClip); screen objects are not cut.
    // worldView, in the same pixels, is the view (mapWorldView): a world sprite coloured by a colour matrix is drawn
    // stretched from it onto worldClip. Without it such sprites are only cut.
    uint64_t prepare(QRhiResourceUpdateBatch* updates, const MapSprites& sprites,
                     QRect source, std::optional<QRect> worldClip = {}, QRect worldView = {});
    // Consumes the updates, then records dependency-ordered plane/target passes.
    void renderOffscreen(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch* updates);
    // Draws into the pass in progress, over the whole of viewport. The viewport is set here, after a pipeline is
    // bound: QRhi takes the scissor rectangle from a viewport only then, and Vulkan draws nothing without one.
    void draw(QRhiCommandBuffer* cb, const QRhiViewport& viewport);
    bool ready() const;
    // Resource/recording admission is separate from graph support. The presenter
    // publishes this only after composing the complete frame, never on failure.
    std::shared_ptr<const CompositionPlan> preparedPlan() const;
    // GUI-thread profiling window. CPU planning is part of, not additional to,
    // MapWidget's command-recording time. Execution counts describe the latest
    // complete plan; rejection/candidate counts describe the latest preparation.
    struct Stats {
        uint64_t preparations = 0, replays = 0, draws = 0, surfaces = 0;
        uint64_t requestedSurfaces = 0, planRefusals = 0;
        // Executed offscreen passes versus physical pool + final MAP storage.
        uint64_t passes = 0, surfaceBytes = 0, workPixels = 0, ignoredFilters = 0;
        PlanRejection rejection;
        CompositionMetrics composition;
        uint64_t candidatePasses = 0, reachablePasses = 0;
        QSize sourceSize;
        double prepareMs = 0;
    };
    Stats takeStats();
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    const bool boundedMaterials_; // configuration survives QRhi resource recreation
};
}
