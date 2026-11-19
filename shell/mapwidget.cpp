#include "shell/mapwidget.h"
#include <QApplication>
#include <QCursor>
#include <QPainter>
#include <QRegion>
#include <QWindow>
#include <QFontDatabase>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace fugue::shell {

MapWidget::MapWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setObjectName("map");
    setMouseTracking(true);  // button-less motion too, for the status bar's hover name (updateHover)
}

void MapWidget::setFrame(const QImage& img, HitPtr hit) {
    snapshot_ = {};
    if (externalView_) externalView_->hide();
    img_ = img;
    frameSize_ = img.size();
    hit_ = std::move(hit);
    shown_++;
    // A new frame can move a different atom under a still pointer, so the hover is re-resolved on every frame.
    if (hovering_ && updateHover() && !QGuiApplication::mouseButtons())
        emitHover(hoverPos_, QGuiApplication::keyboardModifiers(), false);
    update();
}

void MapWidget::setExternalView(QWidget* view, QWindow* window) {
    externalView_ = view;
    externalWindow_ = window;
    if (view) {
        view->setParent(this);
        view->setGeometry(rect());
        clipExternalView();
    }
}

void MapWidget::clipExternalView() {
    QWidget* parent = parentWidget();
    if (!externalWindow_ || !parent) return;
    QRegion shown(rect());
    bool above = false;
    for (QObject* o : parent->children()) {  // bottom of the stack first
        if (o == this) {
            above = true;
            continue;
        }
        auto* w = qobject_cast<QWidget*>(o);
        if (above && w && !w->isWindow() && !w->isHidden()) shown -= w->geometry().translated(-pos());
    }
    // An empty mask means no mask, so a fully covered presenter keeps only a pixel outside itself.
    externalWindow_->setMask(shown == QRegion(rect()) ? QRegion() : shown.isEmpty() ? QRegion(-1, -1, 1, 1) : shown);
}

void MapWidget::setExternalFrame(QSize size, HitPtr hit, std::function<QImage()> snapshot) {
    img_ = {};
    frameSize_ = size;
    snapshot_ = std::move(snapshot);
    hit_ = std::move(hit);
    ++shown_;
    if (externalView_) externalView_->show();
    if (hovering_ && updateHover() && !QGuiApplication::mouseButtons())
        emitHover(hoverPos_, QGuiApplication::keyboardModifiers(), false);
}

void MapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (externalView_) {
        externalView_->setGeometry(rect());
        clipExternalView();
    }
    emit presentationChanged();
}

void MapWidget::setMapScale(double zoom, int iconSize, bool letterbox, const std::string& mode) {
    zoom_ = zoom > 0 ? zoom : 0;
    iconSize_ = zoom_ > 0 ? 0 : std::max(0, iconSize);
    letterbox_ = letterbox;
    mode_ = mode.empty() ? "normal" : mode;
    update();
    emit presentationChanged();
}

// Scaling follows stock DreamSeeker 516.
double MapWidget::scaleF(QSize sourceSize) const {
    if (sourceSize.isEmpty()) sourceSize = frameSize_;
    if (sourceSize.isEmpty()) return 1;
    if (zoom_ > 0) return zoom_;
    if (iconSize_ > 0) return double(iconSize_) / tile_;
    double sx = double(width()) / sourceSize.width(), sy = double(height()) / sourceSize.height();
    return letterbox_ ? std::min(sx, sy) : std::max(sx, sy);
}

