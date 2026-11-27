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
    // eglSwapBuffers, at the window's own pace. EGL_SUCCESS, or the error it left.
    EGLint swap();
    // EGL_SUCCESS after a swap that returned true, or the error a failed one left.
    static EGLint swapError();
    EGLDisplay display() const { return display_; }
    EGLSurface surface() const { return surface_; }
    ANativeWindow* window() const { return window_; }
    void destroySurface();
    void destroyContext();

private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    ANativeWindow* window_ = nullptr;
    bool surfaceless_ = false;
};

}  // namespace geode::stage::platform
