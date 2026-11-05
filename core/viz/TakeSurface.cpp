#include "viz/TakeSurface.hpp"

#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <cmath>

#include "output/TakeSession.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "GeodeTake";

PFNEGLPRESENTATIONTIMEANDROIDPROC presentationTime() {
    static const auto fn = reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(eglGetProcAddress("eglPresentationTimeANDROID"));
    return fn;
}

// The current context's own config, and before it one with EGL_RECORDABLE_ANDROID and the same colour sizes,
// which lets the encoder take the buffers without a conversion. Some drivers refuse a surface whose config is
// not the context's; attach() then falls back to the context's own.
int candidateConfigs(EGLDisplay display, EGLContext context, EGLConfig out[2]) {
    EGLint id = 0;
    if (!eglQueryContext(display, context, EGL_CONFIG_ID, &id)) return 0;
    const EGLint byId[] = {EGL_CONFIG_ID, id, EGL_NONE};
    EGLConfig own = nullptr;
    EGLint found = 0;
    if (!eglChooseConfig(display, byId, &own, 1, &found) || found < 1) return 0;
    EGLint r = 8, g = 8, b = 8;
    eglGetConfigAttrib(display, own, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(display, own, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(display, own, EGL_BLUE_SIZE, &b);
    const EGLint recordable[] = {EGL_RED_SIZE,        r, EGL_GREEN_SIZE,       g,
                                 EGL_BLUE_SIZE,       b, EGL_RENDERABLE_TYPE,  EGL_OPENGL_ES3_BIT_KHR,
                                 EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,          EGL_RECORDABLE_ANDROID,
                                 EGL_TRUE,            EGL_NONE};
    int n = 0;
    EGLConfig rec = nullptr;
    if (eglChooseConfig(display, recordable, &rec, 1, &found) && found > 0 && rec != own) out[n++] = rec;
    out[n++] = own;
    return n;
}

}  // namespace

bool TakeSurface::attach(EGLDisplay display, EGLContext context, void* window) {
    if (!window) return false;
    const EGLSurface screenDraw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface screenRead = eglGetCurrentSurface(EGL_READ);
    EGLConfig configs[2] = {};
    const int n = candidateConfigs(display, context, configs);
    const EGLint attrs[] = {EGL_NONE};
    for (int i = 0; i < n; ++i) {
        EGLSurface surface = eglCreateWindowSurface(display, configs[i], static_cast<EGLNativeWindowType>(window), attrs);
        if (surface == EGL_NO_SURFACE) continue;
        // Proved current once here, so a config the driver will not pair with this context is found now and
        // not on every frame of the take.
        const bool usable = eglMakeCurrent(display, surface, surface, context) == EGL_TRUE;
        eglMakeCurrent(display, screenDraw, screenRead, context);
        if (usable) {
            display_ = display;
            context_ = context;
            surface_ = surface;
            return true;
        }
        eglDestroySurface(display, surface);
    }
    __android_log_print(ANDROID_LOG_WARN, kTag, "no EGL surface for the take's encoder (0x%x)", eglGetError());
    return false;
}

void TakeSurface::release() {
    if (surface_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
}

void TakeSurface::present(double frameSeconds, int width, int height) {
    const std::shared_ptr<output::Take> take = output::TakeSession::shared().current();
    if (!take) {
        if (generation_ != 0) {
            release();
            generation_ = 0;
            failed_ = false;
        }
        return;
    }
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface screenDraw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface screenRead = eglGetCurrentSurface(EGL_READ);
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT || screenDraw == EGL_NO_SURFACE) return;
    // A new take, a surface dropped by a teardown, or a new context (the old one was lost) makes the surface
    // again; a take whose surface could not be made is not retried every frame.
    const bool fresh = take->generation() != generation_;
    if (fresh) failed_ = false;
    if (fresh || (!failed_ && (surface_ == EGL_NO_SURFACE || context != context_))) {
        release();
        generation_ = take->generation();
        failed_ = !attach(display, context, take->window());
    }
    if (failed_ || surface_ == EGL_NO_SURFACE || !std::isfinite(frameSeconds)) return;
    const std::optional<int64_t> ptsNs = take->beginFrame(static_cast<int64_t>(frameSeconds * 1e9));
    if (!ptsNs) return;

    if (eglMakeCurrent(display, surface_, screenRead, context) == EGL_TRUE) {
        GLint viewport[4] = {};
        GLint readFb = 0;
        GLint drawFb = 0;
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        // Framebuffer 0 reads the screen's back buffer (the read surface) and draws the encoder's.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        const output::TakeSize size = take->size();
        glViewport(0, 0, size.width, size.height);
        const bool same = size.width == width && size.height == height;
        glBlitFramebuffer(0, 0, width, height, 0, 0, size.width, size.height, GL_COLOR_BUFFER_BIT, same ? GL_NEAREST : GL_LINEAR);
        if (auto fn = presentationTime()) fn(display, surface_, *ptsNs);
        eglSwapBuffers(display, surface_);
        eglMakeCurrent(display, screenDraw, screenRead, context);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFb));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFb));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    } else {
        // The screen must never lose its surface over a take: put it back whatever happened.
        eglMakeCurrent(display, screenDraw, screenRead, context);
    }
    take->endFrame();
}

}  // namespace geode::viz
