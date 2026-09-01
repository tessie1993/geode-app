#include "viz/Quad.hpp"

namespace geode::viz {

void FullscreenTriangle::create() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    const float quad[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}

void FullscreenTriangle::draw() {
    if (vao_ == 0) create();
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void FullscreenTriangle::release() {
    if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    vbo_ = 0;
    vao_ = 0;
}

void FullscreenTriangle::forget() {
    vao_ = 0;
    vbo_ = 0;
}

void resetFrameState() {
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFFFFFFFFu);
    glBlendEquation(GL_FUNC_ADD);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (GLuint unit = 0; unit <= 7; unit++) glBindSampler(unit, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
}

}  // namespace geode::viz