// Centred on both axes, whether smaller (letterboxed) or larger (centre-cropped) than the control.
QRect MapWidget::imageRect(QSize sourceSize) const {
    if (sourceSize.isEmpty()) sourceSize = frameSize_;
    if (sourceSize.isEmpty()) return {};
    double s = scaleF(sourceSize);
    int w = std::max(1, int(std::lround(sourceSize.width() * s))),
        h = std::max(1, int(std::lround(sourceSize.height() * s)));
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void MapWidget::setPerformanceText(const QString& text) {
    if (performanceText_ == text) return;
    performanceText_ = text;
    update();
    emit presentationChanged();
}

bool MapWidget::smoothScaling(QSize sourceSize) const {
    double s = scaleF(sourceSize);
    bool integral = std::fabs(s - std::round(s)) < 1e-9;
    return mode_ == "blur" || (mode_ != "distort" && !integral);
}

void MapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!img_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling());
        p.drawImage(imageRect(), img_);
    }
    paintOverlay(p);
}

QImage MapWidget::overlayImage() const {
    if (performanceText_.isEmpty() || size().isEmpty()) return {};
    QImage result(size() * devicePixelRatioF(), QImage::Format_RGBA8888_Premultiplied);
    result.setDevicePixelRatio(devicePixelRatioF());
    result.fill(Qt::transparent);
    QPainter p(&result);
    paintOverlay(p);
    return result;
}

QImage MapWidget::viewSnapshot() const {
    if (size().isEmpty()) return {};
    QImage result(size() * devicePixelRatioF(), QImage::Format_RGBA8888_Premultiplied);
    result.setDevicePixelRatio(devicePixelRatioF());
    result.fill(Qt::black);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling());
    p.drawImage(imageRect(), frame());
    paintOverlay(p);
    return result;
}

void MapWidget::paintOverlay(QPainter& p) const {
    if (!performanceText_.isEmpty() && width() > 24 && height() > 24) {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(9);
        p.setFont(font);
        QRect area = rect().adjusted(12, 12, -12, -12);
        constexpr int flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
        QRect text = p.boundingRect(area, flags, performanceText_).intersected(area);
        p.fillRect(text.adjusted(-6, -6, 6, 6), QColor(0, 0, 0, 200));
        p.setPen(Qt::white);
        p.drawText(area, flags, performanceText_);
    }
}

bool MapWidget::toImage(const QPoint& pos, int& ix, int& iy, bool extrapolate) const {
    if (frameSize_.isEmpty()) return false;
    QRect r = imageRect();
    if (r.width() <= 0 || r.height() <= 0) return false;
    if (!extrapolate && !r.contains(pos)) return false;
    int x = pos.x() - r.x(), y = pos.y() - r.y();
    ix = int(std::floor(double(x) * frameSize_.width() / r.width()));
    iy = int(std::floor(double(y) * frameSize_.height() / r.height()));
    if (!extrapolate) {
        ix = std::clamp(ix, 0, frameSize_.width() - 1);
        iy = std::clamp(iy, 0, frameSize_.height() - 1);
    }
    return true;
}

static int x11Button(Qt::MouseButton b) {
    return b == Qt::LeftButton ? 1 : b == Qt::MiddleButton ? 2 : b == Qt::RightButton ? 3 : 0;
}

// While a button is held this control keeps the pointer (Qt's implicit grab), but stock names what is under it, by window
// stacking: a map control (its scene, image pixel and id), another control or window (fugueControl), or nothing.
// Qt on Wayland reports every top-level window at 0,0, so widgetAt there follows creation order, not stacking: only this
// control's own window is looked at (whatever lies on top of the map in it), and another top-level window over it is
// not seen.
std::optional<bool> MapWidget::positionsKnown_;

MapWidget::Target MapWidget::targetAt(const QPoint& globalPos) const { return targetAtGlobal(globalPos, window()); }

// top: the window whose coordinates the global position is in, where the platform does not say where windows are.
MapWidget::Target MapWidget::targetAtGlobal(const QPoint& globalPos, const QWidget* top) {
    Target t;
    const QWidget* w = nullptr;
    if (positionsKnown_.value_or(!QGuiApplication::platformName().startsWith("wayland"))) {
        w = QApplication::widgetAt(globalPos);
    } else if (top) {
        const QPoint p = top->mapFromGlobal(globalPos);
        if (top->rect().contains(p)) w = top->childAt(p) ? top->childAt(p) : top;
    }
    for (; w; w = w->parentWidget()) {
        if (auto* m = qobject_cast<const MapWidget*>(w)) {
            t.map = m;
            return t;
        }
        QVariant id = w->property("fugueControl");
        if (id.isValid()) {
            t.control = id.toString().toStdString();
            return t;
        }
    }
    return t;
}

