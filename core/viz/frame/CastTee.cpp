#include "viz/frame/CastTee.hpp"

#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>

#include <utility>

namespace geode::viz::frame {

namespace {

// The extension entry points, looked up once. Every Android 8+ driver has them; a missing one makes the tee
// refuse (ok() false) and the cast renders on its own as before.
using GetClientBufferFn = EGLClientBuffer (*)(const AHardwareBuffer*);
using CreateImageFn = EGLImageKHR (*)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint*);
using DestroyImageFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR);
using CreateSyncFn = EGLSyncKHR (*)(EGLDisplay, EGLenum, const EGLint*);
using DestroySyncFn = EGLBoolean (*)(EGLDisplay, EGLSyncKHR);
using ClientWaitSyncFn = EGLint (*)(EGLDisplay, EGLSyncKHR, EGLint, EGLTimeKHR);
using WaitSyncFn = EGLint (*)(EGLDisplay, EGLSyncKHR, EGLint);
using ImageTargetTextureFn = void (*)(GLenum, GLeglImageOES);

struct Ext {
    GetClientBufferFn clientBuffer = nullptr;
    CreateImageFn createImage = nullptr;
    DestroyImageFn destroyImage = nullptr;
    CreateSyncFn createSync = nullptr;
    DestroySyncFn destroySync = nullptr;
    ClientWaitSyncFn clientWait = nullptr;
    WaitSyncFn serverWait = nullptr;   // EGL_KHR_wait_sync; optional
    ImageTargetTextureFn imageTexture = nullptr;

    bool usable() const {
        return clientBuffer && createImage && destroyImage && createSync && destroySync && clientWait && imageTexture;
    }
};

template <typename F>
F proc(const char* name) {
    return reinterpret_cast<F>(eglGetProcAddress(name));
}

const Ext& ext() {
    static const Ext e = [] {
        Ext x;
        x.clientBuffer = proc<GetClientBufferFn>("eglGetNativeClientBufferANDROID");
        x.createImage = proc<CreateImageFn>("eglCreateImageKHR");
        x.destroyImage = proc<DestroyImageFn>("eglDestroyImageKHR");
        x.createSync = proc<CreateSyncFn>("eglCreateSyncKHR");
        x.destroySync = proc<DestroySyncFn>("eglDestroySyncKHR");
        x.clientWait = proc<ClientWaitSyncFn>("eglClientWaitSyncKHR");
        x.serverWait = proc<WaitSyncFn>("eglWaitSyncKHR");
        x.imageTexture = proc<ImageTargetTextureFn>("glEGLImageTargetTexture2DOES");
        return x;
    }();
    return e;
}

// Without a GPU-side wait, the CPU waits this long for the other context's work at most (it is one blit).
constexpr EGLTimeKHR kClientWaitNs = 20'000'000;

std::mutex& currentLock() {
    static std::mutex m;
    return m;
}

std::shared_ptr<CastTee>& currentTee() {
    static std::shared_ptr<CastTee> t;
    return t;
}

}  // namespace

CastTee::CastTee(int width, int height, int fps)
    : width_(width), height_(height), side_(width > height ? width : height), fps_(fps > 0 ? fps : 30) {
    const Ext& e = ext();
    display_ = eglGetCurrentDisplay();
    if (display_ == EGL_NO_DISPLAY) display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!e.usable() || display_ == EGL_NO_DISPLAY || width_ <= 0 || height_ <= 0) return;
    AHardwareBuffer_Desc desc{};
    // Square: a slot holds the frame's size, or its transpose for a stage that is turned (tee::place).
    desc.width = static_cast<uint32_t>(side_);
    desc.height = static_cast<uint32_t>(side_);
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;
    const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    for (int i = 0; i < tee::kSlots; ++i) {
        if (AHardwareBuffer_allocate(&desc, &buffers_[i]) != 0 || !buffers_[i]) return;
        const EGLClientBuffer client = e.clientBuffer(buffers_[i]);
        if (!client) return;
        const EGLImageKHR image = e.createImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attrs);
        if (image == EGL_NO_IMAGE_KHR) return;
        images_[i] = image;
    }
    ok_ = true;
}

CastTee::~CastTee() {
    const Ext& e = ext();
    for (int i = 0; i < tee::kSlots; ++i) {
        if (written_[i] && e.destroySync) e.destroySync(display_, written_[i]);
        if (read_[i] && e.destroySync) e.destroySync(display_, read_[i]);
        // A texture still bound to the image in some context keeps its storage (EGLImage orphaning).
        if (images_[i] && e.destroyImage) e.destroyImage(display_, images_[i]);
        if (buffers_[i]) AHardwareBuffer_release(buffers_[i]);
    }
}

