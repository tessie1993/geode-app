#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Compute.hpp"

namespace geode::viz::sim {

// Port of SimField.kt: the ping-pong state pair, allocated on demand and never mipmapped.
class SimField {
public:
    SimField(std::string label, GlImageFormat format, bool filterable) : label_(std::move(label)), format_(format), filterable_(filterable) {}
    ~SimField() { release(); }
    SimField(const SimField&) = delete;
    SimField& operator=(const SimField&) = delete;

    int width() const { return width_; }
    int height() const { return height_; }
    GLuint readTexture() const { return front_.texture; }
    GLuint writeTexture() const { return back_.texture; }
    GLuint writeFramebuffer() const { return back_.framebuffer; }
    bool ok() const { return front_.ok() && back_.ok(); }

    bool ensure(int w, int h);
    void swap();
    void clear();
    void release();
    void forget();

private:
    struct Side {
        GLuint texture = 0;
        GLuint framebuffer = 0;
        bool ok() const { return texture != 0 && framebuffer != 0; }
    };

    Side allocate(int w, int h) const;
    void clearSide(const Side& side) const;
    static void releaseSide(Side& side);

    std::string label_;
    GlImageFormat format_;
    bool filterable_;
    Side front_;
    Side back_;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz::sim
