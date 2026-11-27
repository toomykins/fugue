// Desktop pixels must come from the unobstructed test window. Diagnostics
// leave activation unchanged and capture the screen as it is.
#pragma once
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QtTest>
#include <functional>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#undef near
#undef far
#endif

namespace nativedesktopfixture {
inline bool ownsPoint(QWidget& window, QPoint global) {
    if(QGuiApplication::topLevelAt(global)!=window.windowHandle()) return false;
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(window.winId());
    if(GetForegroundWindow()!=hwnd) return false;
    const auto client=window.mapFromGlobal(global);
    POINT physical{qRound(client.x()*window.devicePixelRatioF()),qRound(client.y()*window.devicePixelRatioF())};
    if(!ClientToScreen(hwnd,&physical)) return false;
    if(GetAncestor(WindowFromPoint(physical),GA_ROOT)!=hwnd) return false;
#endif
    return window.isActiveWindow();
}

// What stands between the test window and its sample point when that is a window of ANOTHER program, or the
// foreground belongs to one: someone is using the desktop. Empty when nothing foreign is in the way, so a window
// that fails to own its point then is the code's own fault.
inline QString foreignOwner(QWidget& window, QPoint global) {
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(window.winId());
    const auto client=window.mapFromGlobal(global);
    POINT physical{qRound(client.x()*window.devicePixelRatioF()),qRound(client.y()*window.devicePixelRatioF())};
    if(!ClientToScreen(hwnd,&physical)) return {};
    const std::pair<const char*,HWND> candidates[]={{"over the sample point",GetAncestor(WindowFromPoint(physical),GA_ROOT)},
                                                     {"in the foreground",GetForegroundWindow()}};
    for(const auto& [where,other]:candidates) {
        DWORD pid=0;
        if(!other || other==hwnd || !GetWindowThreadProcessId(other,&pid) || pid==GetCurrentProcessId()) continue;
        wchar_t title[256]{}; GetWindowTextW(other,title,256);
        return QStringLiteral("a window of another program is %1 (process %2, \"%3\")")
            .arg(QLatin1String(where)).arg(pid).arg(QString::fromWCharArray(title));
    }
#else
    Q_UNUSED(window); Q_UNUSED(global);
#endif
    return {};
}
// Before a desktop observation: the test window must own the point. When another program's window is in the way the
// test function ends here and says so on a line CTest reports as a skip (shell/CMakeLists.txt); a desktop in use is
// not a failure of the code, and the entry must not pass for it either. Otherwise not owning the point fails.
#define FUGUE_OWN_DESKTOP_POINT(window, sample) \
    do { \
        if(!QTest::qWaitFor([&]{ return nativedesktopfixture::ownsPoint(window,sample); },5000)) { \
            const QString other=nativedesktopfixture::foreignOwner(window,sample); \
            if(other.isEmpty()) QFAIL("The test window does not own its sample point, and no other program is in the way"); \
            qInfo().noquote() << "Desktop interference:" << other; \
            return; \
        } \
    } while(false)

inline void diagnose(QWidget& window, QWidget& surface, QPoint local, qsizetype submissions);

// Once observation starts, obstruction or changed geometry invalidates the case.
// Pixel polling must never silently recover from sampling another window.
class Capture {
    QWidget& window_;
    QWidget& surface_;
    QList<QPoint> points_;
    std::function<qsizetype()> submissions_;
    QRect bounds_, screen_;
    qreal dpr_;
    bool valid_=true;
    bool unchanged() const {
        if(QRect(surface_.mapToGlobal(QPoint(0,0)),surface_.size())!=bounds_ ||
           surface_.screen()->geometry()!=screen_ || surface_.devicePixelRatioF()!=dpr_) return false;
        for(const auto& point:points_) if(!ownsPoint(window_,surface_.mapToGlobal(point))) return false;
        return true;
    }
public:
    Capture(QWidget& window,QWidget& surface,QList<QPoint> points,std::function<qsizetype()> submissions)
        :window_(window),surface_(surface),points_(std::move(points)),submissions_(std::move(submissions)),
         bounds_(surface.mapToGlobal(QPoint(0,0)),surface.size()),screen_(surface.screen()->geometry()),
         dpr_(surface.devicePixelRatioF()) {}
    bool valid() const { return valid_; }
    QImage grab() {
        if(!valid_) return {};
        const bool before=unchanged();
        auto pixels=surface_.screen()->grabWindow(0).toImage();
        valid_=before && unchanged() && !pixels.isNull();
        if(!valid_) {
            qInfo() << "Desktop observation invalidated: window visibility, geometry, DPI or capture changed";
            diagnose(window_,surface_,points_.front(),submissions_());
        }
        return pixels;
    }
    QColor color(const QImage& pixels,QPoint local) const {
        if(!valid_) return {};
        const auto p=bounds_.topLeft()+local-screen_.topLeft();
        const QPoint pixel(p.x()*pixels.width()/screen_.width(),p.y()*pixels.height()/screen_.height());
        return pixels.rect().contains(pixel)?pixels.pixelColor(pixel):QColor{};
    }
};

inline void diagnose(QWidget& window, QWidget& surface, QPoint local, qsizetype submissions) {
    const auto sample=surface.mapToGlobal(local);
    auto* screen=surface.screen();
    const auto pixels=screen->grabWindow(0).toImage();
    const auto p=sample-screen->geometry().topLeft();
    const QPoint pixel(p.x()*pixels.width()/screen->geometry().width(),
                       p.y()*pixels.height()/screen->geometry().height());
    qInfo() << "Native desktop:" << QTest::currentTestFunction() << QTest::currentDataTag()
            << "submissions" << submissions << "window" << window.geometry()
            << "surface" << surface.geometry() << "screen" << screen->geometry()
            << "DPR" << window.devicePixelRatioF() << "exposed" << (window.windowHandle() && window.windowHandle()->isExposed())
            << "active" << QApplication::activeWindow() << "focus" << QApplication::focusWidget()
            << "sample" << sample << "widgetAt" << QApplication::widgetAt(sample)
            << "topLevelAt" << QGuiApplication::topLevelAt(sample)
            << "desktopSize" << pixels.size() << "pixel" << (pixels.rect().contains(pixel)?pixels.pixelColor(pixel):QColor{});
#ifdef Q_OS_WIN
    const auto hwnd=reinterpret_cast<HWND>(window.winId());
    auto describe=[](const char* label,HWND handle) {
        DWORD pid=0; GetWindowThreadProcessId(handle,&pid);
        RECT rect{}; GetWindowRect(handle,&rect);
        wchar_t title[256]{}; GetWindowTextW(handle,title,256);
        qInfo() << label << quintptr(handle) << "pid" << pid << "title" << QString::fromWCharArray(title)
                << "physicalBounds" << QRect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top)
                << "DPI" << GetDpiForWindow(handle);
    };
    POINT physical{local.x(),local.y()};
    // Map Qt logical coordinates through the top-level's client origin/DPI.
    const auto client=window.mapFromGlobal(sample);
    physical.x=qRound(client.x()*window.devicePixelRatioF());
    physical.y=qRound(client.y()*window.devicePixelRatioF());
    ClientToScreen(hwnd,&physical);
    describe("expected",hwnd);
    describe("foreground",GetForegroundWindow());
    describe("sample owner",WindowFromPoint(physical));
#endif
}
}
