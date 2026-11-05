#pragma once
#include <EGL/egl.h>

#include <cstdint>

// The stage renderer's half of a capture take (ADR 0008): an EGL window surface over the take's AVC encoder
// input, sharing the screen's context, that receives a copy of every finished frame the take keeps.
//
// The copy is a framebuffer blit with the screen as the read surface and the encoder as the draw surface
// (eglMakeCurrent takes them separately), taken after the composite and before the UI blur: the take holds
// the visual exactly as the screen shows it, and never the app's glass sheet, mini player or anything Compose
// draws, which live in other layers anyway. Nothing is rendered twice and no engine state steps twice.
//
// GL thread only. The surface follows the process-wide output::TakeSession: made on the first frame of a
// take, remade if the context changes under it (a lost context brings a new one), and dropped once the take
// ends.
namespace geode::viz {

class TakeSurface {
public:
    TakeSurface() = default;
    TakeSurface(const TakeSurface&) = delete;
    TakeSurface& operator=(const TakeSurface&) = delete;
    ~TakeSurface() { release(); }

    // After the composite, with the screen's surface current and its finished frame in the back buffer
    // (width × height). frameSeconds is the engine frame time this frame was rendered for (BOOTTIME).
    void present(double frameSeconds, int width, int height);

    // Drops the encoder surface. Safe without a current context (a teardown runs contextless).
    void release();

private:
    bool attach(EGLDisplay display, EGLContext context, void* window);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    uint64_t generation_ = 0;
    bool failed_ = false;  // this take's surface could not be made; do not retry every frame
};

}  // namespace geode::viz
