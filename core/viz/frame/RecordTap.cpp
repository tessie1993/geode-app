#include "viz/frame/RecordTap.hpp"

namespace geode::viz::frame {

namespace {

// EGL_ANDROID_recordable and EGL_ANDROID_presentation_time, named here so the file needs no eglext.h.
constexpr EGLint kRecordableAndroid = 0x3142;
using PresentationTimeFn = EGLBoolean (*)(EGLDisplay, EGLSurface, int64_t);

EGLint attrib(EGLDisplay display, EGLConfig config, EGLint name) {
    EGLint value = 0;
    eglGetConfigAttrib(display, config, name, &value);
    return value;
}

// The context's own config, by its id.
bool contextConfig(EGLDisplay display, EGLContext context, EGLConfig* out) {
    EGLint id = 0;
    if (!eglQueryContext(display, context, EGL_CONFIG_ID, &id)) return false;
    const EGLint want[] = {EGL_CONFIG_ID, id, EGL_NONE};
    EGLint count = 0;
    return eglChooseConfig(display, want, out, 1, &count) && count == 1;
}

// A config the encoder asks for (recordable) with the context's colour sizes, which is what makes a surface
// compatible with a context. False where the driver has none.
bool recordableConfig(EGLDisplay display, EGLConfig like, EGLConfig* out) {
    const EGLint want[] = {
        EGL_RED_SIZE, attrib(display, like, EGL_RED_SIZE),
        EGL_GREEN_SIZE, attrib(display, like, EGL_GREEN_SIZE),
        EGL_BLUE_SIZE, attrib(display, like, EGL_BLUE_SIZE),
        EGL_ALPHA_SIZE, attrib(display, like, EGL_ALPHA_SIZE),
        EGL_RENDERABLE_TYPE, attrib(display, like, EGL_RENDERABLE_TYPE),
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        kRecordableAndroid, 1,
        EGL_NONE,
    };
    EGLint count = 0;
    return eglChooseConfig(display, want, out, 1, &count) && count == 1;
}

}  // namespace

bool RecordTap::start(void* nativeWindow, int width, int height) {
    stop();
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    if (!nativeWindow || width <= 0 || height <= 0 || display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        return false;
    }
    const EGLSurface draw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface read = eglGetCurrentSurface(EGL_READ);
    EGLConfig own{};
    if (!contextConfig(display, context, &own)) return false;
    EGLConfig recordable{};
    const bool hasRecordable = recordableConfig(display, own, &recordable);
    const EGLConfig tries[] = {hasRecordable ? recordable : own, own};
    const auto window = static_cast<EGLNativeWindowType>(reinterpret_cast<uintptr_t>(nativeWindow));
    for (const EGLConfig config : tries) {
        const EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
        if (surface == EGL_NO_SURFACE) continue;
        // The one real test of "this context draws into that surface" is to make them current together.
        const bool fits = eglMakeCurrent(display, surface, read, context) == EGL_TRUE;
        eglMakeCurrent(display, draw, read, context);
        if (fits) {
            eglSwapInterval(display, 0);
            display_ = display;
            surface_ = surface;
            width_ = width;
            height_ = height;
            return true;
        }
        eglDestroySurface(display, surface);
    }
    return false;
}

void RecordTap::stop() {
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    display_ = EGL_NO_DISPLAY;
    width_ = 0;
    height_ = 0;
}

bool RecordTap::begin(GLuint sourceFbo, int sourceWidth, int sourceHeight) {
    if (!running() || sourceWidth <= 0 || sourceHeight <= 0) return false;
    context_ = eglGetCurrentContext();
    stageDraw_ = eglGetCurrentSurface(EGL_DRAW);
    stageRead_ = eglGetCurrentSurface(EGL_READ);
    if (context_ == EGL_NO_CONTEXT) return false;
    glGetIntegerv(GL_VIEWPORT, viewport_);
    scissor_ = glIsEnabled(GL_SCISSOR_TEST);
    // Draw into the encoder, read from the stage: framebuffer 0 is then a different surface on each side.
    if (!eglMakeCurrent(display_, surface_, stageDraw_, context_)) {
        eglMakeCurrent(display_, stageDraw_, stageRead_, context_);
        return false;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, sourceWidth, sourceHeight, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glViewport(0, 0, width_, height_);
    return true;
}

bool RecordTap::present(int64_t ptsNs, GLuint sourceFbo) {
    if (!running()) return false;
    static const auto presentationTime =
        reinterpret_cast<PresentationTimeFn>(eglGetProcAddress("eglPresentationTimeANDROID"));
    if (presentationTime) presentationTime(display_, surface_, ptsNs);
    const bool swapped = eglSwapBuffers(display_, surface_) == EGL_TRUE;
    restore(sourceFbo);
    return swapped;
}

void RecordTap::restore(GLuint sourceFbo) {
    eglMakeCurrent(display_, stageDraw_, stageRead_, context_);
    glBindFramebuffer(GL_FRAMEBUFFER, sourceFbo);
    glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    if (scissor_) glEnable(GL_SCISSOR_TEST);
}

}  // namespace geode::viz::frame
