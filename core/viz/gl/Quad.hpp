#pragma once
#include <GLES3/gl3.h>

#include "viz/gl/GlHandle.hpp"

namespace geode::viz {

// Port of the old Kotlin GlUtil.FullscreenTriangle (deleted, #603): one VAO/VBO holding the (-1,-1) (3,-1) (-1,3) triangle on attribute 0.
// Owns both names (GlHandle): moves, never copies, and deletes them when it goes unless forget() dropped them.
class FullscreenTriangle {
public:
    FullscreenTriangle() = default;
    FullscreenTriangle(const FullscreenTriangle&) = delete;
    FullscreenTriangle& operator=(const FullscreenTriangle&) = delete;
    FullscreenTriangle(FullscreenTriangle&&) noexcept = default;
    FullscreenTriangle& operator=(FullscreenTriangle&&) noexcept = default;
    ~FullscreenTriangle() = default;

    GLuint vao() const { return vao_.get(); }
    void create();
    void bind() const { glBindVertexArray(vao_.get()); }
    void unbind() const { glBindVertexArray(0); }
    void draw();
    void release();
    void forget();

private:
    // Declared VAO first, so the buffer goes first, as release() deletes them.
    GlVertexArray vao_;
    GlBuffer vbo_;
};

// The fixed-function state every frame starts from.
void resetFrameState();

}  // namespace geode::viz
