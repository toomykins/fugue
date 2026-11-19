// MapWidget: paints the latest finished frame (opaque RGBX8888, produced off the GUI thread) at an integer zoom,
// point-sampled and centred on a black letterbox. It never composites.
#pragma once
#include <string>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaType>
#include <QPointer>
#include <QMenu>
#include <QWidget>
#include <QWindow>
#include <memory>
#include <functional>
#include <optional>
#include "render/hittest.h"
#include "render/mousechain.h"

namespace fugue::shell {

using HitPtr = std::shared_ptr<const fugue::render::HitScene>;

// One pointer event on the map image, carrying the scene that was on screen when it happened, so the hit test
// answers for the picture the player saw. It crosses to the session thread through a mutex-guarded queue
// (app/sessionworker.h).
struct MapPointer {
    // Hover: button-less motion (motion=true) or a new atom under a still pointer (motion=false); Leave: the pointer left.
    enum Kind { Press, Move, Release, Wheel, Hover, Leave } kind = Press;
    fugue::render::PointerEvent
        ev;  // Wheel and Hover: imgX/imgY unclamped; Wheel: ev.button is the button held (0 none)
    HitPtr scene;
    int wheelX = 0, wheelY = 0;             // Wheel: the notch's angle delta (120 per notch, > 0 away from the user)
    bool motion = true;                     // Hover
    std::string control = "mapwindow.map";  // the map control's skin id (MapWidget::controlId)
    // Move/Release over no map control: control is then the control or window there, "" outside every window.
    bool offMap = false;
    // Release: the right press it ends was answered locally (client.show_popup_menus was TRUE) and the flag is FALSE now, so
    // the release goes to the wire as MouseUp and Click with no MouseDown (render::MouseChain::releaseUnpressed).
    bool pressUnsent = false;
};

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget* parent = nullptr);
    // `hit` is the scene the frame was drawn from (render::assembleMapControl withHit); pointer events are hit-tested
    // against it. Null (no scene yet): pointer events are dropped.
    void setFrame(const QImage& img, HitPtr hit = {});
    HitPtr hitScene() const { return hit_; }
    // UI-only diagnostics, painted above the map without changing its image, hit testing or saved map screenshots.
    // Empty text hides the overlay. The caller updates it at a bounded rate, independently of incoming frames.
    void setPerformanceText(const QString& text);
    QString performanceText() const { return performanceText_; }
    // A newer scene for the same picture (the render thread skipped an unchanged list): no repaint.
    void setHit(HitPtr hit) { hit_ = std::move(hit); }
    bool hasFrame() const { return !frameSize_.isEmpty(); }
    // Optional native presenter owned by this widget: its container and the window drawn into it. CPU frames hide it;
    // snapshots are requested only on demand.
    void setExternalView(QWidget* view, QWindow* window = nullptr);
    void setExternalFrame(QSize size, HitPtr hit, std::function<QImage()> snapshot);
    // The presenter's window is drawn over every non-native sibling, so it is masked out wherever a visible sibling
    // stacked above this map covers it, leaving those controls on top as in CPU mode. Called when the skin places controls.
    void clipExternalView();
    bool hasExternalFrame() const { return bool(snapshot_); }
    QImage overlayImage() const;
    QImage viewSnapshot() const;
    bool smoothScaling(QSize sourceSize = {}) const;
    // The `right-click` skin parameter: a right click reaches the wire only if true; otherwise the widget answers it
    // locally (localMenuRequested), as stock DreamSeeker's verb popup does.
    void setRightClickToWire(bool v) { rightClick_ = v; }
    // client.show_popup_menus: while false there is no local menu and the right click goes to the wire. The flag is read at
    // the press (sent as MouseDown iff false) and again at the release, which opens the menu (still true), or sends the
    // MouseUp and Click of a press that was not sent (false now); a press that was sent finishes as a click either way.
    void setPopupMenus(bool v) { popupMenus_ = v; }
    bool rightClickToWire() const { return rightClick_ || !popupMenus_; }
    // Widget position to image pixel (top-left origin); false outside the letterboxed image, unless extrapolate.
    bool toImage(const QPoint& pos, int& ix, int& iy, bool extrapolate) const;
    // The skin id of this map control ("mapwindow.map"); every mouse event names it.
    void setControlId(const std::string& id) { controlId_ = id; }
    const std::string& controlId() const { return controlId_; }
    void setIconSize(int px) {
        iconSize_ = px;
        zoom_ = 0;
        update();
        emit presentationChanged();
    }  // skin icon-size; 0 means fit
    // Scaling. zoom (relative to world.icon_size, fractional) and icon-size (absolute px) are one property: at most
    // one is nonzero, and 0 means auto-fit. The letterbox picks fit (min) or cover (max, centre-cropped). zoom-mode
    // only selects the resampler.
    void setMapScale(double zoom, int iconSize, bool letterbox, const std::string& mode);
    const std::string& zoomMode() const { return mode_; }  // resampler in use: normal, distort or blur
    int iconSizePx() const { return iconSize_; }           // icon-size in use; 0 means auto-fit or zoom
    double scaleF(QSize sourceSize = {}) const;            // the factor in use (fractional when auto-fitting)
    int scale() const { return int(scaleF() + 0.5); }      // scaleF() rounded
    QRect imageRect(QSize sourceSize = {}) const;          // where the frame lands inside the widget
    uint64_t framesShown() const { return shown_; }
    // The latest frame at its native size, as `.screenshot` saves it (shell/screenshot.h).
    QImage frame() const { return snapshot_ ? snapshot_() : img_; }
    // Name of the atom under the pointer (render::hitAt on the frame on screen, honouring mouse_opacity), or "" over
    // nothing, a nameless atom, the letterbox or off the control.
    QString hoverName() const { return hoverName_; }
    // Button-less motion is posted as Hover only while this is true (see mapwidget.cpp).
    bool wantsHover() const;
    // The verb popup closed: it swallowed the pointer's motion, so report what is under the pointer now, with right=1 while
    // the right button is still down. reportHoverAfterPopup takes the pointer's place, popupClosed reads the cursor.
    void popupClosed();
    // The verb popup menu of the app. A right press outside it closes it and is consumed (no MouseDown; the hover that
    // follows carries right=1), and its release then behaves as a fresh right release: a new menu where the flag allows one,
    // else MouseUp and Click. Qt's popup grab swallows that press and release, so they are taken at the application level.
    static void watchPopup(QMenu* menu);
    void reportHoverAfterPopup(const QPoint& globalPos, bool rightHeld);
    // Whether the platform reports where top-level windows are (not on Wayland); unset follows the platform.
    static void setWindowPositionsKnown(std::optional<bool> known) { positionsKnown_ = known; }
