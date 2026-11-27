#include "shell/gpubackend.h"
#include "shell/mapwidget.h"
#include "shell/spriterenderer.h"
#include <QFile>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <cmath>
#include <QLabel>
#include <QTimer>
#include <QSurfaceFormat>
#include <QScreen>
#include <QStringList>
#include <QFontDatabase>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <rhi/qrhi.h>
#ifdef Q_OS_MACOS
#include <rhi/qrhi_platform.h>
#endif

// Explicit reference keeps the shader resource object in the static shell library.
static void initMapResources() { Q_INIT_RESOURCE(mapshaders); }

namespace fugue::shell {
namespace {
QShader shader(const char* path) {
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QShader::fromSerialized(f.readAll());
}
}  // namespace
#ifdef Q_OS_MACOS
bool metalAvailable() {
    QRhiMetalInitParams params;
    return QRhi::probe(QRhi::Metal, &params);
}
#endif
MapWidget::MapWidget(QWidget* parent) : QRhiWidget(parent) {
    // FUGUE_GPU_BACKEND picks the map's graphics API; start-up has settled it and Qt's own variable on one backend
    // (shell/gpubackend.h). Qt's variable alone does not move a QRhiWidget.
    const QByteArray backend = gpuApi();
    if (backend == "opengl") setApi(QRhiWidget::Api::OpenGL);
    else if (backend == "d3d11") setApi(QRhiWidget::Api::Direct3D11);
    else if (backend == "metal") setApi(QRhiWidget::Api::Metal);
    else if (backend == "vulkan") setApi(QRhiWidget::Api::Vulkan);
    initMapResources();
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    overlay_ = new QLabel(this);
    overlay_->setObjectName("mapPerformanceOverlay");
    overlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay_->setFocusPolicy(Qt::NoFocus);
    overlay_->setTextFormat(Qt::RichText);
    QFont metricsFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    metricsFont.setPointSizeF(9);
    overlay_->setFont(metricsFont);
    overlay_->setStyleSheet(
        "QLabel { color: #e8edf2; background-color: rgba(12, 16, 22, 220); "
        "border: 1px solid rgba(255, 255, 255, 35); border-radius: 4px; padding: 8px; }");
    overlay_->hide();
    auto* timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &MapWidget::refreshMetrics);
    timer->start();
    // Emitted once the top-level window has composed the frame and handed it on for display: a frame shown.
    connect(this, &QRhiWidget::frameSubmitted, this, [this] {
        metrics_->submitted(submittedTiming_);
        submittedTiming_ = {};
        // Re-pick a stationary cursor after natural submissions, using only the
        // immutable input frame installed by render(). No rendering is requested. Stock 516.1689 picks a still
        // pointer again as the map changes under it, a gliding atom frame by frame (tests/data/hover-world).
        if (hoverInside_ && hoverArmed_ && !hoverQueued_) {
            hoverQueued_ = true;
            QTimer::singleShot(0, this, [this] {
                hoverQueued_ = false;
                refreshHover();
            });
        }
    });
}
void MapWidget::setMetricsVisible(bool visible) {
    overlay_->setVisible(visible);
    refreshMetrics();
    overlay_->raise();
}
bool MapWidget::metricsVisible() const { return !overlay_->isHidden(); }
void MapWidget::refreshMetrics() {
    const auto s = metrics_->snapshot();  // consume windows even while the overlay is hidden
    static const bool traceFrames = std::getenv("FUGUE_FRAME_TRACE") != nullptr;
    const auto plan = spriteRenderer_ ? spriteRenderer_->takeStats() : SpriteRenderer::Stats{};
    if (traceFrames) {
        std::set<qint64> textures;
        for (const auto& sprite : sprites_) textures.insert(sprite.image.cacheKey());
        std::fprintf(stderr,
                     "[render-trace] view=%dx%d sprites=%lld textures=%zu shown/s=%.1f record=%.3fms "
                     "record-p95=%.3fms qt-submit=%.3fms upload=%.3fMB/s queue=%.3fms\n",
                     viewSize_.width(), viewSize_.height(), static_cast<long long>(sprites_.size()), textures.size(),
                     s.submittedPerSecond, s.recording.average, s.recording.p95, s.qtSubmitWait.average,
                     s.uploadMBPerSecond, s.queue.average);
        std::fprintf(
            stderr,
            "[render-plan] preparations=%llu replays=%llu prepare=%.3fms draws=%llu surfaces=%llu "
            "requested-surfaces=%llu plan-refusals=%llu passes=%llu surface-bytes=%llu work-pixels=%llu "
            "ignored-filters=%llu\n",
            static_cast<unsigned long long>(plan.preparations), static_cast<unsigned long long>(plan.replays),
            plan.preparations ? plan.prepareMs / plan.preparations : 0, static_cast<unsigned long long>(plan.draws),
            static_cast<unsigned long long>(plan.surfaces), static_cast<unsigned long long>(plan.requestedSurfaces),
            static_cast<unsigned long long>(plan.planRefusals), static_cast<unsigned long long>(plan.passes),
            static_cast<unsigned long long>(plan.surfaceBytes), static_cast<unsigned long long>(plan.workPixels),
            static_cast<unsigned long long>(plan.ignoredFilters));
        if (plan.composition.available)
            for (const auto& row : plan.composition.rows())
                std::fprintf(stderr, "[render-budget] %s=%llu/%llu warning=%d\n", row.label,
                             static_cast<unsigned long long>(row.value), static_cast<unsigned long long>(row.cap),
                             int(row.value > row.cap));
        if (plan.rejection.reason != PlanRejection::None)
            std::fprintf(stderr,
                         "[render-rejection] reason=%s observed=%llu limit=%llu pass=%d source=%dx%d "
                         "candidate-passes=%llu reachable-passes=%llu\n",
                         plan.rejection.name(), static_cast<unsigned long long>(plan.rejection.observed),
                         static_cast<unsigned long long>(plan.rejection.limit), plan.rejection.pass,
                         plan.sourceSize.width(), plan.sourceSize.height(),
                         static_cast<unsigned long long>(plan.candidatePasses),
                         static_cast<unsigned long long>(plan.reachablePasses));
    }
    if (!metricsVisible()) return;
    auto number = [](double value) { return QString::number(value, 'f', 1); };
    auto pair = [](const QString& left, const QString& value, const QString& right, const QString& other) {
        return QString("%1 %2  %3 %4").arg(left, -8).arg(value, 9).arg(right, -9).arg(other, 9);
    };
    auto row = [](const QString& label, const QString& average, const QString& p95) {
        return QString("%1 %2 %3").arg(label, -19).arg(average, 9).arg(p95, 10);
    };
    auto timing = [&](const QString& label, const MapMetrics::Timing& t) {
        return row(label, t.available ? number(t.average) : "--", t.available ? number(t.p95) : "--");
    };
    const QScreen* display = screen();
    const QString refresh = display && display->refreshRate() > 0 ? number(display->refreshRate()) : "--";
    const auto summary =
        QStringList{"PERFORMANCE",
                    pair("Target", number(s.targetFps) + " FPS", "Display", refresh + " Hz"),
                    pair("Map", number(s.mapsPerSecond) + "/s", "Shown", number(s.submittedPerSecond) + "/s"),
                    "",
                    row("TIME (ms)", "AVG", "P95"),
                    timing("Compose", s.compose),
                    timing("Update latency", s.latency),
                    timing("GUI queue", s.queue),
                    timing("Render wait", s.renderWait),
                    timing("Render to submit", s.submit),
                    timing("Command recording", s.recording),
                    timing("Qt submission wait", s.qtSubmitWait),
                    "",
                    pair("Replaced", number(s.overwrittenPerSecond) + "/s", "Pending", QString::number(s.pending)),
                    pair("Missed", number(s.missedPerSecond) + "/s", "Upload", number(s.uploadMBPerSecond) + " MB/s")}
            .join('\n')
            .toHtmlEscaped();
    overlay_->setText("<pre style=\"margin:0\">" + summary + "\n\n" + plan.composition.html() + "</pre>");
    placeMetrics();
}
void MapWidget::placeMetrics() {
    overlay_->adjustSize();
    overlay_->resize(std::min(overlay_->width(), std::max(0, width() - 16)),
                     std::min(overlay_->height(), std::max(0, height() - 16)));
    overlay_->move(8, 8);
}
void MapWidget::resizeEvent(QResizeEvent* event) {
    QRhiWidget::resizeEvent(event);
    placeMetrics();
    if (presentationChanged) presentationChanged();
}
QString MapWidget::createdBackend() const { return rhi() ? QString::fromLatin1(rhi()->backendName()) : QString(); }
MapWidget::~MapWidget() {
    // Retire queued input without calling a handler whose owner may also be
    // tearing down (e.g. a skin rebuild). The worker rejects the expired fence.
    inputSurface_.reset();
    presses_.clear();
    releaseResources();
}
void MapWidget::setPresentationSettings(MapPresentationSettings settings) {
    if (settings_ == settings) return;
    settings_ = settings;
    update();  // settled scenes need no new worker publication
    if (presentationChanged) presentationChanged();
}
MapPresentation MapWidget::presentation() const {
    const int tile = geometry_.tile > 0
                         ? geometry_.tile
                         : (!source_.isEmpty() && viewSize_.width() > 0 ? source_.width() / viewSize_.width() : 32);
    const QSize scene = source_.isEmpty() ? viewSize_ * tile : source_.size();
    return mapPresentation(size(), scene, tile, settings_);
}
QSize MapWidget::presentedViewSize() const { return presentation().destination.size().toSize(); }
QPointF MapWidget::sceneToWidget(QPointF scenePixel) const {
    const auto placement = presentation();
    return placement.destination.topLeft() + scenePixel * placement.scale;
}
QImage MapWidget::screenshot(QString* error) {
    if (error) error->clear();
    auto fail = [&](const QString& why) -> QImage {
        if (error) *error = why;
        return {};
    };
    // inputPlan_ is installed only after a complete composition and cleared on
    // resource/session replacement or rejected frames. Never read stale pixels.
    if (!inputPlan_ || !sceneTexture_ || !resourceRhi_ || viewSize_.isEmpty())
        return fail(QStringLiteral("no complete map frame to capture"));
    if (resourceRhi_->isRecordingFrame()) return fail(QStringLiteral("map frame recording is in progress"));
    QRhiCommandBuffer* cb = nullptr;
    if (resourceRhi_->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return fail(QStringLiteral("cannot begin screenshot readback"));
    QRhiReadbackResult result;
    bool completed = false;
    result.completed = [&] { completed = true; };
    auto* updates = resourceRhi_->nextResourceUpdateBatch();
    updates->readBackTexture(QRhiReadbackDescription(sceneTexture_.get()), &result);
    cb->resourceUpdate(updates);
    // Offscreen submission waits for readbacks: the result and callback can
    // safely live on this stack. Only an explicit screenshot pays this cost.
    const auto status = resourceRhi_->endOffscreenFrame();
    if (status != QRhi::FrameOpSuccess || !completed || result.format != QRhiTexture::RGBA8 ||
        result.pixelSize.isEmpty() ||
        result.data.size() != qint64(result.pixelSize.width()) * result.pixelSize.height() * 4)
        return fail(QStringLiteral("map screenshot readback failed"));
    // Scene rendering already corrects framebuffer Y orientation on OpenGL.
    // Copy before the readback storage dies; native pixels have DPR 1.
    return QImage(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(),
                  result.pixelSize.height(), QImage::Format_RGBA8888)
        .copy();
}
void MapWidget::setViewSize(QSize size) {
    if (size.width() <= 0 || size.height() <= 0) size = QSize(0, 0);
    if (size == viewSize_) return;
    viewSize_ = size;
    update();
}
void MapWidget::setMailbox(std::shared_ptr<MapMailbox> mailbox) {
    cancelGestures();
    leaveHover();
    inputSurface_ = std::make_shared<const uint8_t>(0);
    inputGeometry_ = {};
    inputView_ = {};
    inputSource_ = {};
    inputDestination_ = {};
    inputPlan_.reset();
    mailbox_ = std::move(mailbox);
    renderedSequence_ = 0;
    pendingTiming_ = submittedTiming_ = {};
    requestMapFrame();
}
void MapWidget::requestMapFrame() {
    if (!mailbox_ || !mailbox_->newerThan(renderedSequence_)) return;
    // Without vsync (Vulkan on Wayland, app/main.cpp) nothing holds redraws to the display: the worker publishes up to
    // its own rate (100 a second), and every frame past the screen's refresh is drawn and never seen, which with a
    // software device is CPU spent for nothing. Redraw at most once per refresh; the newest frame waits for the next.
    if (QSurfaceFormat::defaultFormat().swapInterval() != 0) {
        update();
        return;
    }
    const QScreen* display = screen();
    const double hz = display && display->refreshRate() > 0 ? display->refreshRate() : 60;
    const int64_t now = MapMetrics::nowNs();
    const int64_t wait = lastRedrawNs_ + int64_t(1e9 / hz) - now;
    if (wait <= 0) {
        lastRedrawNs_ = now;
        update();
    } else if (!redrawQueued_) {
        redrawQueued_ = true;
        QTimer::singleShot(std::chrono::milliseconds((wait + 999'999) / 1'000'000), Qt::PreciseTimer, this, [this] {
            redrawQueued_ = false;
            requestMapFrame();
        });
    }
}
void MapWidget::applyFrame(const QImage& terrain, const QRect& source, MapMetrics::FrameTiming timing) {
    if (image_.cacheKey() != terrain.cacheKey()) {
        image_ = terrain;
        imageDirty_ = true;
    }
    source_ = source;
    pendingTiming_ = timing;
}
void MapWidget::setInputHandler(QString control, bool defaultMap, std::function<void(const MapPointer&)> handler) {
    cancelGestures();
    leaveHover();
    inputSurface_ = std::make_shared<const uint8_t>(0);
    control_ = std::move(control);
    defaultMap_ = defaultMap;
    input_ = std::move(handler);
}
std::optional<MapPointer> MapWidget::pointerAt(QPointF position, QPoint global, Qt::KeyboardModifiers mods,
                                               Qt::MouseButton button) const {
    if (!inputPlan_) return {};
    auto p = mapPointerAtPresentation(position, inputDestination_, inputView_, inputSource_, inputGeometry_, control_,
                                      defaultMap_, {}, inputPlan_);
    if (!p) return {};
    p->surface = inputSurface_;
    p->global = global;
    p->click.button = button == Qt::LeftButton     ? "left"
                      : button == Qt::RightButton  ? "right"
                      : button == Qt::MiddleButton ? "middle"
                                                   : "";
    p->click.mods = {bool(mods & Qt::ControlModifier), bool(mods & Qt::ShiftModifier), bool(mods & Qt::AltModifier)};
    return p;
}
void MapWidget::refreshHover() {
    // Stock defers hover during a button gesture; Drag/Drop themselves update
    // server hover ownership. A popup must not manufacture Exit/Enter callbacks.
    if (!hoverInside_ || !input_ || !presses_.isEmpty() || QApplication::activePopupWidget()) return;
    const auto global = QCursor::pos();
    auto p = pointerAt(mapFromGlobal(global), global, QApplication::keyboardModifiers(), Qt::NoButton);
    if (!p) {
        leaveHover();
        return;
    }
    // Empty space inside the map, where nothing is drawn, is a pick naming no atom, with the control and screen-loc, as
    // over the letterbox: no turf without an icon is named (stock 516.1689, tests/data/hover-world extra void1).
    // Every pick goes to the session, which alone keeps what the pointer was last over (MapHover) and reports only
    // what changes it.
    p->kind = MapPointer::Hover;
    input_(*p);
}
void MapWidget::popupClosing() {
    closedPopupTimer_.invalidate();
    if (QApplication::mouseButtons() & Qt::RightButton) closedPopupTimer_.start();
}
void MapWidget::popupClosed() {
    // The pointer is picked again as soon as the menu closes (mapMenuClosed has stock's rule).
    if (!input_) return;
    const auto buttons = QApplication::mouseButtons();
    middleFromMenu_ = (buttons & Qt::MiddleButton) && !presses_.contains(int(Qt::MiddleButton));
    const auto global = QCursor::pos();
    const QPoint local = mapFromGlobal(global);
    // Every map hears of the menu closing; the one under the pointer reports.
    bool overMap = false;
    for (auto* w = QApplication::widgetAt(global); w && !overMap; w = w->parentWidget())
        overMap = dynamic_cast<MapWidget*>(w);
    const auto closed = mapMenuClosed(buttons & Qt::LeftButton, buttons & Qt::RightButton,
                                      buttons & Qt::MiddleButton, overMap);
    if (closed == MapMenuClosed::Nothing || !isVisible() || !rect().contains(local)) return;
    auto p = pointerAt(local, global, QApplication::keyboardModifiers(),
                       closed == MapMenuClosed::PickRight    ? Qt::RightButton
                       : closed == MapMenuClosed::PickMiddle ? Qt::MiddleButton
                                                             : Qt::NoButton);
    if (!p) {
        // Over this map, outside its picture: what a move there would report.
        sendUnpicked(MapPointer::Leave);
        return;
    }
    hoverInside_ = true;
    p->kind = MapPointer::Hover;
    input_(*p);
}
void MapWidget::leaveHover() {
    if (!presses_.isEmpty() || QApplication::activePopupWidget()) return;
    sendUnpicked(MapPointer::Leave);
}
void MapWidget::sendUnpicked(MapPointer::Kind kind) {
    if (!input_) return;
    MapPointer p;
    p.kind = kind;
    p.session = inputGeometry_.session;
    p.z = inputGeometry_.z;
    p.surface = inputSurface_;
    p.fromDefaultMap = defaultMap_;
    p.click.over = p.click.loc = proto::enc::nullRef();
    input_(p);
}
bool MapWidget::event(QEvent* e) {
    if (e->type() == QEvent::FocusOut || e->type() == QEvent::WindowDeactivate || e->type() == QEvent::UngrabMouse)
        cancelGestures();
    return QRhiWidget::event(e);
}
void MapWidget::cancelGestures() {
    const auto presses = std::exchange(presses_, {});
    if (!presses.isEmpty()) hoverArmed_ = false;
    for (const auto& press : presses)
        if (input_) {
            auto p = *press.source;
            p.kind = MapPointer::Up;
            p.source = press.source;
            p.outside = true;
            p.click.over = p.click.loc = proto::enc::nullRef();
            p.screen = false;
            p.haveAtomKey = false;
            p.targetIdentity = 0;
            p.visHit.reset();
            p.imageId = 0;
            p.imageIdentity = 0;
            input_(p);  // balance Down, never synthesize Click/Drop on cancellation
        }
}
std::optional<MapPointer> MapWidget::gesturePointer(QMouseEvent* e, Qt::MouseButton button,
                                                    const MapPointer& source) const {
    if (source.session != inputGeometry_.session || source.z != inputGeometry_.z) return {};
    if (rect().contains(e->position().toPoint()))
        return pointerAt(e->position(), e->globalPosition().toPoint(), e->modifiers(), button);
    auto p = source;
    p.outside = true;
    p.click.over = p.click.loc = proto::enc::nullRef();
    p.screen = false;
    p.haveAtomKey = false;
    p.targetIdentity = 0;
    p.visHit.reset();
    p.imageId = 0;
    p.imageIdentity = 0;
    p.global = e->globalPosition().toPoint();
    p.click.locStr.clear();
    p.click.control.clear();
    p.click.mods = {bool(e->modifiers() & Qt::ControlModifier), bool(e->modifiers() & Qt::ShiftModifier),
                    bool(e->modifiers() & Qt::AltModifier)};
    for (auto* w = QApplication::widgetAt(p.global); w && w->window() == window(); w = w->parentWidget()) {
        const auto control = w->property("fugueControl").toString();
        if (!control.isEmpty()) {
            p.click.control = control.toStdString();
            // Another map is a map, not just a control: what is under the pointer there is the target, and its empty
            // area is named by the map's full id as the location (stock 516.1689: a drag released on a second map's
            // empty area reaches MouseUp and MouseDrop with that map as over_control).
            if (auto* other = dynamic_cast<MapWidget*>(w); other && other != this) {
                if (auto hit = other->pointerAt(other->mapFromGlobal(p.global), p.global, e->modifiers(), button);
                    hit && !hit->renderedScene.isEmpty())
                    return hit;
                p.click.locStr = p.click.control;  // a map with no picture: all of it is its empty area
            }
            break;
        }
    }
    return p;
}
void MapWidget::moveGestures(QMouseEvent* e) {
    for (auto it = presses_.begin(); it != presses_.end();) {
        auto& press = it.value();
        if (!(e->buttons() & Qt::MouseButton(it.key()))) {
            ++it;
            continue;
        }
        auto p = gesturePointer(e, Qt::MouseButton(it.key()), *press.source);
        if (!p) {  // the scene changed under the button: no drag steps, the release ends it (mouseReleaseEvent)
            ++it;
            continue;
        }
        // Straight from the map to outside every window, the step still names the last cell dragged over, with the
        // press's pixel in it (stock 516.1689: "screen-loc=9:16,12:16").
        if (p->outside && p->click.control.empty() && !press.last.outside &&
            press.last.click.control == press.source->click.control) {
            p->click.screenX = press.last.click.screenX;
            p->click.screenY = press.last.click.screenY;
            p->dragCell = true;
        }
        // Stock reports target transitions, not a pixel-distance threshold or
        // every icon-coordinate change. A return to the source remains a drag.
        if (!sameMapTarget(press.last, *p)) {
            press.dragged = true;
            press.last = *p;
            p->kind = MapPointer::Drag;
            p->source = press.source;
            if (input_) input_(*p);
        }
        ++it;
    }
}
void MapWidget::mouseMoveEvent(QMouseEvent* e) {
    moveGestures(e);
    hoverInside_ = rect().contains(e->position().toPoint());
    hoverArmed_ = hoverInside_;
    if (hoverInside_)
        refreshHover();
    else
        leaveHover();
    e->accept();
}
void MapWidget::enterEvent(QEnterEvent* e) {
    QRhiWidget::enterEvent(e);
    hoverInside_ = hoverArmed_ = true;
    refreshHover();
}
void MapWidget::leaveEvent(QEvent* e) {
    hoverInside_ = false;
    // Stock 516.1689 (tests/data/hover-world extra m2jump, st1, m2vis): a control the pointer leaves says so after the
    // control it went to has reported. Straight from one map onto another, the second names its atom and then the
    // leave of the first clears the record the session's maps share: the server exits that atom at once and enters it
    // again at the next move. Qt delivers this leave before the other map's enter and move, hence the delay.
    QTimer::singleShot(0, this, [this] {
        if (!hoverInside_) leaveHover();
    });
    QRhiWidget::leaveEvent(e);
}
void MapWidget::hideEvent(QHideEvent* e) {
    cancelGestures();
    hoverInside_ = false;
    leaveHover();
    QRhiWidget::hideEvent(e);
}
void MapWidget::mousePressEvent(QMouseEvent* e) {
    if (!input_ ||
        (e->button() != Qt::LeftButton && e->button() != Qt::RightButton && e->button() != Qt::MiddleButton)) {
        QRhiWidget::mousePressEvent(e);
        return;
    }
    // A right press while the middle button is held does nothing, nor does its release: no MouseDown, no menu (stock
    // 516.1689, tests/data/hover-world round3 mmidbefore).
    if (e->button() == Qt::RightButton && presses_.contains(int(Qt::MiddleButton))) {
        e->accept();
        return;
    }
    if (e->button() == Qt::MiddleButton) middleFromMenu_ = false;
    setFocus(Qt::MouseFocusReason);
    if (auto p = pointerAt(e->position(), e->globalPosition().toPoint(), e->modifiers(), e->button());
        p && (!p->backgroundHidden || (e->button() == Qt::RightButton && p->insideScene))) {
        // A menu-only footprint has no alpha hit. The worker still rejects its
        // Down/Up and popup-disabled Click; popup candidates validate separately.
        p->kind = MapPointer::Down;
        // The popup's own mouse grab delivers the press that closed it right after it hides.
        p->closedPopup =
            e->button() == Qt::RightButton && closedPopupTimer_.isValid() && closedPopupTimer_.elapsed() < 250;
        closedPopupTimer_.invalidate();
        presses_[int(e->button())] = {std::make_shared<const MapPointer>(*p), *p};
        input_(*p);
    }
    e->accept();
}
void MapWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    mousePressEvent(e);
    if (auto it = presses_.find(int(e->button())); it != presses_.end()) it->dbl = true;
}
void MapWidget::mouseReleaseEvent(QMouseEvent* e) {
    const int button = int(e->button());
    // The middle press the verb menu took is released as if pressed where it is released.
    if (input_ && e->button() == Qt::MiddleButton && !presses_.contains(button) && std::exchange(middleFromMenu_, false))
        if (auto p = pointerAt(e->position(), e->globalPosition().toPoint(), e->modifiers(), e->button());
            p && !p->backgroundHidden) {
            p->kind = MapPointer::Down;
            presses_[button] = {std::make_shared<const MapPointer>(*p), *p};
        }
    if (!input_ || !presses_.contains(button)) {
        QRhiWidget::mouseReleaseEvent(e);
        return;
    }
    const auto press = presses_.take(button);
    // Stock 516.1689 forgets what the pointer was over at any release, click or drop: the next move reports the atom
    // under it again, even the same one at the same pixel, and leaving before any move reports nothing.
    // The session forgets at the Up it gets, or at a bare Released when the release names nothing.
    hoverArmed_ = false;
    bool upSent = false;
    if (auto p = gesturePointer(e, e->button(), *press.source)) {
        upSent = true;
        p->source = press.source;
        p->kind = MapPointer::Up;
        input_(*p);
        if (press.dragged || !sameMapTarget(*press.source, *p)) {
            p->kind = MapPointer::Drop;
            input_(*p);
        } else if (!p->outside) {
            p->kind = press.dbl ? MapPointer::DoubleClick : MapPointer::Click;
            input_(*p);
        }
    } else if (press.source->session == inputGeometry_.session && rect().contains(e->position().toPoint())) {
        // The map moved to another z under the held button. The release is then an ordinary one on what is under the
        // pointer now, and a click on it (stock 516.1689: pressed on an obj whose MouseDown moves the mob to another z,
        // the release reaches MouseUp and Click of the turf there). Nothing of the old scene is named.
        if (auto p = pointerAt(e->position(), e->globalPosition().toPoint(), e->modifiers(), e->button());
            p && !p->backgroundHidden) {
            upSent = true;
            p->kind = MapPointer::Up;
            input_(*p);
            p->kind = MapPointer::Click;
            input_(*p);
        }
    }
    if (!upSent) sendUnpicked(MapPointer::Released);
    e->accept();
}
void MapWidget::wheelEvent(QWheelEvent* e) {
    Qt::MouseButton button = e->buttons() & Qt::LeftButton     ? Qt::LeftButton
                             : e->buttons() & Qt::RightButton  ? Qt::RightButton
                             : e->buttons() & Qt::MiddleButton ? Qt::MiddleButton
                                                               : Qt::NoButton;
    auto p = pointerAt(e->position(), e->globalPosition().toPoint(), e->modifiers(), button);
    if (!input_ || !p) {
        e->ignore();
        return;
    }
    // Retain sub-notch deltas from high-resolution wheels. Pixel-only touchpads
    // use 40 logical pixels per notch; do not mix angle and pixel deltas.
    wheelRemainder_ += !e->angleDelta().isNull() ? QPointF(e->angleDelta()) / 120.0 : QPointF(e->pixelDelta()) / 40.0;
    int x = std::clamp(int(std::trunc(wheelRemainder_.x())), -64, 64);
    int y = std::clamp(int(std::trunc(wheelRemainder_.y())), -64, 64);
    wheelRemainder_ -= QPointF(x, y);
    p->kind = MapPointer::Wheel;
    while (x || y) {
        // One report per notch, with Windows' own delta of 120 (stock 516.1689: MouseWheel gets delta_y 120).
        const int stepX = x ? (x > 0 ? 1 : -1) : 0, stepY = y ? (y > 0 ? 1 : -1) : 0;
        p->dx = 120.0f * stepX;
        p->dy = 120.0f * stepY;
        input_(*p);
        x -= stepX;
        y -= stepY;
    }
    if (e->phase() == Qt::ScrollEnd) wheelRemainder_ = {};
    e->accept();
}
void MapWidget::contextMenuEvent(QContextMenuEvent* e) {
    e->accept();
}  // worker handles the rendered right-button Click on release
void MapWidget::releaseResources() {
    resourceRhi_ = nullptr;
    spriteRenderer_.reset();
    inputPlan_.reset();
    inputDestination_ = {};
    presentPipeline_.reset();
    for (auto& binding : presentBindings_) binding.reset();
    presentUniforms_.reset();
    linearSampler_.reset();
    sceneTarget_.reset();
    scenePass_.reset();
    sceneTexture_.reset();
    pipeline_.reset();
    bindings_.reset();
    uniforms_.reset();
    sampler_.reset();
    texture_.reset();
    imageDirty_ = true;
}
void MapWidget::initialize(QRhiCommandBuffer*) {
    // QRhiWidget also calls initialize on output resize/DPR changes. Only its
    // presentation pass depends on that output; keep native scene/icon resources.
    if (resourceRhi_ == rhi() && presentPipeline_) {
        presentPipeline_->setSampleCount(sampleCount());
        presentPipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (presentPipeline_->create()) return;
    }
    releaseResources();
    // The backend and device actually created (FUGUE_GPU_BACKEND only asks).
    if (std::getenv("FUGUE_FRAME_TRACE") && rhi())
        std::fprintf(stderr, "[render-trace] backend=%s device=%s\n", rhi()->backendName(),
                     rhi()->driverInfo().deviceName.constData());
    if (!ensureScene(source_.isEmpty() ? QSize(1, 1) : source_.size())) return;
    uniforms_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    if (!uniforms_->create()) {
        releaseResources();
        return;
    }
    texture_.reset(rhi()->newTexture(QRhiTexture::RGBA8, image_.isNull() ? QSize(1, 1) : image_.size()));
    sampler_.reset(rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!texture_->create() || !sampler_->create()) {
        releaseResources();
        return;
    }
    bindings_.reset(rhi()->newShaderResourceBindings());
    bindings_->setBindings(
        {QRhiShaderResourceBinding::uniformBuffer(
             0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms_.get()),
         QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture_.get(),
                                                   sampler_.get())});
    if (!bindings_->create()) {
        releaseResources();
        return;
    }
    pipeline_.reset(rhi()->newGraphicsPipeline());
    pipeline_->setShaderStages({{QRhiShaderStage::Vertex, shader(":/map/shaders/grid.vert.qsb")},
                                {QRhiShaderStage::Fragment, shader(":/map/shaders/grid.frag.qsb")}});
    pipeline_->setVertexInputLayout(QRhiVertexInputLayout{});
    pipeline_->setShaderResourceBindings(bindings_.get());
    pipeline_->setSampleCount(1);
    pipeline_->setRenderPassDescriptor(scenePass_.get());
    if (!pipeline_->create()) {
        releaseResources();
        return;
    }
    spriteRenderer_ = std::make_unique<SpriteRenderer>();
    if (!spriteRenderer_->initialize(rhi(), scenePass_.get(), 1)) spriteRenderer_.reset();
    presentUniforms_.reset(rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    linearSampler_.reset(rhi()->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!presentUniforms_->create() || !linearSampler_->create()) {
        releaseResources();
        return;
    }
    for (int i = 0; i < 2; ++i) {
        presentBindings_[i].reset(rhi()->newShaderResourceBindings());
        presentBindings_[i]->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(
                 0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                 presentUniforms_.get()),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, sceneTexture_.get(),
                                                       i ? linearSampler_.get() : sampler_.get())});
        if (!presentBindings_[i]->create()) {
            releaseResources();
            return;
        }
    }
    presentPipeline_.reset(rhi()->newGraphicsPipeline());
    presentPipeline_->setShaderStages({{QRhiShaderStage::Vertex, shader(":/map/shaders/grid.vert.qsb")},
                                       {QRhiShaderStage::Fragment, shader(":/map/shaders/grid.frag.qsb")}});
    presentPipeline_->setVertexInputLayout(QRhiVertexInputLayout{});
    presentPipeline_->setShaderResourceBindings(presentBindings_[0].get());
    presentPipeline_->setSampleCount(sampleCount());
    presentPipeline_->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    if (!presentPipeline_->create())
        releaseResources();
    else
        resourceRhi_ = rhi();
}
bool MapWidget::ensureScene(QSize size) {
    const int limit = rhi()->resourceLimit(QRhi::TextureSizeMax);
    if (size.isEmpty() || size.width() > limit || size.height() > limit ||
        uint64_t(size.width()) * size.height() > 128ull * 1024 * 1024)
        return false;
    if (sceneTexture_ && sceneTexture_->pixelSize() == size && sceneTarget_) return true;
    sceneTarget_.reset();
    if (!sceneTexture_)
        sceneTexture_.reset(rhi()->newTexture(QRhiTexture::RGBA8, size, 1,
                                              QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    else
        sceneTexture_->setPixelSize(size);
    if (!sceneTexture_->create()) return false;
    sceneTarget_.reset(rhi()->newTextureRenderTarget({sceneTexture_.get()}));
    if (!scenePass_) scenePass_.reset(sceneTarget_->newCompatibleRenderPassDescriptor());
    sceneTarget_->setRenderPassDescriptor(scenePass_.get());
    if (!sceneTarget_->create()) {
        sceneTarget_.reset();
        return false;
    }
    for (auto& binding : presentBindings_)
        if (binding && !binding->create()) return false;
    return true;
}
void MapWidget::render(QRhiCommandBuffer* cb) {
    // Even if the GUI notification has not run yet, use the newest complete frame
    // available now, not the one that originally requested this redraw.
    if (mailbox_) {
        if (auto latest = mailbox_->forRender(renderedSequence_)) {
            renderedSequence_ = latest->sequence;
            applyFrame(latest->terrain, latest->source, latest->timing);
            sprites_ = latest->sprites;
            // Another session ends every held button. Another z of the same session keeps them: their release is an
            // ordinary one on the new scene (mouseReleaseEvent), and they report no drag in between.
            if (geometry_.session != latest->geometry.session) cancelGestures();
            if (geometry_.session != latest->geometry.session || geometry_.z != latest->geometry.z)
                wheelRemainder_ = {};
            geometry_ = latest->geometry;
        }
    }
    const int64_t renderStart = MapMetrics::nowNs();
    const QSize wanted = image_.isNull() ? QSize(1, 1) : image_.size();
    bool resourcesReady = pipeline_ && presentPipeline_ && !viewSize_.isEmpty() && !source_.isEmpty();
    if (resourcesReady && texture_->pixelSize() != wanted) {
        texture_->setPixelSize(wanted);
        resourcesReady = texture_->create() && bindings_->create();
        imageDirty_ = true;
    }
    resourcesReady = resourcesReady && ensureScene(source_.size());
    const auto placement = presentation();
    resourcesReady = resourcesReady && !placement.destination.isEmpty();
    const QSize pixels = renderTarget()->pixelSize();
    QRhiResourceUpdateBatch* updates = nullptr;
    if (resourcesReady) {
        const float textureW = float(std::max(1, image_.width()));
        const float textureH = float(std::max(1, image_.height()));
        const float viewData[] = {0,
                                  0,
                                  1,
                                  1,
                                  source_.x() / textureW,
                                  source_.y() / textureH,
                                  source_.width() / textureW,
                                  source_.height() / textureH,
                                  rhi()->isYUpInFramebuffer() == rhi()->isYUpInNDC() ? 1.0f : 0.0f, // as SpriteRenderer
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0,
                                  0};
        const auto& dest = placement.destination;
        const float presentData[] = {
            float(dest.x() / width()),
            float(dest.y() / height()),
            float(dest.width() / width()),
            float(dest.height() / height()),
            0,
            0,
            1,
            1,
            rhi()->isYUpInNDC() ? 0.0f : 1.0f, // Vulkan's clip space points down
            0,
            0,
            0,
            placement.sharpen ? float(dest.width() * pixels.width() / width() / source_.width()) : 0,
            placement.sharpen ? float(dest.height() * pixels.height() / height() / source_.height()) : 0,
            float(source_.width()),
            float(source_.height())};
        updates = rhi()->nextResourceUpdateBatch();
        updates->updateDynamicBuffer(uniforms_.get(), 0, sizeof(viewData), viewData);
        updates->updateDynamicBuffer(presentUniforms_.get(), 0, sizeof(presentData), presentData);
        if (imageDirty_) {
            QImage upload = image_;
            if (upload.isNull()) {
                upload = QImage(1, 1, QImage::Format_RGBA8888);
                upload.fill(Qt::black);
            }
            updates->uploadTexture(texture_.get(), upload);
            metrics_->uploaded(uint64_t(upload.sizeInBytes()));
            imageDirty_ = false;
        }
        if (spriteRenderer_)
            metrics_->uploaded(spriteRenderer_->prepare(
                updates, sprites_, source_, mapWorldClip(placement, size(), source_.size(), geometry_.hud, geometry_.tile),
                mapWorldView(source_.size(), geometry_.hud, geometry_.tile)));
    }
    if (spriteRenderer_ && updates) {
        spriteRenderer_->renderOffscreen(cb, updates);
        updates = nullptr;
    }
    // Unsupported or failed-resource compositions are never partially presented.
    const bool complete = resourcesReady && spriteRenderer_ && spriteRenderer_->ready();
    if (complete) {
        cb->beginPass(sceneTarget_.get(), Qt::black, {1.0f, 0}, updates);
        updates = nullptr;
        cb->setGraphicsPipeline(pipeline_.get());
        const QRhiViewport sceneViewport(0, 0, float(source_.width()), float(source_.height()));
        cb->setViewport(sceneViewport);
        cb->setShaderResources(bindings_.get());
        cb->draw(3);
        spriteRenderer_->draw(cb, sceneViewport);
        cb->endPass();
    }
    cb->beginPass(renderTarget(), Qt::black, {1.0f, 0}, updates);
    if (complete) {
        cb->setGraphicsPipeline(presentPipeline_.get());
        cb->setViewport(QRhiViewport(0, 0, float(pixels.width()), float(pixels.height())));
        cb->setShaderResources(presentBindings_[placement.linear ? 1 : 0].get());
        cb->draw(3);
    }
    cb->endPass();
    inputDestination_ = complete ? placement.destination : QRectF();
    inputPlan_ = complete ? spriteRenderer_->preparedPlan() : nullptr;
    if (complete) {
        inputGeometry_ = geometry_;
        inputSource_ = source_;
        // A server view notification can precede its worker frame. Input follows
        // the scene actually composed, not that pending requested tile count.
        inputView_ =
            geometry_.tile > 0 ? QSize(source_.width() / geometry_.tile, source_.height() / geometry_.tile) : viewSize_;
        if (presentationChanged) presentationChanged();
        submittedTiming_ = pendingTiming_;
        submittedTiming_.render = renderStart;
        pendingTiming_ = {};
        // Final timestamp before returning to Qt. The following interval includes
        // Qt's compositor/submission work, not just a guaranteed VSync wait.
        submittedTiming_.recorded = MapMetrics::nowNs();
    }
}
}  // namespace fugue::shell