std::shared_ptr<CastTee> CastTee::current() {
    std::lock_guard<std::mutex> lock(currentLock());
    return currentTee();
}

void CastTee::setCurrent(std::shared_ptr<CastTee> tee) {
    std::lock_guard<std::mutex> lock(currentLock());
    currentTee() = std::move(tee);
}

bool CastTee::settle(void*& fence, int64_t timeoutNs) {
    if (!fence) return true;
    const Ext& e = ext();
    if (e.serverWait) {
        // The GPU waits for the other context's blit; the CPU goes on. The fence may go at once (EGL keeps it
        // until no wait refers to it).
        e.serverWait(display_, fence, 0);
    } else if (e.clientWait(display_, fence, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, static_cast<EGLTimeKHR>(timeoutNs)) ==
               EGL_TIMEOUT_EXPIRED_KHR) {
        return false;
    }
    e.destroySync(display_, fence);
    fence = nullptr;
    return true;
}

void CastTee::forget(Side& side) {
    // The names belonged to another (or a lost) context: nothing to delete from here.
    side.context = EGL_NO_CONTEXT;
    for (int i = 0; i < tee::kSlots; ++i) {
        side.texture[i] = 0;
        side.framebuffer[i] = 0;
    }
}

void CastTee::freeSide(Side& side) {
    if (side.context != EGL_NO_CONTEXT && side.context == eglGetCurrentContext()) {
        glDeleteFramebuffers(tee::kSlots, side.framebuffer);
        glDeleteTextures(tee::kSlots, side.texture);
    }
    forget(side);
}

void CastTee::releaseReader() {
    if (turn_.program != 0 && turn_.context == eglGetCurrentContext()) {
        glDeleteProgram(turn_.program);
        glDeleteVertexArrays(1, &turn_.vao);
    }
    turn_ = {};
    freeSide(reader_);
}

void CastTee::releaseWriter() { freeSide(writer_); }

bool CastTee::bindSide(Side& side, int slot) {
    const EGLContext context = eglGetCurrentContext();
    if (context == EGL_NO_CONTEXT || eglGetCurrentDisplay() != display_) return false;
    if (side.context != context) {
        forget(side);
        side.context = context;
    }
    if (side.framebuffer[slot] != 0) return true;
    GLint texture = 0;
    GLint framebuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    ext().imageTexture(GL_TEXTURE_2D, static_cast<GLeglImageOES>(images_[slot]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    if (!complete) {
        glDeleteFramebuffers(1, &fb);
        glDeleteTextures(1, &tex);
        return false;
    }
    side.texture[slot] = tex;
    side.framebuffer[slot] = fb;
    return true;
}

void CastTee::publish(GLuint sourceFbo, int sourceWidth, int sourceHeight, int64_t nowNs) {
    if (!ok_ || sourceWidth <= 0 || sourceHeight <= 0) return;
    int slot = -1;
    void* readFence = nullptr;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (!tee::publishDue(box_.published, box_.lastPublishNs, nowNs, fps_)) return;
        slot = box_.beginWrite();
        if (slot < 0) return;
        readFence = std::exchange(read_[slot], nullptr);
    }
    // The cast's last blit out of this slot must be done before it is drawn over. Never a long wait here: this
    // is the stage's frame; a copy that cannot go now is skipped.
    if (!settle(readFence, 2'000'000) || !bindSide(writer_, slot)) {
        std::lock_guard<std::mutex> lock(lock_);
        if (readFence) read_[slot] = readFence;
        box_.abandon(slot);
        return;
    }
    GLint readFb = 0;
    GLint drawFb = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
    glGetIntegerv(GL_VIEWPORT, viewport);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, writer_.framebuffer[slot]);
    const tee::Placement place = tee::place(sourceWidth, sourceHeight, width_, height_);
    glViewport(0, 0, place.slotW, place.slotH);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    const tee::Rect r = place.crop;
    glBlitFramebuffer(r.x0, r.y0, r.x1, r.y1, 0, 0, place.slotW, place.slotH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFb));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFb));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    void* fence = ext().createSync(display_, EGL_SYNC_FENCE_KHR, nullptr);
    glFlush();
    std::lock_guard<std::mutex> lock(lock_);
    if (written_[slot]) ext().destroySync(display_, written_[slot]);
    written_[slot] = fence == EGL_NO_SYNC_KHR ? nullptr : fence;
    turned_[slot] = place.turned;
    box_.publish(slot, nowNs);
}