signals:
    void presentationChanged();  // geometry, zoom or diagnostics changed; a native presenter should repaint
    // Press, drag motion or release on the image; a wheel notch, a button-less step or a leave over the control.
    void pointer(const fugue::shell::MapPointer& p);
    void localMenuRequested(const QPoint& globalPos, const QString& atomName);  // right click kept off the wire
    // The same right click with the scene it hit-tested against, so the session thread can build the atom's verb
    // menu (model/verbmenu.h); only that thread may read the model. Emitted only when the click landed on the image.
    void localMenuPointer(const QPoint& globalPos, const fugue::shell::MapPointer& at);
    // hoverName() changed: the main window's status bar shows it (SkinWindow).
    void hoverChanged(const QString& name);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    bool eventFilter(QObject* o, QEvent* ev) override;
    void keyPressEvent(QKeyEvent* e) override { e->ignore(); }  // the app-level focus filter routes keys
private:
    QImage img_;
    QSize frameSize_;
    QPointer<QWidget> externalView_;
    QPointer<QWindow> externalWindow_;
    std::function<QImage()> snapshot_;
    void paintOverlay(QPainter& painter) const;
    QString performanceText_;
    int iconSize_ = 0, tile_ = 32;
    double zoom_ = 0;
    bool letterbox_ = true;
    std::string mode_ = "normal";
    uint64_t shown_ = 0;
    HitPtr hit_;
    bool rightClick_ = false;
    bool popupMenus_ = true;
    std::string controlId_ = "mapwindow.map";
    int heldButton_ = 0;
    bool localPress_ = false;  // a right press answered locally, its release still to come
    void finishRightRelease(const QPoint& globalPos, Qt::KeyboardModifiers mods,
                            qint64 tMs);  // the release of a localPress_
    friend class PopupGate;
    static inline bool consumedPress_ =
        false;               // a replayed right press that closed the verb menu: answered locally whatever the flag
    bool hovering_ = false;  // the pointer is over this control; hoverPos_ is where it is
    QPoint hoverPos_;
    QString hoverName_;
    // The hover report's dedupe: the last image pixel a Hover was emitted for, and the atom then under the pointer.
    bool hoverSent_ = false;
    int hoverImgX_ = 0, hoverImgY_ = 0;
    uint64_t hoverLeaves_ = 0;
    static uint64_t leaves_;  // Leaves posted by every map widget (GUI thread)
    struct HoverKey {
        bool hit = false, screen = false;
        uint8_t kind = 0;
        uint32_t ref = 0;
        int x = 0, y = 0;
    } hoverKey_;
    bool hoverLive_ = false;       // the last Hover was posted under a scene that wanted hover
    bool skipLeave_ = false;       // a press from here was released off this control
    QPointer<QWidget> heldPopup_;  // the popup whose closing decides a held leave
    bool updateHover();            // true when the atom under the pointer changed
    void leaveSoon();
    // What lies under a screen position: a map control, else the id of the control or window there ("" outside every
    // window, and over an area no control names).
    struct Target {
        const MapWidget* map = nullptr;
        std::string control;
    };
    static std::optional<bool> positionsKnown_;
    Target targetAt(const QPoint& globalPos) const;
    static Target targetAtGlobal(const QPoint& globalPos, const QWidget* top);
    bool unclampedImage(const QPoint& pos, int& ix, int& iy) const;
    void emitHover(const QPoint& pos, Qt::KeyboardModifiers mods, bool motion, int button = 0);
    void emitPointer(MapPointer::Kind k, QMouseEvent* e, int button);
};

}  // namespace fugue::shell

Q_DECLARE_METATYPE(fugue::shell::MapPointer)
Q_DECLARE_METATYPE(fugue::shell::HitPtr)
