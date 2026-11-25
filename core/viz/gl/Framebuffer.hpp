#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/gl/GlHandle.hpp"

namespace geode::viz {

// One RGBA8 colour texture behind one FBO, cleared to opaque black on creation. Owns both (GlHandle).
class Framebuffer {
public:
    explicit Framebuffer(std::string label) : label_(std::move(label)) {}
    ~Framebuffer() { release(); }
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    GLuint fbo() const { return fbo_.get(); }
    GLuint tex() const { return tex_.get(); }
    int width() const { return width_; }
    int height() const { return height_; }
    bool ok() const { return fbo_ && tex_; }
    const std::string& label() const { return label_; }

    bool ensure(int w, int h);
    void release();
    void forget();
    // Trades the texture and FBO with `other` (the labels stay): a pass that needs last frame's picture takes
    // it without copying a pixel.
    void swap(Framebuffer& other);
    void discardContents(GLenum bindTarget = GL_FRAMEBUFFER) const;
    static void discardColorAttachments(GLenum bindTarget, int count);

private:
    bool alreadyAt(int w, int h) const { return ok() && width_ == w && height_ == h; }

    std::string label_;
    GlFramebuffer fbo_;
    GlTexture tex_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