namespace {

// One full-frame triangle; vUv is the frame's 0..1, v up.
constexpr const char* kTurnVert = R"(#version 300 es
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// tee::turnedSource: the slot's top on the frame's left. uScale maps 0..1 onto the slot's used corner, uMax keeps
// the filter off the texels past it.
constexpr const char* kTurnFrag = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
uniform vec2 uScale;
uniform vec2 uMax;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec2 st = vec2(vUv.y, 1.0 - vUv.x);
    fragColor = vec4(texture(uTex, min(st * uScale, uMax)).rgb, 1.0);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_FALSE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

}  // namespace

bool CastTee::buildTurn() {
    const EGLContext context = eglGetCurrentContext();
    if (turn_.context == context && turn_.program != 0) return true;
    if (turn_.context == context && turn_.failed) return false;
    // A program from another (or a lost) context is not this one's to delete.
    turn_ = {};
    turn_.context = context;
    const GLuint vert = compileShader(GL_VERTEX_SHADER, kTurnVert);
    const GLuint frag = compileShader(GL_FRAGMENT_SHADER, kTurnFrag);
    GLuint program = 0;
    if (vert != 0 && frag != 0) {
        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE) {
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vert != 0) glDeleteShader(vert);
    if (frag != 0) glDeleteShader(frag);
    if (program == 0) {
        turn_.failed = true;
        return false;
    }
    turn_.program = program;
    turn_.tex = glGetUniformLocation(program, "uTex");
    turn_.scale = glGetUniformLocation(program, "uScale");
    turn_.max = glGetUniformLocation(program, "uMax");
    glGenVertexArrays(1, &turn_.vao);
    return true;
}

bool CastTee::drawTurned(int slot) {
    if (!buildTurn()) return false;
    GLint program = 0;
    GLint vao = 0;
    GLint active = 0;
    GLint texture = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glUseProgram(turn_.program);
    glBindVertexArray(turn_.vao);
    glBindTexture(GL_TEXTURE_2D, reader_.texture[slot]);
    // The slot holds the picture unturned at height_ x width_ in its bottom-left corner.
    const float side = static_cast<float>(side_);
    const float sx = static_cast<float>(height_) / side;
    const float sy = static_cast<float>(width_) / side;
    glUniform1i(turn_.tex, 0);
    glUniform2f(turn_.scale, sx, sy);
    glUniform2f(turn_.max, sx - 0.5f / side, sy - 0.5f / side);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glActiveTexture(static_cast<GLenum>(active));
    glBindVertexArray(static_cast<GLuint>(vao));
    glUseProgram(static_cast<GLuint>(program));
    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    return true;
}

bool CastTee::draw(int64_t nowNs, int width, int height) {
    if (!ok_ || width <= 0 || height <= 0) return false;
    int slot = -1;
    void* writeFence = nullptr;
    bool turned = false;
    {
        std::lock_guard<std::mutex> lock(lock_);
        slot = box_.acquire(nowNs);
        if (slot < 0) return false;
        writeFence = std::exchange(written_[slot], nullptr);
        turned = turned_[slot];
    }
    // The stage's copy into this slot must be done before the encoder gets it (a GPU wait where the driver has
    // one; else up to kClientWaitNs, then the slot is read anyway: a torn frame beats a dropped one here).
    if (!settle(writeFence, static_cast<int64_t>(kClientWaitNs))) ext().destroySync(display_, writeFence);
    if (!bindSide(reader_, slot)) {
        std::lock_guard<std::mutex> lock(lock_);
        box_.release(slot);
        return false;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    if (turned) {
        if (!drawTurned(slot)) {
            // The turn's program would not build: the cast's own renderer draws rather than a stretched picture.
            std::lock_guard<std::mutex> lock(lock_);
            box_.release(slot);
            return false;
        }
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, reader_.framebuffer[slot]);
        const GLenum filter = width == width_ && height == height_ ? GL_NEAREST : GL_LINEAR;
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, filter);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    void* fence = ext().createSync(display_, EGL_SYNC_FENCE_KHR, nullptr);
    glFlush();
    std::lock_guard<std::mutex> lock(lock_);
    if (read_[slot]) ext().destroySync(display_, read_[slot]);
    read_[slot] = fence == EGL_NO_SYNC_KHR ? nullptr : fence;
    box_.release(slot);
    return true;
}

}  // namespace geode::viz::frame
