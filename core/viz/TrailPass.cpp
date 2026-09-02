#include "viz/TrailPass.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

bool TrailPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error) {
    release();
    trail_.forget();
    const auto fadeFrag = assets.load("fade_frag.glsl", error);
    const auto warpFrag = assets.load("trail_warp_frag.glsl", error);
    if (!fadeFrag || !warpFrag) return false;
    fadeProgram_ = program::build(fadeVert, *fadeFrag, cache, error);
    if (fadeProgram_ == 0) return false;
    trailWarpProgram_ = program::build(fadeVert, *warpFrag, cache, error);
    if (trailWarpProgram_ == 0) return false;
    fadeUniforms_ = UniformCache(fadeProgram_);
    trailUniforms_ = UniformCache(trailWarpProgram_);
    return true;
}

void TrailPass::release() {
    if (fadeProgram_ != 0) glDeleteProgram(fadeProgram_);
    if (trailWarpProgram_ != 0) glDeleteProgram(trailWarpProgram_);
    fadeProgram_ = 0;
    trailWarpProgram_ = 0;
    trail_.release();
}

float TrailPass::fadeAlpha(float keep, float dt) { return 1.0f - std::pow(keep * 0.97f, dt * 60.0f); }

float TrailPass::warpDecay(float retention, float dt) { return std::pow(std::clamp(retention * 0.97f + 0.02f, 0.0f, 0.99f), dt * 60.0f); }

void TrailPass::apply(const SceneParams& p, float keep, float timeSeconds, float dt, const Framebuffer& sceneTarget, GLuint quadVao,
                      int renderWidth, int renderHeight) {
    if (p.trailZoom != 0.0f || p.trailWarp > 0.0f) {
        drawTrailWarp(p, keep, timeSeconds, dt, sceneTarget, quadVao, renderWidth, renderHeight);
    } else {
        drawFadeQuad(fadeAlpha(keep, dt), quadVao);
    }
}

void TrailPass::drawTrailWarp(const SceneParams& p, float retention, float timeSeconds, float dt, const Framebuffer& sceneTarget,
                              GLuint quadVao, int renderWidth, int renderHeight) {
    if (!trail_.ensure(renderWidth, renderHeight)) {
        drawFadeQuad(fadeAlpha(retention, dt), quadVao);
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneTarget.fbo());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, trail_.fbo());
    trail_.discardContents(GL_DRAW_FRAMEBUFFER);
    glBlitFramebuffer(0, 0, renderWidth, renderHeight, 0, 0, renderWidth, renderHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneTarget.fbo());
    glViewport(0, 0, renderWidth, renderHeight);
    glDisable(GL_BLEND);
    sceneTarget.discardContents();
    glUseProgram(trailWarpProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, trail_.tex());
    glUniform1i(trailUniforms_.loc("uPrev"), 0);
    glUniform1f(trailUniforms_.loc("uDecay"), warpDecay(retention, dt));
    glUniform1f(trailUniforms_.loc("uZoom"), p.trailZoom);
    glUniform1f(trailUniforms_.loc("uWarp"), p.trailWarp);
    glUniform1f(trailUniforms_.loc("uTime"), timeSeconds);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void TrailPass::drawFadeQuad(float alpha, GLuint quadVao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(fadeProgram_);
    glUniform1f(fadeUniforms_.loc("uFadeAlpha"), std::clamp(alpha, 0.02f, 1.0f));
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace geode::viz
