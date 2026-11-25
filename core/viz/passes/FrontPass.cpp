#include "viz/passes/FrontPass.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

bool FrontPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, std::string* error) {
    release();
    const auto frag = assets.load("fronts_frag.glsl", error);
    if (!frag) return false;
    uniforms_.reset(program::build(vert, *frag, cache, error));
    return uniforms_.program() != 0;
}

void FrontPass::release() {
    target_.release();
    uniforms_.release();
}

void FrontPass::forget() {
    target_.forget();
    uniforms_.forget();
}

void FrontPass::releaseIdle(float wallDt, float idleSeconds) {
    if (!target_.ok()) return;
    idleSeconds_ += wallDt;
    if (idleSeconds_ >= idleSeconds) target_.release();
}

bool FrontPass::run(GLuint sceneTex, GLuint otherTex, int width, int height, float aspect, const fronts::Packed& packed,
                    const std::array<float, 3 * fronts::kMaxFronts>& colours, GLuint quadVao) {
    if (uniforms_.program() == 0 || sceneTex == 0 || packed.count <= 0 || !target_.ensure(std::max(width, 1), std::max(height, 1))) {
        return false;
    }
    idleSeconds_ = 0.0f;
    glBindFramebuffer(GL_FRAMEBUFFER, target_.fbo());
    // Every pixel is written below: a tile GPU must not load the old contents back first.
    target_.discardContents();
    glViewport(0, 0, target_.width(), target_.height());
    glDisable(GL_BLEND);
    glUseProgram(uniforms_.program());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(uniforms_.loc("uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, otherTex != 0 ? otherTex : sceneTex);
    glUniform1i(uniforms_.loc("uOther"), 1);
    glUniform1f(uniforms_.loc("uHasOther"), otherTex != 0 ? 1.0f : 0.0f);
    glUniform1f(uniforms_.loc("uAspect"), std::isfinite(aspect) ? std::clamp(aspect, 0.1f, 10.0f) : 1.0f);
    glUniform1i(uniforms_.loc("uCount"), packed.count);
    glUniform4fv(uniforms_.loc("uFront"), packed.count * 3, packed.data.data());
    glUniform3fv(uniforms_.loc("uTint"), packed.count, colours.data());
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

}  // namespace geode::viz