void MapWidget::emitPointer(MapPointer::Kind k, QMouseEvent* e, int button) {
    const QPoint g = e->globalPosition().toPoint();
    MapPointer mp;
    mp.kind = k;
    if (k == MapPointer::Press) {
        mp.scene = hit_;
        mp.control = controlId_;
        if (!toImage(e->position().toPoint(), mp.ev.imgX, mp.ev.imgY, false)) return;
    } else if (Target t = targetAt(g); t.map) {
        mp.scene = t.map->hit_;
        mp.control = t.map->controlId_;
        // Over the letterbox the pixel is extrapolated: nothing is named there (render::MouseChain). A map with no scene
        // yet is all empty area, with no cell.
        if (!t.map->hit_ || !t.map->toImage(t.map->mapFromGlobal(g), mp.ev.imgX, mp.ev.imgY, /*extrapolate=*/true))
            mp.scene = {};
    } else {
        mp.offMap = true;
        mp.control = t.control;
    }
    mp.ev.winX = g.x();  // screen pixels: one frame for a press on one control and the motion over another
    mp.ev.winY = g.y();
    mp.ev.button = button;
    mp.ev.mods.ctrl = e->modifiers() & Qt::ControlModifier;
    mp.ev.mods.shift = e->modifiers() & Qt::ShiftModifier;
    mp.ev.mods.alt = e->modifiers() & Qt::AltModifier;
    mp.ev.tMs = qint64(e->timestamp());  // Qt's event clock (ms); only relative timing matters
    emit pointer(mp);
}

void MapWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    int b = x11Button(e->button());
    if (b == 0 || heldButton_) return;  // wheel and extra buttons; a second button while one is held is ignored
    if (b == 3 && (consumedPress_ || !rightClickToWire())) {
        consumedPress_ = false;
        // Stock DreamSeeker answers the right button locally unless the map declares right-click = true or the world
        // turned client.show_popup_menus off; what it does is decided at the release (mouseReleaseEvent).
        localPress_ = true;
        return;
    }
    int ix, iy;
    if (!toImage(e->position().toPoint(), ix, iy, false)) return;  // on the letterbox
    heldButton_ = b;
    emitPointer(MapPointer::Press, e, b);
}

// The status bar names the topmost atom under the pointer, using the same render::hitAt as a click, so the bar and a
// click always agree. Stock DreamSeeker skips mouse_opacity 0 atoms and shows nothing over a nameless atom, the
// letterbox, or once the pointer leaves the map. This differs from stock, which keeps a blank bar when the pointer
// re-enters onto the atom it last named; this names it at once.
bool MapWidget::updateHover() {
    QString name;
    HoverKey key;
    int ix, iy;
    if (hovering_ && hit_ && toImage(hoverPos_, ix, iy, /*clamp=*/false)) {
        fugue::render::Hit h;
        // HitAtom::name is already plain: the scene strips the text-macro marker when it fills it (render/scene.cpp).
        if (fugue::render::hitAt(*hit_, ix, iy, h)) {
            name = QString::fromStdString(h.name);
            key = HoverKey{true, h.screen, h.kind, h.ref, h.worldX, h.worldY};
        }
    }
    bool changed = key.hit != hoverKey_.hit || key.screen != hoverKey_.screen || key.kind != hoverKey_.kind ||
                   key.ref != hoverKey_.ref || key.x != hoverKey_.x || key.y != hoverKey_.y;
    hoverKey_ = key;
    if (name != hoverName_) {
        hoverName_ = name;
        emit hoverChanged(name);
    }
    return changed;
}

