#pragma once
#include <GLES3/gl3.h>

namespace geode::viz {

// Port of GlUtil.FullscreenTriangle: one VAO/VBO holding the (-1,-1) (3,-1) (-1,3) triangle on attribute 0.
class FullscreenTriangle {
public:
    ~FullscreenTriangle() { release(); }
    GLuint vao() const { return vao_; }
    void create();
    void bind() const { glBindVertexArray(vao_); }
    void unbind() const { glBindVertexArray(0); }
    void draw();
    void release();
    void forget();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

// Port of GlUtil.resetFrameState: the fixed-function state every frame starts from.
void resetFrameState();

}  // namespace geode::viz
