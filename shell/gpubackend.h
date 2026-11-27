// The one graphics backend of the process. FUGUE_GPU_BACKEND (docs/USAGE.md) is the only supported way to choose it,
// including a software device.
// A window has one graphics device, which the map, Qt Quick and browser content in it all draw with, so they must
// agree. Qt Quick and WebEngine follow QSG_RHI_BACKEND, the map follows ours: settleGpuBackend() makes both name
// the same backend, always, before anything of Qt's reads them. Header-only, QtCore alone.
#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QList>
#include <QtCore/QLibrary>
#include <cstdint>
#include <QtCore/qtenvironmentvariables.h>

namespace fugue::shell {
// Whether this machine can draw a window with Vulkan: its loader is installed, an instance that can present to a
// window can be made, and at least one graphics device can be opened with a swapchain. Listing a device is not
// enough: a broken or compute-only driver lists one and then fails, which would leave the map blank. Asked of the
// loader itself, since Qt's own Vulkan types need an application object and the Vulkan headers, and this runs
// before either. The structures are Vulkan 1.0's VkApplicationInfo, VkInstanceCreateInfo, VkDeviceQueueCreateInfo
// and VkDeviceCreateInfo, which never change; a pointer-sized handle and a 32-bit result are how every 64-bit
// loader passes its handles and VkResult. Every device has a queue family 0 with at least one queue.
inline bool vulkanAvailable() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    struct Application { std::int32_t type = 0; const void* next = nullptr; const char* name = "fugue";
                         std::uint32_t version = 0; const char* engine = nullptr; std::uint32_t engineVersion = 0;
                         std::uint32_t api = 1u << 22; };   // Vulkan 1.0
    struct Create { std::int32_t type = 1; const void* next = nullptr; std::uint32_t flags = 0;
                    const Application* application = nullptr; std::uint32_t layers = 0; const char* const* layerNames = nullptr;
                    std::uint32_t extensions = 0; const char* const* extensionNames = nullptr; };
    struct Queue { std::int32_t type = 2; const void* next = nullptr; std::uint32_t flags = 0; std::uint32_t family = 0;
                   std::uint32_t count = 1; const float* priorities = nullptr; };
    struct Device { std::int32_t type = 3; const void* next = nullptr; std::uint32_t flags = 0;
                    std::uint32_t queues = 1; const Queue* queue = nullptr; std::uint32_t layers = 0;
                    const char* const* layerNames = nullptr; std::uint32_t extensions = 0;
                    const char* const* extensionNames = nullptr; const void* features = nullptr; };
    QLibrary loader(QStringLiteral("vulkan-1"));
    if (!loader.load()) return false;
    const auto create = reinterpret_cast<std::int32_t (*)(const Create*, const void*, void**)>(loader.resolve("vkCreateInstance"));
    const auto devices = reinterpret_cast<std::int32_t (*)(void*, std::uint32_t*, void**)>(loader.resolve("vkEnumeratePhysicalDevices"));
    const auto open = reinterpret_cast<std::int32_t (*)(void*, const Device*, const void*, void**)>(loader.resolve("vkCreateDevice"));
    const auto close = reinterpret_cast<void (*)(void*, const void*)>(loader.resolve("vkDestroyDevice"));
    const auto destroy = reinterpret_cast<void (*)(void*, const void*)>(loader.resolve("vkDestroyInstance"));
    if (!create || !devices || !open || !close || !destroy) return false;
    const char* const presenting[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    const Application application; Create request; request.application = &application;
    request.extensions = 2; request.extensionNames = presenting;
    void* instance = nullptr;
    if (create(&request, nullptr, &instance) != 0 || !instance) return false;
    void* found[8] = {}; std::uint32_t count = 8;
    const std::int32_t listed = devices(instance, &count, found);   // 5: more than eight, the first eight are given
    bool usable = false;
    if (listed == 0 || listed == 5) {
        const float priority = 1; Queue queue; queue.priorities = &priority;
        const char* const swapchain[] = {"VK_KHR_swapchain"};
        Device wanted; wanted.queue = &queue; wanted.extensions = 1; wanted.extensionNames = swapchain;
        for (std::uint32_t i = 0; i < count && !usable; ++i) {
            void* device = nullptr;
            if (open(found[i], &wanted, nullptr, &device) == 0 && device) { usable = true; close(device, nullptr); }
        }
    }
    destroy(instance, nullptr);
    return usable;
#else
    return false;
#endif
}
// Where Mesa's EGL vendor file is: glvnd's EGL dispatcher chooses its driver by vendor file, not by name, and the
// directory differs between systems (/usr/share or /etc, a prefix in XDG_DATA_DIRS, a Flatpak GL extension).
inline QByteArray mesaEglVendorFile() {
    QList<QByteArray> dirs{"/etc/glvnd/egl_vendor.d"};
    const QByteArray data = qgetenv("XDG_DATA_DIRS");
    for (const QByteArray& dir : (data.isEmpty() ? QByteArray("/usr/local/share:/usr/share") : data).split(':'))
        if (!dir.isEmpty()) dirs.push_back(dir + "/glvnd/egl_vendor.d");
    for (const QByteArray& dir : dirs) {
        const QDir found(QString::fromLocal8Bit(dir), QStringLiteral("*mesa*.json"), QDir::Name, QDir::Files);
        if (!found.isEmpty()) return found.absoluteFilePath(found[0]).toLocal8Bit();
    }
    return {};
}
// The graphics API of a backend name: "opengl" for "opengl-llvmpipe". The map widget and Qt follow the API; the
// device part only chooses the driver, before any is loaded.
inline QByteArray gpuApi(const QByteArray& backend) {
    const qsizetype dash = backend.indexOf('-');
    return dash < 0 ? backend : backend.left(dash);
}
inline QByteArray gpuApi() { return gpuApi(qgetenv("FUGUE_GPU_BACKEND").toLower()); }

// Call first thing in main(). Afterwards FUGUE_GPU_BACKEND holds one backend and QSG_RHI_BACKEND its API. A backend
// is an API, optionally with the software device to draw on:
//   d3d11, d3d11-warp (Windows), metal (macOS), opengl, vulkan, opengl-llvmpipe, vulkan-lavapipe (Mesa, not on
//   Windows or macOS).
// A software device is chosen here through the drivers' own variables, for OpenGL both its X11 (GLX) and its Wayland
// (EGL) dispatcher, so it holds whichever window system Qt uses; web pages are then drawn in software as well. With none, an unknown name or another platform's
// software device asked for, the platform's usual one: Vulkan on Windows, or Direct3D 11 on a machine without Vulkan; OpenGL on
// Linux; and Metal on macOS unless metalWorks is given and says no, then OpenGL (Qt makes the same fallback on its
// own, but only while its variable is unset). A backend asked for by name is used as given and fails visibly where
// its API or device does not exist.
inline QByteArray settleGpuBackend(bool (*metalWorks)() = nullptr) {
    QByteArray backend = qgetenv("FUGUE_GPU_BACKEND").toLower();
    bool known = backend == "d3d11" || backend == "metal" || backend == "opengl" || backend == "vulkan";
#if defined(Q_OS_WIN)
    known = known || backend == "d3d11-warp";
#elif !defined(Q_OS_MACOS)
    known = known || backend == "opengl-llvmpipe" || backend == "vulkan-lavapipe";
#endif
    if (!known) {
        if (!backend.isEmpty())
            qWarning("FUGUE_GPU_BACKEND=%s is not a backend of this platform; using the platform's backend",
                     backend.constData());
#if defined(Q_OS_WIN)
        backend = vulkanAvailable() ? "vulkan" : "d3d11";
        Q_UNUSED(metalWorks);
#elif defined(Q_OS_MACOS)
        backend = !metalWorks || metalWorks() ? "metal" : "opengl";
#else
        backend = "opengl";
        Q_UNUSED(metalWorks);
#endif
    }
    if (backend == "d3d11-warp") {
        qputenv("QSG_RHI_PREFER_SOFTWARE_RENDERER", "1");
    } else if (backend == "opengl-llvmpipe") {
        qputenv("__GLX_VENDOR_LIBRARY_NAME", "mesa");
        if (const QByteArray vendor = mesaEglVendorFile(); !vendor.isEmpty())
            qputenv("__EGL_VENDOR_LIBRARY_FILENAMES", vendor);
        else
            qWarning("FUGUE_GPU_BACKEND=opengl-llvmpipe: Mesa's EGL vendor file was not found");
        qputenv("LIBGL_ALWAYS_SOFTWARE", "1");
    } else if (backend == "vulkan-lavapipe") {
        qputenv("VK_LOADER_DRIVERS_SELECT", "*lvp*");
    }
    if (backend.contains('-')) {
        // A software device draws web pages in software too. Chromium otherwise renders them on its own GPU device
        // and hands Qt GPU images, which lavapipe imports with the wrong row layout (each page shows its top lines
        // repeated); in software it hands over plain pixels, which every device draws.
        QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        if (!flags.split(' ').contains("--disable-gpu"))
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.isEmpty() ? QByteArray("--disable-gpu") : flags + " --disable-gpu");
    }
    qputenv("FUGUE_GPU_BACKEND", backend);
    qputenv("QSG_RHI_BACKEND", gpuApi(backend));
    return backend;
}
} // namespace fugue::shell