// The image pixel under pos, extrapolated past the image over the letterbox.
bool MapWidget::unclampedImage(const QPoint& pos, int& ix, int& iy) const {
    if (frameSize_.isEmpty()) return false;
    QRect r = imageRect();
    if (r.width() <= 0 || r.height() <= 0) return false;
    ix = int(std::floor(double(pos.x() - r.x()) * frameSize_.width() / r.width()));
    iy = int(std::floor(double(pos.y() - r.y()) * frameSize_.height() / r.height()));
    return true;
}

// Nothing can report a hover unless the world declares a client hover proc or an atom on this map declares one. The
// post after the last such scene still goes, so the report that ends a hover is not lost.
bool MapWidget::wantsHover() const { return hit_ && (hoverLive_ || hit_->clientHover || hit_->anyHover); }

uint64_t MapWidget::leaves_ = 0;

// A hover report for the session thread (render::HoverTracker decides what reaches the wire). Motion to the image pixel
// last posted is dropped here, unless a map's leave was posted since (it ends the hover the step would continue).
void MapWidget::emitHover(const QPoint& pos, Qt::KeyboardModifiers mods, bool motion, int button) {
    MapPointer mp;
    mp.kind = MapPointer::Hover;
    mp.scene = hit_;
    mp.control = controlId_;
    mp.motion = motion;
    if (!wantsHover() || !unclampedImage(pos, mp.ev.imgX, mp.ev.imgY)) return;
    if (motion && hoverSent_ && hoverLeaves_ == leaves_ && mp.ev.imgX == hoverImgX_ && mp.ev.imgY == hoverImgY_) return;
    hoverSent_ = true;
    hoverImgX_ = mp.ev.imgX;
    hoverImgY_ = mp.ev.imgY;
    hoverLeaves_ = leaves_;
    hoverLive_ = hit_->clientHover || hit_->anyHover;
    const QPoint g = mapToGlobal(pos);
    mp.ev.winX = g.x();
    mp.ev.winY = g.y();
    mp.ev.button = button;
    mp.ev.mods.ctrl = mods & Qt::ControlModifier;
    mp.ev.mods.shift = mods & Qt::ShiftModifier;
    mp.ev.mods.alt = mods & Qt::AltModifier;
    emit pointer(mp);
}

void MapWidget::popupClosed() {
    QTimer::singleShot(0, this, [this] {
        if (QApplication::activePopupWidget()) return;
        reportHoverAfterPopup(QCursor::pos(), QGuiApplication::mouseButtons() & Qt::RightButton);
    });
}

void MapWidget::reportHoverAfterPopup(const QPoint& globalPos, bool rightHeld) {
    if (targetAt(globalPos).map != this) return;
    hovering_ = true;
    hoverPos_ = mapFromGlobal(globalPos);
    updateHover();
    hoverSent_ = false;
    emitHover(hoverPos_, QGuiApplication::keyboardModifiers(), false, rightHeld ? 3 : 0);
}

void MapWidget::mouseMoveEvent(QMouseEvent* e) {
    hovering_ = true;
    hoverPos_ = e->position().toPoint();
    updateHover();
    if (heldButton_)
        emitPointer(MapPointer::Move, e, heldButton_);
    else if (!e->buttons())
        emitHover(hoverPos_, e->modifiers(), true);  // stock sends no hover report while a button is held
}

void MapWidget::enterEvent(QEnterEvent* e) {
    skipLeave_ = false;
    QWidget::enterEvent(e);
}

// Stock sends no leave for a control whose drag ended off it: over another control or outside the window.
void MapWidget::leaveEvent(QEvent* e) {
    hovering_ = false;
    updateHover();
    hoverSent_ = false;
    if (skipLeave_)
        skipLeave_ = false;
    else
        leaveSoon();
    QWidget::leaveEvent(e);
}

