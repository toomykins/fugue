// The backend a native test entry runs on. Entries name it in FUGUE_GPU_BACKEND, which MapWidget reads itself.
#pragma once
#include "shell/mapwidget.h"
#include <QtTest>
#include <optional>

namespace fugue::shell::test {
inline bool backendIsOpenGL() { return qgetenv("FUGUE_GPU_BACKEND").toLower() == "opengl"; }
// Empty when the MapWidget draws with the backend the process settled on (shell/gpubackend.h), otherwise what went
// wrong. Asking for a backend is not having it: without this two entries could pass on one. shown is a map the
// caller has already shown, so a suite can check the very device its tests draw with; none: one of its own.
inline QString backendMismatch(MapWidget* shown = nullptr) {
    const QByteArray asked = qgetenv("FUGUE_GPU_BACKEND").toLower();
    const QString expected = asked == "opengl" ? "OpenGL" : asked == "d3d11" ? "D3D11" : asked == "metal" ? "Metal"
        : asked == "vulkan" ? "Vulkan" : QString();
    if (expected.isEmpty()) return "FUGUE_GPU_BACKEND names no known backend: '" + QString::fromLatin1(asked) + "'";
    std::optional<MapWidget> own;
    if (!shown) { own.emplace(); own->resize(64, 64); own->setViewSize({1, 1}); own->show(); }
    MapWidget& map = shown ? *shown : *own;
    if (!QTest::qWaitForWindowExposed(&map)) return "a MapWidget was not shown";
    for (int waited = 0; map.createdBackend().isEmpty() && waited < 5000; waited += 20) { map.grabFramebuffer(); QTest::qWait(20); }
    return map.createdBackend() == expected ? QString()
        : "FUGUE_GPU_BACKEND asks for " + expected + " but the widget created '" + map.createdBackend() + "'";
}
}
