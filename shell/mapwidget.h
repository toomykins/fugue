// QRhi presentation and picking of immutable worker frames (terrain, HUD and images).
#pragma once
#include <QElapsedTimer>
#include <QRhiWidget>
#include <QImage>
#include <memory>
#include <functional>
#include <QHash>
#include <QSet>
#include "shell/mapmetrics.h"
#include "shell/mapmailbox.h"
#include "shell/mappresentation.h"

class QLabel;
class QRhi;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiGraphicsPipeline;
class QRhiTextureRenderTarget;
class QRhiRenderPassDescriptor;

namespace fugue::shell {
#ifdef Q_OS_MACOS
// Whether this Mac has a Metal device, asked as Qt asks before it settles on Metal (shell/gpubackend.h).
bool metalAvailable();
#endif
class SpriteRenderer;
class MapWidget : public QRhiWidget {
public:
    explicit MapWidget(QWidget* parent = nullptr);
    ~MapWidget() override;
    void setViewSize(QSize size);
    void setMailbox(std::shared_ptr<MapMailbox> mailbox);
    void requestMapFrame();
    void setInputHandler(QString control, bool defaultMap, std::function<void(const MapPointer&)> handler);
    void setMetrics(std::shared_ptr<MapMetrics> metrics) { metrics_ = std::move(metrics); }
    void setMetricsVisible(bool visible);
    bool metricsVisible() const;
    QSize viewSize() const { return viewSize_; }
    // The graphics backend the widget is drawing with ("D3D11", "OpenGL", "Metal", "Vulkan"); empty before its
    // first frame.
    QString createdBackend() const;
    void setPresentationSettings(MapPresentationSettings settings);
    MapPresentationSettings presentationSettings() const { return settings_; }
    QSize presentedViewSize() const;
    // Where a pixel of the presented scene is in the widget (the FUGUE_AUTO_RIGHTCLICK dev hook).
    QPointF sceneToWidget(QPointF scenePixel) const;
    // Explicit GUI-thread capture of the last complete native MAP composition.
    // Call outside render()/frameSubmitted (e.g. a queued client command). No
    // worker frame is consumed, redraw requested or timing sample recorded.
    QImage screenshot(QString* error = nullptr);
    std::function<void()> presentationChanged;

    // The verb popup closed. It swallowed the pointer's motion, so what is under the pointer is reported now, with
    // right=1 or middle=1 while that button is still down; a right press, when it is what closed the menu, sends no
    // MouseDown. A middle press the menu took is released here as a click (middleFromMenu_).
    void popupClosing();  // as it hides: notes a right button that is down
    void popupClosed();   // once it is hidden: the hover report
protected:
    bool event(QEvent*) override;
    void initialize(QRhiCommandBuffer*) override;
    void render(QRhiCommandBuffer*) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void hideEvent(QHideEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void refreshHover();
    void leaveHover();
    void cancelGestures();
    std::optional<MapPointer> gesturePointer(QMouseEvent*, Qt::MouseButton, const MapPointer&) const;
    void moveGestures(QMouseEvent*);
    bool hoverInside_ = false, hoverQueued_ = false;
    // The pointer has moved over the map since the last button release: only then is a still pointer picked again
    // when the map changes (stock 516.1689, tests/data/hover-world).
    bool hoverArmed_ = false;
    // A pointer event that names nothing on the map: the pointer left it (Leave) or a release with nothing under it
    // that belongs to the press (Released).
    void sendUnpicked(MapPointer::Kind);
    QElapsedTimer closedPopupTimer_;  // running: the verb menu just closed under a held right button
    // The verb menu closed with the middle button held, its press taken by the menu: stock 516.1689 sends MouseUp and
    // Click where that button is released, with no MouseDown (tests/data/hover-world round3 mmidheld).
    bool middleFromMenu_ = false;
    void refreshMetrics();
    void placeMetrics();
    QLabel* overlay_ = nullptr;
    std::shared_ptr<MapMetrics> metrics_ = std::make_shared<MapMetrics>();
    void applyFrame(const QImage& terrain, const QRect& source, MapMetrics::FrameTiming timing);
    std::shared_ptr<MapMailbox> mailbox_;
    uint64_t renderedSequence_ = 0;
    int64_t lastRedrawNs_ = 0;  // requestMapFrame's redraw cap without vsync
    bool redrawQueued_ = false;
    MapMetrics::FrameTiming pendingTiming_, submittedTiming_;
    std::optional<MapPointer> pointerAt(QPointF position, QPoint global, Qt::KeyboardModifiers mods,
                                        Qt::MouseButton button) const;
    QString control_;
    bool defaultMap_ = true;
    std::function<void(const MapPointer&)> input_;
    std::shared_ptr<const uint8_t> inputSurface_ = std::make_shared<const uint8_t>(0);
    struct Press {
        std::shared_ptr<const MapPointer> source;
        MapPointer last;
        bool dragged = false, dbl = false;
    };
    QHash<int, Press> presses_;
    QPointF wheelRemainder_;
    MapGeometry geometry_, inputGeometry_;
    QRect inputSource_;
    QSize inputView_;
    QRectF inputDestination_;
    MapPresentationSettings settings_;
    MapPresentation presentation() const;
    bool ensureScene(QSize size);
    QSize viewSize_{0, 0};
    QImage image_;
    MapSprites sprites_;
    std::shared_ptr<const CompositionPlan> inputPlan_;
    std::unique_ptr<SpriteRenderer> spriteRenderer_;
    QRect source_;
    bool imageDirty_ = true;
    QRhi* resourceRhi_ = nullptr;
    std::unique_ptr<QRhiTexture> sceneTexture_;
    std::unique_ptr<QRhiTextureRenderTarget> sceneTarget_;
    std::unique_ptr<QRhiRenderPassDescriptor> scenePass_;
    std::unique_ptr<QRhiBuffer> presentUniforms_;
    std::unique_ptr<QRhiSampler> linearSampler_;
    std::unique_ptr<QRhiShaderResourceBindings> presentBindings_[2];
    std::unique_ptr<QRhiGraphicsPipeline> presentPipeline_;
    std::unique_ptr<QRhiTexture> texture_;
    std::unique_ptr<QRhiSampler> sampler_;
    std::unique_ptr<QRhiBuffer> uniforms_;
    std::unique_ptr<QRhiShaderResourceBindings> bindings_;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
};
}  // namespace fugue::shell
