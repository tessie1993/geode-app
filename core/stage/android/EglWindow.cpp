#include "stage/android/EglWindow.hpp"

#include <cstring>
#include <vector>

#include "util/Log.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

// EGL_KHR_create_context's ES 3 renderable bit (EGL 1.5's EGL_OPENGL_ES3_BIT), named here so the file needs no
// eglext.h. GLSurfaceView asks for it when the client version is 3.
constexpr EGLint kOpenGlEs3Bit = 0x0040;

EGLint attrib(EGLDisplay display, EGLConfig config, EGLint name) {
    EGLint value = 0;
    eglGetConfigAttrib(display, config, name, &value);
    return value;
}

// GLSurfaceView's default for setEGLContextClientVersion(3): SimpleEGLConfigChooser(true) asks for red, green and
// blue 8, alpha 0, depth 16, stencil 0, ES 3 renderable, then ComponentSizeChooser takes the first config whose
// colour sizes match exactly and whose depth and stencil are at least those. The same config keeps the picture
// the views drew unchanged.
bool chooseConfig(EGLDisplay display, EGLConfig* out) {
    const EGLint spec[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_RENDERABLE_TYPE, kOpenGlEs3Bit,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(display, spec, nullptr, 0, &count) || count <= 0) return false;
    std::vector<EGLConfig> configs(static_cast<size_t>(count));
    if (!eglChooseConfig(display, spec, configs.data(), count, &count)) return false;
    for (EGLint i = 0; i < count; ++i) {
        const EGLConfig c = configs[static_cast<size_t>(i)];
        if (attrib(display, c, EGL_DEPTH_SIZE) < 16 || attrib(display, c, EGL_STENCIL_SIZE) < 0) continue;
        if (attrib(display, c, EGL_RED_SIZE) == 8 && attrib(display, c, EGL_GREEN_SIZE) == 8 &&
            attrib(display, c, EGL_BLUE_SIZE) == 8 && attrib(display, c, EGL_ALPHA_SIZE) == 0) {
            *out = c;
            return true;
        }
    }
    return false;
}

bool hasExtension(EGLDisplay display, const char* name) {
    const char* all = eglQueryString(display, EGL_EXTENSIONS);
    if (!all) return false;
    const size_t n = std::strlen(name);
    for (const char* p = std::strstr(all, name); p; p = std::strstr(p + n, name)) {
        const bool starts = p == all || p[-1] == ' ';
        const bool ends = p[n] == '\0' || p[n] == ' ';
        if (starts && ends) return true;
    }
    return false;
}

}  // namespace

EglWindow::~EglWindow() {
    destroySurface();
    destroyContext();
}

bool EglWindow::ensureDisplay() {
    if (display_ != EGL_NO_DISPLAY) return config_ != nullptr;
    const EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        GEODE_LOGE(kTag, "eglGetDisplay: no display");
        return false;
    }
    // Never terminated: RecordTap, CastTee, the export's encoder and the other stages share the default display.
    if (!eglInitialize(display, nullptr, nullptr)) {
        GEODE_LOGE(kTag, "eglInitialize: 0x%x", static_cast<unsigned>(eglGetError()));
        return false;
    }
    display_ = display;
    if (!chooseConfig(display, &config_)) {
        config_ = nullptr;
        GEODE_LOGE(kTag, "eglChooseConfig: no RGB888 depth-16 ES 3 config");
        return false;
    }
    surfaceless_ = hasExtension(display, "EGL_KHR_surfaceless_context");
    return true;
}

bool EglWindow::createContext() {
    if (context_ != EGL_NO_CONTEXT) return true;
    if (!ensureDisplay()) return false;
    const EGLint attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext context = eglCreateContext(display_, config_, EGL_NO_CONTEXT, attribs);
    if (context == EGL_NO_CONTEXT) {
        GEODE_LOGE(kTag, "eglCreateContext: 0x%x", static_cast<unsigned>(eglGetError()));
        return false;
    }
    context_ = context;
    return true;
}

bool EglWindow::createSurface(ANativeWindow* window) {
    if (!window || context_ == EGL_NO_CONTEXT) return false;
    destroySurface();
    const EGLSurface surface = eglCreateWindowSurface(display_, config_, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
    if (surface == EGL_NO_SURFACE) {
        GEODE_LOGW(kTag, "eglCreateWindowSurface: 0x%x", static_cast<unsigned>(eglGetError()));
        return false;
    }
    ANativeWindow_acquire(window);
    window_ = window;
    surface_ = surface;
    return true;
}

EGLint EglWindow::makeCurrent() {
    if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE) return EGL_BAD_SURFACE;
    if (eglMakeCurrent(display_, surface_, surface_, context_)) return EGL_SUCCESS;
    const EGLint error = eglGetError();
    GEODE_LOGW(kTag, "eglMakeCurrent: 0x%x", static_cast<unsigned>(error));
    return error == EGL_SUCCESS ? EGL_BAD_SURFACE : error;
}

EGLint EglWindow::makeCurrentSurfaceless() {
    if (context_ == EGL_NO_CONTEXT || !surfaceless_) return EGL_BAD_MATCH;
    if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_)) return EGL_SUCCESS;
    const EGLint error = eglGetError();
    GEODE_LOGW(kTag, "eglMakeCurrent (surfaceless): 0x%x", static_cast<unsigned>(error));
    return error == EGL_SUCCESS ? EGL_BAD_MATCH : error;
}

void EglWindow::releaseCurrent() {
    if (display_ == EGL_NO_DISPLAY) return;
    if (eglGetCurrentContext() != context_ || context_ == EGL_NO_CONTEXT) return;
    if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        GEODE_LOGW(kTag, "eglMakeCurrent (release): 0x%x", static_cast<unsigned>(eglGetError()));
    }
}

EGLint EglWindow::swap() {
    if (surface_ == EGL_NO_SURFACE) return EGL_BAD_SURFACE;
    if (eglSwapBuffers(display_, surface_)) return EGL_SUCCESS;
    const EGLint error = eglGetError();
    GEODE_LOGW(kTag, "eglSwapBuffers: 0x%x", static_cast<unsigned>(error));
    return error == EGL_SUCCESS ? EGL_BAD_SURFACE : error;
}

void EglWindow::destroySurface() {
    if (surface_ == EGL_NO_SURFACE) return;
    // Released first, so the surface is not left current and its buffers go back to the window now, not when
    // another surface is made current.
    if (eglGetCurrentSurface(EGL_DRAW) == surface_) releaseCurrent();
    if (!eglDestroySurface(display_, surface_)) GEODE_LOGW(kTag, "eglDestroySurface: 0x%x", static_cast<unsigned>(eglGetError()));
    surface_ = EGL_NO_SURFACE;
    if (window_) ANativeWindow_release(window_);
    window_ = nullptr;
}

void EglWindow::destroyContext() {
    if (context_ == EGL_NO_CONTEXT) return;
    releaseCurrent();
    if (!eglDestroyContext(display_, context_)) GEODE_LOGW(kTag, "eglDestroyContext: 0x%x", static_cast<unsigned>(eglGetError()));
    context_ = EGL_NO_CONTEXT;
}

}  // namespace geode::stage::platform
