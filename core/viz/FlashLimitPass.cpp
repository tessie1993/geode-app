#include "viz/FlashLimitPass.hpp"

#include <initializer_list>

#include "util/Log.hpp"
#include "viz/FlashLimiter.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "GeodeFlashLimit";
constexpr GLenum kAttachments[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
static_assert(flash_limit::kSlots == safety::kFlashEventsPerWindow, "the detector keeps one age per allowed event");
static_assert(flash_limit::kTiles + 2 * flash_limit::kRegions <= FlashLimitPass::kControlSize * FlashLimitPass::kControlSize,
              "the control texture holds every tile and detector");
}  // namespace

bool FlashLimitPass::Target::create(int w, int h, int attachments) {
    release();
    count = attachments;
    glGenTextures(count, tex);
    for (int i = 0; i < count; ++i) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (int i = 0; i < count; ++i) glFramebufferTexture2D(GL_FRAMEBUFFER, kAttachments[i], GL_TEXTURE_2D, tex[i], 0);
    glDrawBuffers(count, kAttachments);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        // All zero: for the control state that is "not primed".
        const GLfloat zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int i = 0; i < count; ++i) glClearBufferfv(GL_COLOR, i, zero);
        width = w;
        height = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) release();
    return complete;
}

void FlashLimitPass::Target::release() {
    if (fbo != 0) glDeleteFramebuffers(1, &fbo);
    if (count > 0 && tex[0] != 0) glDeleteTextures(count, tex);
    *this = Target{};
}

void FlashLimitPass::Target::discard() const { glInvalidateFramebuffer(GL_FRAMEBUFFER, count, kAttachments); }

bool FlashLimitPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error) {
    release();
    const auto cells = assets.load("flash_cells_frag.glsl", error);
    const auto tiles = assets.load("flash_tiles_frag.glsl", error);
    const auto control = assets.load("flash_control_frag.glsl", error);
    const auto apply = assets.load("flash_apply_frag.glsl", error);
    if (!cells || !tiles || !control || !apply) return false;
    cellsProgram_ = program::build(fadeVert, *cells, cache, error);
    tilesProgram_ = cellsProgram_ ? program::build(fadeVert, *tiles, cache, error) : 0;
    controlProgram_ = tilesProgram_ ? program::build(fadeVert, *control, cache, error) : 0;
    applyProgram_ = controlProgram_ ? program::build(fadeVert, *apply, cache, error) : 0;
    if (applyProgram_ == 0) {
        release();
        return false;
    }
    cellsUniforms_ = UniformCache(cellsProgram_);
    tilesUniforms_ = UniformCache(tilesProgram_);
    controlUniforms_ = UniformCache(controlProgram_);
    applyUniforms_ = UniformCache(applyProgram_);
    return true;
}

void FlashLimitPass::release() {
    for (GLuint* p : {&cellsProgram_, &tilesProgram_, &controlProgram_, &applyProgram_}) {
        if (*p != 0) glDeleteProgram(*p);
        *p = 0;
    }
    cellsUniforms_ = UniformCache(0);
    tilesUniforms_ = UniformCache(0);
    controlUniforms_ = UniformCache(0);
    applyUniforms_ = UniformCache(0);
    frame_.release();
    releaseGrid();
    width_ = 0;
    height_ = 0;
}

bool FlashLimitPass::ensure(int width, int height) {
    if (applyProgram_ == 0 || width <= 0 || height <= 0) return false;
    if (!frame_.ensure(width, height)) return false;
    width_ = width;
    height_ = height;
    const flash_limit::Layout layout = flash_limit::Layout::forSize(width, height);
    if (layout.cols == cols_ && layout.rows == rows_ && control_[0].fbo != 0) return true;
    // A new grid (first frame, or a turn between portrait and landscape): fresh, unprimed state.
    if (!createGrid(layout.cols, layout.rows)) {
        GEODE_LOGW(kTag, "flash limiter targets failed at %dx%d; drawing unlimited", width, height);
        return false;
    }
    return true;
}

bool FlashLimitPass::createGrid(int cols, int rows) {
    releaseGrid();
    const bool ok = cells_.create(cols * kCellsPerTile, rows * kCellsPerTile, 2) && tiles_.create(cols, rows, 2) &&
                    control_[0].create(kControlSize, kControlSize, 4) && control_[1].create(kControlSize, kControlSize, 4);
    if (!ok) {
        releaseGrid();
        return false;
    }
    cols_ = cols;
    rows_ = rows;
    current_ = 0;
    stepped_ = false;
    return true;
}

