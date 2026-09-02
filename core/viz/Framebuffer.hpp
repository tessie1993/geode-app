#pragma once
#include <GLES3/gl3.h>

#include <string>

namespace geode::viz {

// Port of RenderTarget.kt: one RGBA8 colour texture behind one FBO, cleared to opaque black on creation.
class Framebuffer {
public:
    explicit Framebuffer(std::string label) : label_(std::move(label)) {}
    ~Framebuffer() { release(); }
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    GLuint fbo() const { return fbo_; }
    GLuint tex() const { return tex_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool ok() const { return fbo_ != 0 && tex_ != 0; }
    const std::string& label() const { return label_; }

    bool ensure(int w, int h);
    void release();
    void forget();
    void discardContents(GLenum bindTarget = GL_FRAMEBUFFER) const;
    static void discardColorAttachments(GLenum bindTarget, int count);

private:
    bool alreadyAt(int w, int h) const { return ok() && width_ == w && height_ == h; }

    std::string label_;
    GLuint fbo_ = 0;
    GLuint tex_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
