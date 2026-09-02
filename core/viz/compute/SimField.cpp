#include "viz/compute/SimField.hpp"

#include <utility>

#include "util/Log.hpp"

namespace geode::viz::sim {

namespace {
constexpr const char* kTag = "SimField";
}

bool SimField::ensure(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (ok() && width_ == w && height_ == h) return true;
    // Allocation binds framebuffers mid-step, so the caller's draw binding is restored afterwards.
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    release();
    front_ = allocate(w, h);
    back_ = allocate(w, h);
    const bool allocated = front_.ok() && back_.ok();
    if (allocated) {
        width_ = w;
        height_ = h;
        clear();
    } else {
        GEODE_LOGW(kTag, "%s: could not allocate a %dx%d state pair", label_.c_str(), w, h);
        release();
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
    return allocated;
}

void SimField::swap() { std::swap(front_, back_); }

void SimField::clear() {
    if (!ok()) return;
    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    clearSide(front_);
    clearSide(back_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));
}

void SimField::release() {
    releaseSide(front_);
    releaseSide(back_);
    width_ = 0;
    height_ = 0;
}

void SimField::forget() {
    front_ = Side{};
    back_ = Side{};
    width_ = 0;
    height_ = 0;
}

void SimField::clearSide(const Side& side) const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, side.framebuffer);
    if (imageFormatInfo(format_).integerTexels) {
        const GLuint zero[4] = {0, 0, 0, 0};
        glClearBufferuiv(GL_COLOR, 0, zero);
    } else {
        const GLfloat zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, zero);
    }
}

SimField::Side SimField::allocate(int w, int h) const {
    Side side;
    glGenTextures(1, &side.texture);
    glBindTexture(GL_TEXTURE_2D, side.texture);
    // NEAREST unless proven filterable: LINEAR on an integer texture leaves it incomplete and sampling zero.
    const GLint filter = filterable_ ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, imageFormatInfo(format_).internalFormat, w, h);
    glGenFramebuffers(1, &side.framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, side.framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, side.texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) {
        GEODE_LOGW(kTag, "%s: framebuffer incomplete at %dx%d", label_.c_str(), w, h);
        releaseSide(side);
    }
    return side;
}

void SimField::releaseSide(Side& side) {
    if (side.framebuffer != 0) glDeleteFramebuffers(1, &side.framebuffer);
    if (side.texture != 0) glDeleteTextures(1, &side.texture);
    side.framebuffer = 0;
    side.texture = 0;
}

}  // namespace geode::viz::sim