// Stock posts a control's leave after the motion that entered the next control, and none when its right-click menu
// opens over the map. A leave held for an open popup goes when it closes, unless the pointer is back on this control.
void MapWidget::leaveSoon() {
    QTimer::singleShot(0, this, [this] {
        if (QWidget* pop = QApplication::activePopupWidget()) {
            if (heldPopup_ != pop) {
                pop->installEventFilter(this);
                heldPopup_ = pop;
            }
            return;
        }
        MapPointer mp;
        mp.kind = MapPointer::Leave;
        mp.control = controlId_;
        leaves_++;
        emit pointer(mp);
    });
}

bool MapWidget::eventFilter(QObject* o, QEvent* ev) {
    if (o == heldPopup_ && ev->type() == QEvent::Hide) {
        o->removeEventFilter(this);
        heldPopup_ = nullptr;
        if (targetAt(QCursor::pos()).map != this) leaveSoon();
    }
    return QWidget::eventFilter(o, ev);
}

void MapWidget::mouseReleaseEvent(QMouseEvent* e) {
    int b = x11Button(e->button());
    if (b == 3 && localPress_ && !heldButton_) {
        finishRightRelease(e->globalPosition().toPoint(), e->modifiers(), qint64(e->timestamp()));
        return;
    }
    if (!heldButton_ || b != heldButton_) return;
    heldButton_ = 0;
    if (targetAt(e->globalPosition().toPoint()).map != this) skipLeave_ = true;
    emitPointer(MapPointer::Release, e, b);
}

// The release of a right press answered locally. The flag is read now: still TRUE opens the menu, FALSE sends the
// MouseUp and Click of a press that never went out.
void MapWidget::finishRightRelease(const QPoint& g, Qt::KeyboardModifiers mods, qint64 tMs) {
    localPress_ = false;
    if (rightClickToWire()) {
        MapPointer mp;
        mp.kind = MapPointer::Release;
        mp.pressUnsent = true;
        if (Target t = targetAt(g); t.map) {
            mp.scene = t.map->hit_;
            mp.control = t.map->controlId_;
            if (!t.map->hit_ || !t.map->toImage(t.map->mapFromGlobal(g), mp.ev.imgX, mp.ev.imgY, true)) mp.scene = {};
        } else {
            mp.offMap = true;
            mp.control = t.control;
        }
        mp.ev.winX = g.x();
        mp.ev.winY = g.y();
        mp.ev.button = 3;
        mp.ev.mods.ctrl = mods & Qt::ControlModifier;
        mp.ev.mods.shift = mods & Qt::ShiftModifier;
        mp.ev.mods.alt = mods & Qt::AltModifier;
        mp.ev.tMs = tMs;
        emit pointer(mp);
        return;
    }
    int ix = 0, iy = 0;
    const bool onImage = toImage(mapFromGlobal(g), ix, iy, false);
    QString name;
    fugue::render::Naming n;
    if (hit_ && onImage && fugue::render::nameMouse(*hit_, ix, iy, n)) name = QString::fromStdString(n.hit.name);
    emit localMenuRequested(g, name);
    if (!onImage) return;  // on the letterbox: no verb menu
    MapPointer mp;
    mp.kind = MapPointer::Press;
    mp.scene = hit_;
    mp.control = controlId_;
    mp.ev.imgX = ix;
    mp.ev.imgY = iy;
    mp.ev.winX = g.x();
    mp.ev.winY = g.y();
    mp.ev.button = 3;
    mp.ev.mods.ctrl = mods & Qt::ControlModifier;
    mp.ev.mods.shift = mods & Qt::ShiftModifier;
    mp.ev.mods.alt = mods & Qt::AltModifier;
    emit localMenuPointer(g, mp);
}

// A right press outside the open verb menu. Qt's popup grab closes the menu on it and the press never reaches the map, nor
// does its release; stock consumes the press and lets the release act as a right release of its own. The gate sees both
// at the application level: it records the press (the map under it), and takes the release as that map's.
class PopupGate : public QObject {
public:
    QPointer<QMenu> menu;
    QPointer<MapWidget> pressMap;
    bool armed = false;
    ulong pressTs = 0;

