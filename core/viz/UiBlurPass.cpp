#include "viz/UiBlurPass.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

namespace {

// Rounds down / up to a multiple of 4, so the blit chain full -> half -> quarter maps every edge onto
// a whole texel at each level and the blurred copy lines up with the frame exactly.
int floor4(int v) { return v & ~3; }
int ceil4(int v) { return (v + 3) & ~3; }

}  // namespace

bool UiBlurPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error) {
    release();
    const auto blurFrag = assets.load("ui_blur_frag.glsl", error);
    if (!blurFrag) return false;
    const auto maskFrag = assets.load("ui_blur_mask_frag.glsl", error);
    if (!maskFrag) return false;
    blurProgram_ = program::build(fadeVert, *blurFrag, cache, error);
    if (blurProgram_ == 0) return false;
    maskProgram_ = program::build(fadeVert, *maskFrag, cache, error);
    if (maskProgram_ == 0) {
        release();
        return false;
    }
    blurUniforms_ = UniformCache(blurProgram_);
    maskUniforms_ = UniformCache(maskProgram_);
    return true;
}

void UiBlurPass::release() {
    if (blurProgram_ != 0) glDeleteProgram(blurProgram_);
    if (maskProgram_ != 0) glDeleteProgram(maskProgram_);
    blurProgram_ = 0;
    maskProgram_ = 0;
    blurUniforms_ = UniformCache(0);
    maskUniforms_ = UniformCache(0);
    half_.release();
    quarterA_.release();
    quarterB_.release();
}

void UiBlurPass::releaseTargets() {
    half_.release();
    quarterA_.release();
    quarterB_.release();
}

void UiBlurPass::draw(const UiBlurRegion& region, GLuint targetFbo, int width, int height, GLuint quadVao) {
    if (blurProgram_ == 0 || maskProgram_ == 0 || !region.active() || width < 8 || height < 8) return;
    const int qw = width / 4;
    const int qh = height / 4;
    // The textures span the whole viewport, so a sheet that slides or resizes never reallocates them.
    if (!half_.ensure(qw * 2, qh * 2) || !quarterA_.ensure(qw, qh) || !quarterB_.ensure(qw, qh)) return;

    // The rect in window pixels, GL y-up (the region is y-down, as Android lays out views).
    const auto px = [](float v, int size) { return static_cast<float>(size) * std::clamp(v, 0.0f, 1.0f); };
    const float fx0 = px(region.left, width);
    const float fx1 = px(region.right, width);
    const float fy0 = static_cast<float>(height) - px(region.bottom, height);
    const float fy1 = static_cast<float>(height) - px(region.top, height);
    const int sx0 = std::clamp(static_cast<int>(std::floor(fx0)), 0, width);
    const int sy0 = std::clamp(static_cast<int>(std::floor(fy0)), 0, height);
    const int sx1 = std::clamp(static_cast<int>(std::ceil(fx1)), 0, width);
    const int sy1 = std::clamp(static_cast<int>(std::ceil(fy1)), 0, height);
    if (sx1 <= sx0 || sy1 <= sy0) return;

    // The source: the rect plus the margin, 4-aligned, inside the part of the frame the quarter
    // texture covers (a frame not a multiple of 4 wide loses at most 3 pixels at its far edges, which
    // the clamp-to-edge sampler repeats).
    const int x0 = floor4(std::max(sx0 - kMarginPx, 0));
    const int y0 = floor4(std::max(sy0 - kMarginPx, 0));
    const int x1 = std::min(ceil4(sx1 + kMarginPx), qw * 4);
    const int y1 = std::min(ceil4(sy1 + kMarginPx), qh * 4);
    if (x1 <= x0 || y1 <= y0) return;

    // The scissor clips a blit too, so it is off for the two copies.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, half_.fbo());
    glBlitFramebuffer(x0, y0, x1, y1, x0 / 2, y0 / 2, x1 / 2, y1 / 2, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, half_.fbo());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, quarterA_.fbo());
    glBlitFramebuffer(x0 / 2, y0 / 2, x1 / 2, y1 / 2, x0 / 4, y0 / 4, x1 / 4, y1 / 4, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Separable Gaussian at quarter resolution, A -> B horizontally, B -> A vertically.
    glUseProgram(blurProgram_);
    glUniform1i(blurUniforms_.loc("uSrc"), 0);
    const GLint stepLoc = blurUniforms_.loc("uStep");
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, qw, qh);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0 / 4, y0 / 4, (x1 - x0) / 4, (y1 - y0) / 4);
    glBindVertexArray(quadVao);
    const float stepX = 1.0f / static_cast<float>(qw);
    const float stepY = 1.0f / static_cast<float>(qh);
    for (int i = 0; i < kIterations; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, quarterB_.fbo());
        glBindTexture(GL_TEXTURE_2D, quarterA_.tex());
        glUniform2f(stepLoc, stepX, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, quarterA_.fbo());
        glBindTexture(GL_TEXTURE_2D, quarterB_.tex());
        glUniform2f(stepLoc, 0.0f, stepY);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Back over the finished frame, inside the rounded rect only.
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width, height);
    glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
    glUseProgram(maskProgram_);
    glBindTexture(GL_TEXTURE_2D, quarterA_.tex());
    glUniform1i(maskUniforms_.loc("uBlur"), 0);
    glUniform4f(maskUniforms_.loc("uRect"), fx0, fy0, fx1, fy1);
    glUniform2f(maskUniforms_.loc("uTexScale"), 1.0f / (4.0f * static_cast<float>(qw)), 1.0f / (4.0f * static_cast<float>(qh)));
    glUniform1f(maskUniforms_.loc("uCorner"), std::max(region.cornerPx, 0.0f));
    glUniform1f(maskUniforms_.loc("uStrength"), std::clamp(region.strength, 0.0f, 1.0f));
    glEnable(GL_BLEND);
    // Over on colour; the destination's alpha stays as the composite left it.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
}

}  // namespace geode::viz
