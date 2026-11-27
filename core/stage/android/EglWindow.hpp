#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>

#include <cstdint>

// The stage's EGL: one display connection, the config GLSurfaceView picks by default for an ES 3 view, one context
// and at most one window surface. Stage thread only. It decides nothing: the stage
// lifecycle (core/stage/StageLifecycle) says when each call happens, and every failure goes back to it.
namespace geode::stage::platform {

class EglWindow {
public:
    EglWindow() = default;
    EglWindow(const EglWindow&) = delete;
    EglWindow& operator=(const EglWindow&) = delete;
    ~EglWindow();

    // The display and the config, once. False when there is no display or no ES 3 config like GLSurfaceView's.
    bool ensureDisplay();
    // EGL_KHR_surfaceless_context: a context may be current with no surface (the client's release after the
    // window has gone). Valid after ensureDisplay.
    bool surfaceless() const { return surfaceless_; }

    bool createContext();
    // Takes its own reference on the window for as long as the surface lives.
    bool createSurface(ANativeWindow* window);
    // EGL_SUCCESS, or the error (EGL_CONTEXT_LOST, EGL_BAD_SURFACE, EGL_BAD_NATIVE_WINDOW...).
    EGLint makeCurrent();
    EGLint makeCurrentSurfaceless();
    void releaseCurrent();
    // Inserts the frame's fence (EGL_KHR_fence_sync) after the draw and swaps. EGL_SUCCESS, or the error
    // eglSwapBuffers left.
    EGLint swap();
    // The last swapped frame's GPU work has not finished: its fence is not signalled, asked with no wait. False with
    // no frame swapped yet, or without EGL_KHR_fence_sync (no fence, so no skip).
    bool previousFrameBusy();
    // The next swap's stamp, CLOCK_MONOTONIC ns: the system does not show the frame before it; 0: none. Used by
    // that swap alone. Does nothing where EGL_ANDROID_presentation_time is missing.
    void presentAt(int64_t stampNs) { stampNs_ = stampNs; }
    void destroySurface();
    void destroyContext();

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    ANativeWindow* window_ = nullptr;
    bool surfaceless_ = false;
    // eglPresentationTimeANDROID, from EGL_ANDROID_presentation_time; null without it.
    EGLBoolean (*presentationTime_)(EGLDisplay, EGLSurface, int64_t) = nullptr;
    int64_t stampNs_ = 0;
    // EGL_KHR_fence_sync, from the display's extensions; null without it.
    EGLSyncKHR (*createSync_)(EGLDisplay, EGLenum, const EGLint*) = nullptr;
    EGLBoolean (*destroySync_)(EGLDisplay, EGLSyncKHR) = nullptr;
    EGLint (*clientWaitSync_)(EGLDisplay, EGLSyncKHR, EGLint, EGLTimeKHR) = nullptr;
    // The fence of the last frame swapped, EGL_NO_SYNC_KHR for none.
    EGLSyncKHR fence_ = EGL_NO_SYNC_KHR;
    void dropFence();
};

}  // namespace geode::stage::platform
