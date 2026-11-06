#include "viz/TakeSurface.hpp"

#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <vector>

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

constexpr const char* kMarkVertex = R"(#version 300 es
uniform vec2 uRepeat;
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? 1.0 : -1.0);
    // Tiles across the frame; the tile is a top-down bitmap, so v runs from the top.
    vUv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5) * uRepeat;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

constexpr const char* kMarkFragment = R"(#version 300 es
precision mediump float;
uniform sampler2D uTile;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = texture(uTile, vUv); }
)";

GLuint compile(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

void TakeSurface::makeWatermark(const output::TakeLimits& limits) {
    if (!limits.watermark()) return;
    const GLuint vs = compile(GL_VERTEX_SHADER, kMarkVertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kMarkFragment);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        __android_log_print(ANDROID_LOG_WARN, kTag, "watermark shader did not compile");
        return;
    }
    markProgram_ = glCreateProgram();
    glAttachShader(markProgram_, vs);
    glAttachShader(markProgram_, fs);
    glLinkProgram(markProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    markRepeat_ = glGetUniformLocation(markProgram_, "uRepeat");
    // Android's ARGB (Bitmap.getPixels, straight alpha) to premultiplied RGBA bytes, as WatermarkOverlay's
    // GLUtils upload has it, so the blend below is the export's own.
    const size_t n = static_cast<size_t>(limits.tileWidth) * static_cast<size_t>(limits.tileHeight);
    std::vector<uint8_t> rgba(n * 4);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = limits.tileArgb[i];
        const uint32_t a = c >> 24;
        rgba[i * 4 + 0] = static_cast<uint8_t>(((c >> 16) & 0xFFu) * a / 255u);
        rgba[i * 4 + 1] = static_cast<uint8_t>(((c >> 8) & 0xFFu) * a / 255u);
        rgba[i * 4 + 2] = static_cast<uint8_t>((c & 0xFFu) * a / 255u);
        rgba[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    GLint boundTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    glGenTextures(1, &markTexture_);
    glBindTexture(GL_TEXTURE_2D, markTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // The shell's tile is a power of two on both sides, so GL_REPEAT is legal everywhere.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, limits.tileWidth, limits.tileHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundTexture));
    markContext_ = eglGetCurrentContext();
}

void TakeSurface::drawWatermark(const output::TakeLimits& limits, int width, int height) {
    if (markProgram_ == 0 || markTexture_ == 0) return;
    // The tile was drawn for a frame whose short side is tileForShortSide; a smaller take (an encoder that
    // refused the screen's size) scales the pattern with it, so the density matches a render's.
    const float scale = static_cast<float>(std::min(width, height)) / static_cast<float>(limits.tileForShortSide);
    const float tileW = static_cast<float>(limits.tileWidth) * scale;
    const float tileH = static_cast<float>(limits.tileHeight) * scale;
    GLint program = 0;
    GLint vao = 0;
    GLint texture = 0;
    GLint activeUnit = 0;
    GLint srcRgb = 0;
    GLint dstRgb = 0;
    GLint srcAlpha = 0;
    GLint dstAlpha = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(markProgram_);
    glUniform2f(markRepeat_, static_cast<float>(width) / tileW, static_cast<float>(height) / tileH);
    glBindTexture(GL_TEXTURE_2D, markTexture_);
    glBindVertexArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(static_cast<GLuint>(vao));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glActiveTexture(static_cast<GLenum>(activeUnit));
    glUseProgram(static_cast<GLuint>(program));
    glBlendFuncSeparate(static_cast<GLenum>(srcRgb), static_cast<GLenum>(dstRgb), static_cast<GLenum>(srcAlpha),
                        static_cast<GLenum>(dstAlpha));
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
}

void TakeSurface::dropWatermark() {
    // GL names belong to the context that made them: delete them only there. A lost context took them with it,
    // and a contextless teardown cannot delete anything.
    if (markContext_ != EGL_NO_CONTEXT && eglGetCurrentContext() == markContext_) {
        if (markTexture_) glDeleteTextures(1, &markTexture_);
        if (markProgram_) glDeleteProgram(markProgram_);
    }
    markTexture_ = 0;
    markProgram_ = 0;
    markRepeat_ = -1;
    markContext_ = EGL_NO_CONTEXT;
}

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
    dropWatermark();
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
        if (!failed_) makeWatermark(take->limits());
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
        // A trial take carries the full-frame wordmark like a trial render, drawn into the take alone.
        drawWatermark(take->limits(), size.width, size.height);
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