void FlashLimitPass::releaseGrid() {
    cells_.release();
    tiles_.release();
    control_[0].release();
    control_[1].release();
    cols_ = 0;
    rows_ = 0;
    stepped_ = false;
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

void FlashLimitPass::uploadConstants(UniformCache& u) {
    glUniform1f(u.loc("uRedSaturation"), safety::kRedSaturation);
    glUniform1f(u.loc("uRedGuardRatio"), safety::kRedGuardRatio);
}

void FlashLimitPass::apply(GLuint targetFbo, float dt, GLuint quadVao) {
    glDisable(GL_BLEND);
    const int cellCols = cols_ * kCellsPerTile;
    const int cellRows = rows_ * kCellsPerTile;
    // 1. Frame -> cells.
    glBindFramebuffer(GL_FRAMEBUFFER, cells_.fbo);
    cells_.discard();
    glViewport(0, 0, cellCols, cellRows);
    glUseProgram(cellsProgram_);
    uploadConstants(cellsUniforms_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_.tex());
    glUniform1i(cellsUniforms_.loc("uFrame"), 0);
    glUniform2i(cellsUniforms_.loc("uCellCount"), cellCols, cellRows);
    draw(quadVao);
    // 2. Cells -> tiles.
    glBindFramebuffer(GL_FRAMEBUFFER, tiles_.fbo);
    tiles_.discard();
    glViewport(0, 0, cols_, rows_);
    glUseProgram(tilesProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cells_.tex[0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, cells_.tex[1]);
    glUniform1i(tilesUniforms_.loc("uCellLumRed"), 0);
    glUniform1i(tilesUniforms_.loc("uCellGuarded"), 1);
    glUniform2i(tilesUniforms_.loc("uFrameSize"), width_, height_);
    glUniform2i(tilesUniforms_.loc("uCellCount"), cellCols, cellRows);
    draw(quadVao);
    // 3. Control: step every tile and detector into the other set.
    const int next = 1 - current_;
    glBindFramebuffer(GL_FRAMEBUFFER, control_[next].fbo);
    control_[next].discard();
    glViewport(0, 0, kControlSize, kControlSize);
    UniformCache& c = controlUniforms_;
    glUseProgram(controlProgram_);
    uploadConstants(c);
    const GLuint inputs[6] = {tiles_.tex[0], tiles_.tex[1], control_[current_].tex[0], control_[current_].tex[1],
                              control_[current_].tex[2], control_[current_].tex[3]};
    const char* names[6] = {"uTileLumRed", "uTileGuarded", "uCtrl0", "uCtrl1", "uCtrl2", "uCtrl3"};
    for (int i = 0; i < 6; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, inputs[i]);
        glUniform1i(c.loc(names[i]), i);
    }
    glUniform2i(c.loc("uGrid"), cols_, rows_);
    glUniform2i(c.loc("uFrameSize"), width_, height_);
    glUniform2i(c.loc("uCellCount"), cellCols, cellRows);
    glUniform1f(c.loc("uDt"), dt);
    glUniform1f(c.loc("uFlSwing"), safety::kFlashSwing);
    glUniform1f(c.loc("uFlDarkLimit"), safety::kFlashDarkLimit);
    glUniform1f(c.loc("uFlWindow"), safety::kFlashWindow);
    glUniform1f(c.loc("uRedSwing"), safety::kRedSwing);
    glUniform1f(c.loc("uFlDetectSlack"), safety::kFlashDetectSlack);
    glUniform1f(c.loc("uFlWindowSlack"), safety::kFlashWindowSlack);
    glUniform1f(c.loc("uFlReleaseRate"), safety::kFlashReleaseRate);
    glUniform1f(c.loc("uFlMaxDt"), safety::kFlashMaxDt);
    draw(quadVao);
    current_ = next;
    stepped_ = true;
    // 4. Frame -> target, through the tile transforms.
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glUseProgram(applyProgram_);
    uploadConstants(applyUniforms_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_.tex());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, control_[current_].tex[0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, control_[current_].tex[1]);
    glUniform1i(applyUniforms_.loc("uFrame"), 0);
    glUniform1i(applyUniforms_.loc("uCtrl0"), 1);
    glUniform1i(applyUniforms_.loc("uCtrl1"), 2);
    glUniform2i(applyUniforms_.loc("uGrid"), cols_, rows_);
    glUniform2i(applyUniforms_.loc("uCellCount"), cellCols, cellRows);
    draw(quadVao);
    for (int i = 5; i >= 0; --i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

}  // namespace geode::viz
