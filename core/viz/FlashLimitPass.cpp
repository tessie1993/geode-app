#include "viz/FlashLimitPass.hpp"

#include <initializer_list>

#include "util/Log.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "GeodeFlashLimit";
constexpr GLenum kStateAttachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
}  // namespace

bool FlashLimitPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error) {
    release();
    const auto lum = assets.load("flash_lum_frag.glsl", error);
    const auto tile = assets.load("flash_tile_frag.glsl", error);
    const auto state = assets.load("flash_state_frag.glsl", error);
    const auto apply = assets.load("flash_apply_frag.glsl", error);
    if (!lum || !tile || !state || !apply) return false;
    lumProgram_ = program::build(fadeVert, *lum, cache, error);
    tileProgram_ = lumProgram_ ? program::build(fadeVert, *tile, cache, error) : 0;
    stateProgram_ = tileProgram_ ? program::build(fadeVert, *state, cache, error) : 0;
    applyProgram_ = stateProgram_ ? program::build(fadeVert, *apply, cache, error) : 0;
    if (applyProgram_ == 0) {
        release();
        return false;
    }
    lumUniforms_ = UniformCache(lumProgram_);
    tileUniforms_ = UniformCache(tileProgram_);
    stateUniforms_ = UniformCache(stateProgram_);
    applyUniforms_ = UniformCache(applyProgram_);
    return true;
}

void FlashLimitPass::release() {
    for (GLuint* p : {&lumProgram_, &tileProgram_, &stateProgram_, &applyProgram_}) {
        if (*p != 0) glDeleteProgram(*p);
        *p = 0;
    }
    lumUniforms_ = UniformCache(0);
    tileUniforms_ = UniformCache(0);
    stateUniforms_ = UniformCache(0);
    applyUniforms_ = UniformCache(0);
    frame_.release();
    cells_.release();
    tiles_.release();
    releaseStates();
    cols_ = 0;
    rows_ = 0;
    width_ = 0;
    height_ = 0;
}

bool FlashLimitPass::ensure(int width, int height) {
    if (applyProgram_ == 0 || width <= 0 || height <= 0) return false;
    if (!frame_.ensure(width, height)) return false;
    width_ = width;
    height_ = height;
    const bool landscape = width >= height;
    const int cols = landscape ? safety::kFlashGridLong : safety::kFlashGridShort;
    const int rows = landscape ? safety::kFlashGridShort : safety::kFlashGridLong;
    if (cols == cols_ && rows == rows_ && states_[0].fbo != 0 && cells_.ok() && tiles_.ok()) return true;
    // A new grid (first frame, or a turn between portrait and landscape): fresh, unprimed state.
    cols_ = cols;
    rows_ = rows;
    if (!cells_.ensure(cols * kCellsPerTile, rows * kCellsPerTile) || !tiles_.ensure(cols, rows) || !createStates()) {
        GEODE_LOGW(kTag, "flash limiter targets failed at %dx%d; drawing unlimited", width, height);
        cols_ = 0;
        rows_ = 0;
        return false;
    }
    return true;
}

bool FlashLimitPass::createStates() {
    releaseStates();
    const GLfloat zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (StateSet& set : states_) {
        glGenTextures(3, set.tex);
        for (GLuint tex : set.tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cols_, rows_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        glGenFramebuffers(1, &set.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, set.fbo);
        for (int i = 0; i < 3; ++i) glFramebufferTexture2D(GL_FRAMEBUFFER, kStateAttachments[i], GL_TEXTURE_2D, set.tex[i], 0);
        glDrawBuffers(3, kStateAttachments);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            releaseStates();
            return false;
        }
        // All zero: not primed, so the first frame is shown as measured.
        for (int i = 0; i < 3; ++i) glClearBufferfv(GL_COLOR, i, zero);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    current_ = 0;
    stepped_ = false;
    return true;
}

void FlashLimitPass::releaseStates() {
    for (StateSet& set : states_) {
        if (set.fbo != 0) glDeleteFramebuffers(1, &set.fbo);
        if (set.tex[0] != 0) glDeleteTextures(3, set.tex);
        set = StateSet{};
    }
}

void FlashLimitPass::bindFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, frame_.fbo());
    frame_.discardContents();
    glViewport(0, 0, width_, height_);
}

void FlashLimitPass::draw(GLuint quadVao) {
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void FlashLimitPass::apply(GLuint targetFbo, float dt, GLuint quadVao) {
    glDisable(GL_BLEND);
    // 1. Frame -> cells.
    glBindFramebuffer(GL_FRAMEBUFFER, cells_.fbo());
    cells_.discardContents();
    glViewport(0, 0, cells_.width(), cells_.height());
    glUseProgram(lumProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_.tex());
    glUniform1i(lumUniforms_.loc("uFrame"), 0);
    glUniform2i(lumUniforms_.loc("uCells"), cells_.width(), cells_.height());
    draw(quadVao);
    // 2. Cells -> tiles.
    glBindFramebuffer(GL_FRAMEBUFFER, tiles_.fbo());
    tiles_.discardContents();
    glViewport(0, 0, cols_, rows_);
    glUseProgram(tileProgram_);
    glBindTexture(GL_TEXTURE_2D, cells_.tex());
    glUniform1i(tileUniforms_.loc("uCellsTex"), 0);
    draw(quadVao);
    // 3. Step every tile's state into the other set.
    const int next = 1 - current_;
    glBindFramebuffer(GL_FRAMEBUFFER, states_[next].fbo);
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, kStateAttachments);
    glUseProgram(stateProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, states_[current_].tex[0]);
    glUniform1i(stateUniforms_.loc("uState0"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, states_[current_].tex[1]);
    glUniform1i(stateUniforms_.loc("uState1"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tiles_.tex());
    glUniform1i(stateUniforms_.loc("uTiles"), 2);
    glUniform1f(stateUniforms_.loc("uDt"), dt);
    glUniform1f(stateUniforms_.loc("uFlBand"), safety::kFlashBand);
    glUniform1f(stateUniforms_.loc("uFlCapacity"), safety::kFlashBudgetCapacity);
    glUniform1f(stateUniforms_.loc("uFlRefill"), safety::kFlashBudgetRefill);
    glUniform1f(stateUniforms_.loc("uFlFree"), safety::kFlashFreeRate);
    glUniform1f(stateUniforms_.loc("uFlMaxDt"), safety::kFlashMaxDt);
    draw(quadVao);
    current_ = next;
    stepped_ = true;
    // 4. Frame -> target, through the gains.
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(applyProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_.tex());
    glUniform1i(applyUniforms_.loc("uFrame"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, states_[current_].tex[2]);
    glUniform1i(applyUniforms_.loc("uGain"), 1);
    glUniform2i(applyUniforms_.loc("uTileCount"), cols_, rows_);
    draw(quadVao);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

}  // namespace geode::viz
