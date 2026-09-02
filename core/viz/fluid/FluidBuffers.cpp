#include "viz/fluid/FluidBuffers.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"
#include "viz/Framebuffer.hpp"

namespace geode::viz::fluid {

namespace {

constexpr const char* kTag = "FluidSim";

bool renderable(TexFormat f) {
    GLuint tex = 0;
    GLuint fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(f.internal), 4, 4, 0, f.format, f.type, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return ok;
}

void setupTexture(GLuint tex, int w, int h, TexFormat fmt, GLint filter) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fmt.internal), w, h, 0, fmt.format, fmt.type, nullptr);
}

}  // namespace

Formats probeFormats() {
    const TexFormat rgba{GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    const TexFormat rg{GL_RG16F, GL_RG, GL_HALF_FLOAT};
    const TexFormat r{GL_R16F, GL_RED, GL_HALF_FLOAT};
    const TexFormat rgba32{GL_RGBA32F, GL_RGBA, GL_FLOAT};
    const bool rgba32Ok = renderable(rgba32);
    const bool rgbaOk = renderable(rgba);
    const bool rgOk = renderable(rg);
    const bool rOk = renderable(r);
    Formats out;
    out.r = rOk ? r : (rgOk ? rg : rgba);
    out.rg = rgOk ? rg : rgba;
    out.rgba = rgba;
    out.rgba32 = rgba32;
    out.hasRgba32 = rgba32Ok;
    out.ok = rgbaOk;
    GEODE_LOGI(kTag, "fluid formats: R16F=%s RG16F=%s RGBA16F=%s RGBA32F=%s", rOk ? "ok" : "fb", rgOk ? "ok" : "fb",
               rgbaOk ? "ok" : "MISSING", rgba32Ok ? "ok" : "no");
    return out;
}

std::pair<int, int> resolution(int res, int width, int height) {
    if (width <= 0 || height <= 0) return {res, res};
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    if (aspect >= 1.0f) return {std::max(static_cast<int>(std::lround(res * aspect)), 2), res};
    return {res, std::max(static_cast<int>(std::lround(res / aspect)), 2)};
}

Fbo& Fbo::operator=(Fbo&& o) noexcept {
    if (this != &o) {
        release();
        width_ = o.width_;
        height_ = o.height_;
        fmt_ = o.fmt_;
        linear_ = o.linear_;
        fbo_ = o.fbo_;
        tex_ = o.tex_;
        o.fbo_ = 0;
        o.tex_ = 0;
    }
    return *this;
}

void Fbo::create() {
    glGenTextures(1, &tex_);
    setupTexture(tex_, width_, height_, fmt_, linear_ ? GL_LINEAR : GL_NEAREST);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        GEODE_LOGW(kTag, "FBO incomplete (%dx%d fmt=0x%x)", width_, height_, static_cast<unsigned>(fmt_.internal));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        release();
        return;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Fbo::discardContents() const {
    if (fbo_ != 0) Framebuffer::discardColorAttachments(GL_FRAMEBUFFER, 1);
}

void Fbo::release() {
    if (tex_ != 0) glDeleteTextures(1, &tex_);
    if (fbo_ != 0) glDeleteFramebuffers(1, &fbo_);
    tex_ = 0;
    fbo_ = 0;
}

GLuint DoubleMrt::Side::makeTex(int w, int h, TexFormat fmt) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    setupTexture(tex, w, h, fmt, GL_NEAREST);
    return tex;
}

void DoubleMrt::Side::create() {
    texA_ = makeTex(width_, height_, fmtA_);
    texB_ = makeTex(width_, height_, fmtB_);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texA_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texB_, 0);
    const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, buffers);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        GEODE_LOGW(kTag, "MRT FBO incomplete (%dx%d)", width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        release();
        return;
    }
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DoubleMrt::Side::discardContents() const {
    if (fbo_ != 0) Framebuffer::discardColorAttachments(GL_FRAMEBUFFER, 2);
}

void DoubleMrt::Side::release() {
    if (texA_ != 0) glDeleteTextures(1, &texA_);
    if (texB_ != 0) glDeleteTextures(1, &texB_);
    if (fbo_ != 0) glDeleteFramebuffers(1, &fbo_);
    texA_ = 0;
    texB_ = 0;
    fbo_ = 0;
}

}  // namespace geode::viz::fluid
