#include "viz/DivePass.hpp"

#include <algorithm>

namespace geode::viz {

bool DivePass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error) {
    release();
    const auto frag = assets.load("dive_frag.glsl", error);
    if (!frag) return false;
    program_ = program::build(vert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    return true;
}

void DivePass::release() {
    target_.release();
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
}

GLuint DivePass::run(GLuint sceneTex, int srcWidth, int srcHeight, int width, int height, float phase, float mix,
                     const endless::tunnel::Look& look, GLuint quadVao) {
    if (program_ == 0 || sceneTex == 0 || !target_.ensure(std::max(width, 1), std::max(height, 1))) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, target_.fbo());
    // Every pixel is written below: a tile GPU must not load the old contents back first.
    target_.discardContents();
    glViewport(0, 0, target_.width(), target_.height());
    glDisable(GL_BLEND);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(uniforms_.loc("uScene"), 0);
    glUniform2f(uniforms_.loc("uTexel"), 1.0f / static_cast<float>(std::max(srcWidth, 1)),
                1.0f / static_cast<float>(std::max(srcHeight, 1)));
    glUniform1f(uniforms_.loc("uPhase"), phase);
    glUniform1f(uniforms_.loc("uMix"), mix);
    glUniform1f(uniforms_.loc("uAspect"), look.aspect);
    glUniform1i(uniforms_.loc("uInner"), look.inner);
    glUniform1i(uniforms_.loc("uBase"), look.base);
    glUniform1f(uniforms_.loc("uStep"), look.step);
    glUniform1f(uniforms_.loc("uTwist"), look.twist);
    glUniform2f(uniforms_.loc("uCentre"), look.cx, look.cy);
    glUniform1f(uniforms_.loc("uShape"), look.shape);
    glUniform1f(uniforms_.loc("uBlend"), look.blend);
    glUniform1f(uniforms_.loc("uFog"), look.fog);
    glUniform1f(uniforms_.loc("uLens"), look.lens);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return target_.tex();
}

}  // namespace geode::viz