    bool insideMenu(const QPoint& g) const {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->geometry().contains(g)) return true;
        return false;
    }
    bool ourPopup() const {
        QWidget* pop = QApplication::activePopupWidget();
        return pop && menu && (pop == menu || menu->isAncestorOf(pop));
    }
    bool eventFilter(QObject*, QEvent* ev) override {
        const QEvent::Type t = ev->type();
        if (t != QEvent::MouseButtonPress && t != QEvent::MouseButtonRelease) return false;
        auto* e = static_cast<QMouseEvent*>(ev);
        if (e->button() != Qt::RightButton) return false;
        const QPoint g = e->globalPosition().toPoint();
        if (t == QEvent::MouseButtonPress) {
            if (armed && e->timestamp() == pressTs) return false;  // the same press is seen once per receiver
            armed = false;
            MapWidget::consumedPress_ = false;
            if (!ourPopup() || insideMenu(g)) return false;
            pressMap = nullptr;
            if (MapWidget::Target tg = MapWidget::targetAtGlobal(g, QApplication::activeWindow()); tg.map)
                pressMap = const_cast<MapWidget*>(tg.map);
            if (!pressMap) return false;
            armed = true;
            pressTs = e->timestamp();
            MapWidget::consumedPress_ = true;
            return false;  // Qt closes the menu on it
        }
        if (!armed) return false;
        armed = false;
        MapWidget::consumedPress_ = false;
        MapWidget* m = pressMap;
        pressMap = nullptr;
        if (!m || m->localPress_) return false;  // the press was replayed to the map: its own release handles it
        m->finishRightRelease(g, e->modifiers(), qint64(e->timestamp()));
        return true;
    }
};

void MapWidget::watchPopup(QMenu* menu) {
    static PopupGate* gate = [] {
        auto* g = new PopupGate;
        qApp->installEventFilter(g);
        return g;
    }();
    gate->menu = menu;
}

// One frame per wheel event, as stock DreamSeeker sends one per WM_MOUSEWHEEL. Over the letterbox the image position
// is extrapolated (unclamped). Horizontal scrolling sends nothing (stock sent nothing for buttons 6/7); Qt on X11 turns
// Alt+wheel into a horizontal delta, which is turned back, since stock sends Alt+wheel as a vertical notch with alt=1.
void MapWidget::wheelEvent(QWheelEvent* e) {
    e->accept();
    QPoint d = e->angleDelta();
    if ((e->modifiers() & Qt::AltModifier) && d.y() == 0) d = d.transposed();
    if (d.y() == 0 || frameSize_.isEmpty()) return;
    QRect r = imageRect();
    if (r.width() <= 0 || r.height() <= 0) return;
    QPoint pos = e->position().toPoint();
    MapPointer mp;
    mp.kind = MapPointer::Wheel;
    mp.scene = hit_;
    mp.control = controlId_;
    mp.ev.imgX = int(std::floor(double(pos.x() - r.x()) * frameSize_.width() / r.width()));
    mp.ev.imgY = int(std::floor(double(pos.y() - r.y()) * frameSize_.height() / r.height()));
    mp.ev.winX = e->globalPosition().toPoint().x();
    mp.ev.winY = e->globalPosition().toPoint().y();
    Qt::MouseButtons b = e->buttons();
    mp.ev.button = (b & Qt::LeftButton) ? 1 : (b & Qt::MiddleButton) ? 2 : (b & Qt::RightButton) ? 3 : 0;
    mp.ev.mods.ctrl = e->modifiers() & Qt::ControlModifier;
    mp.ev.mods.shift = e->modifiers() & Qt::ShiftModifier;
    mp.ev.mods.alt = e->modifiers() & Qt::AltModifier;
    mp.ev.tMs = qint64(e->timestamp());
    mp.wheelY = d.y();
    emit pointer(mp);
}

}  // namespace fugue::shell
