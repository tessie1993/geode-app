#include "viz/frame/EdgeGlowPass.hpp"

namespace geode::viz::frame {

bool EdgeGlowPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert,
                          std::string* error) {
    release();
    const auto frag = assets.load("edge_glow_frag.glsl", error);
    if (!frag) return false;
    program_ = program::build(fadeVert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    // An empty vertex array of its own: fade_vert makes the triangle from gl_VertexID, so nothing is read
    // from it, but drawing with a generated VAO bound (as UiBlurPass does with the renderer's quad VAO)
    // does not lean on the default VAO 0, which ES 3.0 keeps and desktop core GL (the host checks) removes.
    glGenVertexArrays(1, &vao_);
    return true;
}

void EdgeGlowPass::setOptions(const edge_glow::Options& options) {
    if (options.enabled && !options_.enabled) envelope_.reset();
    options_ = options;
}

bool EdgeGlowPass::wants(const FrameCtx& ctx) const {
    return options_.enabled && ctx.surface != Surface::Export && ctx.width > 0 && ctx.height > 0;
}

void EdgeGlowPass::prepare(const FrameCtx& ctx) {
    const float level = envelope_.step(ctx.sound, ctx.dt);
    shape_ = edge_glow::shape(edge_glow::glWindow(options_, ctx.width, ctx.height), level);
    prepared_ = true;
}

void EdgeGlowPass::draw(const PassInputs& /*inputs*/, const PassTarget& target) {
    // A driver that would not build the program shows the full visual, as the Kotlin pass did.
    if (program_ == 0 || vao_ == 0 || !prepared_ || target.width <= 0 || target.height <= 0) return;
    prepared_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // Darken colour only; the surface's alpha stays what the composite wrote.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(program_);
    glUniform4f(uniforms_.loc("uWindow"), shape_.window.x, shape_.window.y, shape_.window.w, shape_.window.h);
    glUniform1f(uniforms_.loc("uWidth"), shape_.bandPx);
    glUniform1f(uniforms_.loc("uRadius"), shape_.radiusPx);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void EdgeGlowPass::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    program_ = 0;
    vao_ = 0;
    uniforms_ = UniformCache(0);
    prepared_ = false;
}

}  // namespace geode::viz::frame
