#include "viz/Framebuffer.hpp"

#include <utility>

namespace geode::viz {

namespace {
constexpr GLenum kColorAttachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
}

bool Framebuffer::ensure(int w, int h) {
    if (w <= 0 || h <= 0 || alreadyAt(w, h)) return ok();
    release();
    tex_.reset(gl_gen::texture());
    glBindTexture(GL_TEXTURE_2D, tex_.get());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    fbo_.reset(gl_gen::framebuffer());
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_.get(), 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        width_ = w;
        height_ = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) release();
    return complete;
}

void Framebuffer::release() {
    fbo_.reset();
    tex_.reset();
    width_ = 0;
    height_ = 0;
}

void Framebuffer::swap(Framebuffer& other) {
    fbo_.swap(other.fbo_);
    tex_.swap(other.tex_);
    std::swap(width_, other.width_);
    std::swap(height_, other.height_);
}

void Framebuffer::forget() {
    fbo_.forget();
    tex_.forget();
    width_ = 0;
    height_ = 0;
}

void Framebuffer::discardContents(GLenum bindTarget) const {
    if (!fbo_) return;
    discardColorAttachments(bindTarget, 1);
}

void Framebuffer::discardColorAttachments(GLenum bindTarget, int count) {
    glInvalidateFramebuffer(bindTarget, count, kColorAttachments);
}

}  // namespace geode::viz
