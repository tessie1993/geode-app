#include "output/android/EncoderGl.hpp"

namespace geode::output {

namespace {

// EGL_KHR_create_context, EGL_ANDROID_recordable and EGL_ANDROID_presentation_time, named here so the file
// needs no eglext.h (viz/frame/RecordTap.cpp does the same).
constexpr EGLint kOpenGlEs3Bit = 0x0040;
constexpr EGLint kRecordableAndroid = 0x3142;
using PresentationTimeFn = EGLBoolean (*)(EGLDisplay, EGLSurface, int64_t);

}  // namespace

const char* EncoderGl::open(void* window) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) return "No EGL display";
    if (!eglInitialize(display_, nullptr, nullptr)) return "eglInitialize failed";
    const EGLint want[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, kOpenGlEs3Bit,
        kRecordableAndroid, 1,
        EGL_NONE,
    };
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display_, want, &config, 1, &count) || count < 1) return "No EGL config";
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, version);
    if (context_ == EGL_NO_CONTEXT) return "eglCreateContext failed";
    const EGLint none[] = {EGL_NONE};
    surface_ = eglCreateWindowSurface(display_, config, reinterpret_cast<EGLNativeWindowType>(window), none);
    if (surface_ == EGL_NO_SURFACE) return "eglCreateWindowSurface failed";
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) return "eglMakeCurrent failed";
    // The encoder's frames carry their own presentation times: a swap never waits on a vsync (viz/frame/RecordTap too).
    eglSwapInterval(display_, 0);
    return nullptr;
}

bool EncoderGl::present(int64_t ptsNs) {
    static const auto presentationTime =
        reinterpret_cast<PresentationTimeFn>(eglGetProcAddress("eglPresentationTimeANDROID"));
    if (presentationTime) presentationTime(display_, surface_, ptsNs);
    return eglSwapBuffers(display_, surface_) == EGL_TRUE;
}

void EncoderGl::close() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
        if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
        eglReleaseThread();
    }
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    display_ = EGL_NO_DISPLAY;
}

}  // namespace geode::output
