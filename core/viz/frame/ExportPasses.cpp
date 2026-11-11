#include "viz/frame/ExportPasses.hpp"

#include <algorithm>
#include <cmath>

#include "viz/frame/ExportPassMath.hpp"

namespace geode::viz::frame {

namespace {

// Builds vertex + fragment from the APK's shaders/; 0 with the reason in `error`. No binary cache: an
// export builds each program once, and its context is not the live renderer's.
GLuint buildFrom(const ShaderSource& assets, const char* vert, const char* frag, std::string* error) {
    const auto vs = assets.load(vert, error);
    if (!vs) return 0;
    const auto fs = assets.load(frag, error);
    if (!fs) return 0;
    return program::build(*vs, *fs, nullptr, error);
}

// The triangle's VAO is made once; its buffer is unbound again at once, because Media3's programs draw
// from client-side arrays on the same context and a bound GL_ARRAY_BUFFER would turn their pointers
// into offsets.
void createTriangle(FullscreenTriangle& triangle) {
    triangle.create();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Binds `tex` on `unit` the way Media3's GlProgram binds a sampler (GlUtil.bindTexture): linear, clamped.
// The Kotlin programs sampled through it, so the native ones do too, or an off-grid tap (a displacement
// transition, an odd-sized frame) could read a different texel than before.
void bindSampled(GLuint tex, int unit) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void unbindUnits(int count) {
    for (int unit = count - 1; unit >= 0; unit--) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void drainErrors() {
    for (int guard = 0; guard < 32 && glGetError() != GL_NO_ERROR; guard++) {
    }
}

// The copy program's one draw: `tex` unchanged into whatever framebuffer and viewport are bound.
void drawCopy(GLuint program, UniformCache& uniforms, FullscreenTriangle& triangle, GLuint tex) {
    glUseProgram(program);
    bindSampled(tex, 0);
    glUniform1i(uniforms.loc("uTex"), 0);
    triangle.draw();
}

}  // namespace

// ---- WatermarkPass ----------------------------------------------------------------------------------

bool WatermarkPass::create(const ShaderSource& assets, const void* rgba, int tileWidth, int tileHeight, int placement,
                           int corner, int64_t lengthMs, std::string* error) {
    release();
    placement_ = placement;
    corner_ = corner;
    lengthMs_ = lengthMs;
    if (!rgba || tileWidth <= 0 || tileHeight <= 0) {
        if (error) *error = "watermark tile is empty";
        return false;
    }
    program_ = buildFrom(assets, "export_watermark_vert.glsl", "export_watermark_frag.glsl", error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    // The tiled pattern's tile is a power of two (exportmath::watermark::tileWidth), so GL_REPEAT is legal on any
    // GLES; a single mark's is clipped in the shader, so its repeat is never seen.
    tile_.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, tileWidth, tileHeight, GL_LINEAR, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    tile_.upload(GL_RGBA, GL_UNSIGNED_BYTE, rgba, tileWidth, tileHeight);
    glBindTexture(GL_TEXTURE_2D, 0);
    createTriangle(triangle_);
    tileWidth_ = tileWidth;
    tileHeight_ = tileHeight;
    return true;
}

bool WatermarkPass::wants(const FrameCtx& ctx) const {
    return ctx.surface == Surface::Export && program_ != 0 && tile_.ok() && ctx.width > 0 && ctx.height > 0 &&
           exportmath::watermark::opacity(placement_, static_cast<int64_t>(std::llround(ctx.seconds * 1000.0)), lengthMs_) > 0.0f;
}

void WatermarkPass::prepare(const FrameCtx& ctx) {
    const auto at = exportmath::watermark::layout(placement_, corner_, ctx.width, ctx.height, tileWidth_, tileHeight_);
    originU_ = at.originU;
    originV_ = at.originV;
    tilesU_ = at.tilesU;
    tilesV_ = at.tilesV;
    clip_ = at.clip ? 1.0f : 0.0f;
    opacity_ = exportmath::watermark::opacity(placement_, static_cast<int64_t>(std::llround(ctx.seconds * 1000.0)), lengthMs_);
}

void WatermarkPass::draw(const PassInputs&, const PassTarget& target) {
    const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    // The tile is premultiplied (an Android bitmap), so ONE rather than SRC_ALPHA for the source.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    tile_.bind(0);
    glUniform1i(uniforms_.loc("uTexture"), 0);
    glUniform2f(uniforms_.loc("uTiles"), tilesU_, tilesV_);
    glUniform2f(uniforms_.loc("uOrigin"), originU_, originV_);
    glUniform1f(uniforms_.loc("uClip"), clip_);
    glUniform1f(uniforms_.loc("uDim"), exportmath::watermark::dim(placement_));
    glUniform1f(uniforms_.loc("uAlpha"), opacity_);
    triangle_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(static_cast<GLuint>(previousProgram));
    if (!blendWasOn) glDisable(GL_BLEND);
}

void WatermarkPass::release() {
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    uniforms_ = UniformCache(0);
    tile_.release();
    triangle_.release();
}

// ---- SeamPass ---------------------------------------------------------------------------------------

bool SeamPass::create(const ShaderSource& assets, int width, int height, std::string* error) {
    release();
    width_ = width;
    height_ = height;
    program_ = buildFrom(assets, "quad_vert.glsl", "export_seam_frag.glsl", error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    createTriangle(triangle_);
    return true;
}

int SeamPass::allocate(int requested) {
    for (GLuint id : textures_) glDeleteTextures(1, &id);
    textures_.clear();
    if (program_ == 0 || requested <= 0 || width_ <= 0 || height_ <= 0) return 0;
    drainErrors();
    std::vector<GLuint> ids(static_cast<size_t>(requested), 0);
    glGenTextures(requested, ids.data());
    int allocated = 0;
    while (allocated < requested && allocateOne(ids[static_cast<size_t>(allocated)])) allocated++;
    if (allocated < requested) glDeleteTextures(requested - allocated, ids.data() + allocated);
    textures_.assign(ids.begin(), ids.begin() + allocated);
    return allocated;
}

bool SeamPass::allocateOne(GLuint id) const {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width_, height_);
    glBindTexture(GL_TEXTURE_2D, 0);
    // A driver that cannot find room says so here rather than by crashing later, so the seam is
    // shortened to whatever did fit.
    return glGetError() == GL_NO_ERROR;
}

void SeamPass::stageCapture(int index) {
    op_ = Op::Capture;
    index_ = index;
}

void SeamPass::stageBlend(int index, int tail, int seamFrames) {
    op_ = Op::Blend;
    index_ = index;
    tail_ = tail;
    seamFrames_ = seamFrames;
}

bool SeamPass::wants(const FrameCtx& ctx) const {
    return ctx.surface == Surface::Export && program_ != 0 && op_ != Op::None && index_ >= 0 && index_ < frames();
}

void SeamPass::prepare(const FrameCtx&) {
    if (op_ == Op::Blend) alpha_ = exportmath::seam::weight(tail_, seamFrames_);
}

void SeamPass::draw(const PassInputs&, const PassTarget& target) {
    const GLuint tex = textures_[static_cast<size_t>(index_)];
    if (op_ == Op::Capture) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
        glBindTexture(GL_TEXTURE_2D, tex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else if (op_ == Op::Blend) {
        glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
        glViewport(0, 0, target.width, target.height);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(uniforms_.loc("uTex"), 0);
        glUniform1f(uniforms_.loc("uAlpha"), alpha_);
        // Source-alpha blending of a constant alpha is exactly a cross-dissolve.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        triangle_.draw();
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    op_ = Op::None;
}

void SeamPass::release() {
    for (GLuint id : textures_) glDeleteTextures(1, &id);
    textures_.clear();
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    uniforms_ = UniformCache(0);
    triangle_.release();
    op_ = Op::None;
}

// ---- CapturePass ------------------------------------------------------------------------------------

bool CapturePass::create(const ShaderSource& assets, std::string* error) {
    release();
    program_ = buildFrom(assets, "quad_vert.glsl", "export_copy_frag.glsl", error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    createTriangle(triangle_);
    return true;
}

void CapturePass::configure(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (latest_.ok() && latest_.width() == width && latest_.height() == height) return;
    // Media3's GlUtil.createTexture: RGBA8, linear, clamped.
    latest_.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    captured_ = false;
}

bool CapturePass::wants(const FrameCtx& ctx) const { return ctx.surface == Surface::Export && program_ != 0; }

void CapturePass::prepare(const FrameCtx&) {}

void CapturePass::draw(const PassInputs& inputs, const PassTarget& target) {
    drawCopy(program_, uniforms_, triangle_, inputs.sceneTex);
    unbindUnits(1);
    if (!latest_.ok()) return;
    // What was just drawn, read back off the framebuffer Media3 bound for this program's output.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.fbo);
    glBindTexture(GL_TEXTURE_2D, latest_.id());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, latest_.width(), latest_.height());
    glBindTexture(GL_TEXTURE_2D, 0);
    captured_ = true;
}

bool CapturePass::readBack(void* out, size_t capacity) const {
    if (!out || !latest_.ok() || !captured_) return false;
    const size_t bytes = static_cast<size_t>(latest_.width()) * static_cast<size_t>(latest_.height()) * 4u;
    if (capacity < bytes) return false;
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, latest_.id(), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, latest_.width(), latest_.height(), GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    glDeleteFramebuffers(1, &fbo);
    return true;
}

void CapturePass::release() {
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    uniforms_ = UniformCache(0);
    triangle_.release();
    latest_.release();
    captured_ = false;
}

// ---- TransitionPass ---------------------------------------------------------------------------------

bool TransitionPass::create(const ShaderSource& assets, const std::string& id, int64_t durationUs, std::string* error) {
    release();
    id_ = id;
    durationUs_ = durationUs;
    copyProgram_ = buildFrom(assets, "quad_vert.glsl", "export_copy_frag.glsl", error);
    if (copyProgram_ == 0) return false;
    copyUniforms_ = UniformCache(copyProgram_);
    createTriangle(triangle_);
    TransitionCatalog catalog(assets);
    if (const TransitionCatalog::Def* def = catalog.definition(id)) {
        def_ = *def;
        const auto vert = assets.load("quad_vert.glsl", error);
        const auto harness = assets.load("export_transition_frag.glsl", error);
        if (!vert || !harness) return false;
        blendProgram_ = program::build(*vert, TransitionCatalog::spliceInto(*harness, def_), nullptr, error);
        if (blendProgram_ == 0) return false;
        blendUniforms_ = UniformCache(blendProgram_);
        // A parameter the compiler optimised out has no location; uploadParams skips it, as the Kotlin did.
        glUseProgram(blendProgram_);
        TransitionCatalog::uploadParams(blendProgram_, def_);
        glUseProgram(0);
    }
    return true;
}

void TransitionPass::configure(int width, int height) { ratio_ = exportmath::transition::ratio(width, height); }

void TransitionPass::setFrom(const void* rgba, int width, int height) {
    if (from_.ok() || !rgba || width <= 0 || height <= 0) return;
    from_.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    from_.upload(GL_RGBA, GL_UNSIGNED_BYTE, rgba, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TransitionPass::setPresentationUs(int64_t ptsUs) {
    if (originUs_ < 0) originUs_ = ptsUs;
    ptsUs_ = ptsUs;
}

bool TransitionPass::wants(const FrameCtx& ctx) const { return ctx.surface == Surface::Export && copyProgram_ != 0; }

void TransitionPass::prepare(const FrameCtx&) {
    progress_ = exportmath::transition::progress(ptsUs_, originUs_ < 0 ? ptsUs_ : originUs_, durationUs_);
}

void TransitionPass::draw(const PassInputs& inputs, const PassTarget&) {
    // Media3 has bound this program's output framebuffer and viewport; the pass draws into them as they are.
    if (blendProgram_ == 0 || exportmath::transition::passthrough(from_.ok(), progress_)) {
        drawCopy(copyProgram_, copyUniforms_, triangle_, inputs.sceneTex);
        unbindUnits(1);
        return;
    }
    glUseProgram(blendProgram_);
    bindSampled(from_.id(), 0);
    bindSampled(inputs.sceneTex, 1);
    glUniform1i(blendUniforms_.loc("uFrom"), 0);
    glUniform1i(blendUniforms_.loc("uTo"), 1);
    glUniform1f(blendUniforms_.loc("progress"), progress_);
    glUniform1f(blendUniforms_.loc("ratio"), ratio_);
    triangle_.draw();
    unbindUnits(2);
}

void TransitionPass::release() {
    if (copyProgram_ != 0) glDeleteProgram(copyProgram_);
    if (blendProgram_ != 0) glDeleteProgram(blendProgram_);
    copyProgram_ = 0;
    blendProgram_ = 0;
    copyUniforms_ = UniformCache(0);
    blendUniforms_ = UniformCache(0);
    triangle_.release();
    from_.release();
    originUs_ = -1;
}

}  // namespace geode::viz::frame
