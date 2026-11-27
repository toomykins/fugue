#pragma once
#include "shell/spriterenderer.h"
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <QOffscreenSurface>
// Qt declares its Vulkan types only when the Vulkan headers are there at build time: the build says whether they
// are (shell/CMakeLists.txt), the same for every file that includes this.
#ifndef FUGUE_TEST_VULKAN
#define FUGUE_TEST_VULKAN 0
#endif
#if FUGUE_TEST_VULKAN
#include <QVulkanInstance>
#endif
#include <QDebug>
#include <memory>

// Q_INIT_RESOURCE must resolve the generated symbol in the global namespace.
inline void initGpuResources() { Q_INIT_RESOURCE(mapshaders); }

namespace fugue::shell::test {
// Native GPU texture readback, not a desktop screenshot or presentation test.
// Two independent renderer pools share a device; all channels compare exactly.
struct GpuComparison {
    std::unique_ptr<QOffscreenSurface> fallback;
#if FUGUE_TEST_VULKAN
    QVulkanInstance vulkan; // outlives the device below
#endif
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderPassDescriptor> descriptor;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    SpriteRenderer reference{false}, bounded{true};
    uint64_t uploaded=0;
    // The backend is the process's (FUGUE_GPU_BACKEND: opengl, vulkan where the build has its headers, or d3d11 on
    // Windows), on the machine's own adapter unless FUGUE_GPU_SOFTWARE=1 asks for a software one. False when that
    // backend is not available here: a test never runs on another backend than its entry names.
    bool initialize(QSize size) {
        ::initGpuResources();
        const QByteArray backend=qgetenv("FUGUE_GPU_BACKEND").toLower();
#ifdef Q_OS_WIN
        const QRhi::Implementation wanted=backend=="opengl" ? QRhi::OpenGLES2 : backend=="vulkan" ? QRhi::Vulkan : QRhi::D3D11;
        if (!backend.isEmpty() && backend!="opengl" && backend!="vulkan" && backend!="d3d11") return false;
#else
        const QRhi::Implementation wanted=backend=="vulkan" ? QRhi::Vulkan : QRhi::OpenGLES2;
        if (!backend.isEmpty() && backend!="opengl" && backend!="vulkan") return false;
#endif
        const bool software=qEnvironmentVariableIntValue("FUGUE_GPU_SOFTWARE")==1;
        Q_UNUSED(software);
        if (wanted==QRhi::OpenGLES2) {
            QRhiGles2InitParams params;
            fallback.reset(QRhiGles2InitParams::newFallbackSurface());params.fallbackSurface=fallback.get();
            rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
        } else if (wanted==QRhi::Vulkan) {
#if FUGUE_TEST_VULKAN
            vulkan.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
            if (!vulkan.create()) return false;
            QRhiVulkanInitParams params; params.inst=&vulkan;
            rhi.reset(QRhi::create(QRhi::Vulkan,&params,software ? QRhi::PreferSoftwareRenderer : QRhi::Flags{}));
#endif
        } else {
#ifdef Q_OS_WIN
            QRhiD3D11InitParams params;
            rhi.reset(QRhi::create(QRhi::D3D11,&params,software ? QRhi::PreferSoftwareRenderer : QRhi::Flags{}));
#endif
        }
        if (!rhi || rhi->backend()!=wanted ||
            !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F,QRhiTexture::RenderTarget)) return false;
        qInfo()<<rhi->backendName()<<rhi->driverInfo().deviceName;
        texture.reset(rhi->newTexture(QRhiTexture::RGBA8,size,1,QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create()) return false;
        target.reset(rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(texture.get()))));
        descriptor.reset(target->newCompatibleRenderPassDescriptor());target->setRenderPassDescriptor(descriptor.get());
        return target->create() && reference.initialize(rhi.get(),descriptor.get(),1) && bounded.initialize(rhi.get(),descriptor.get(),1);
    }
    QImage render(SpriteRenderer& renderer, const MapSprites& scene, QRect source, QColor clear = Qt::transparent,
                  std::optional<QRect> worldClip = {}, QRect worldView = {}) {
        QRhiCommandBuffer* cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        auto* updates=rhi->nextResourceUpdateBatch();uploaded=renderer.prepare(updates,scene,source,worldClip,worldView);
        renderer.renderOffscreen(cb,updates);
        cb->beginPass(target.get(),clear,{1,0});
        renderer.draw(cb,QRhiViewport(0,0,float(texture->pixelSize().width()),float(texture->pixelSize().height())));cb->endPass();
        QRhiReadbackResult readback;bool completed=false;readback.completed=[&]{completed=true;};
        updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(texture.get()),&readback);
        cb->resourceUpdate(updates);
        if (rhi->endOffscreenFrame()!=QRhi::FrameOpSuccess || !renderer.ready() || !completed) return {};
        return QImage(reinterpret_cast<const uchar*>(readback.data.constData()),readback.pixelSize.width(),
            readback.pixelSize.height(),QImage::Format_RGBA8888).copy();
    }
};
}
