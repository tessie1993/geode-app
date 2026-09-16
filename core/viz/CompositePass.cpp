#include "viz/CompositePass.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/BlueNoise.hpp"
#include "viz/CompositeGrade.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "GeodeCompositePass";

// Android Bitmap.getPixels hands back ARGB_8888 packed as 0xAARRGGBB ints; GLES RGBA8 wants
// R,G,B,A byte order. Converted once here, on the GL thread, rather than by every caller of
// setOverlay/setUnderlay (which may be the audio or UI thread).
void argbToRgbaBytes(const std::vector<uint32_t>& argb, std::vector<uint8_t>& out) {
    out.resize(argb.size() * 4);
    for (size_t i = 0; i < argb.size(); i++) {
        const uint32_t px = argb[i];
        uint8_t* dst = &out[i * 4];
        dst[0] = static_cast<uint8_t>((px >> 16) & 0xFFu);  // R
        dst[1] = static_cast<uint8_t>((px >> 8) & 0xFFu);   // G
        dst[2] = static_cast<uint8_t>(px & 0xFFu);          // B
        dst[3] = static_cast<uint8_t>((px >> 24) & 0xFFu);  // A
    }
}
}  // namespace

bool CompositePass::create(const std::string& fadeVert, std::string* error) {
    if (!transitions_.create(fadeVert, error)) return false;
    noiseTex_ = createNoiseTexture();
    zeroTex_ = createZeroTexture();
    const auto overlayFrag = assets_.load("overlay_frag.glsl", error);
    if (!overlayFrag) return false;
    overlayProgram_ = program::build(fadeVert, *overlayFrag, cache_, error);
    if (overlayProgram_ == 0) return false;
    overlayUniforms_ = UniformCache(overlayProgram_);
    return true;
}

void CompositePass::release() {
    transitions_.release();
    releaseStaleTextures();
    if (overlayProgram_ != 0) glDeleteProgram(overlayProgram_);
    overlayProgram_ = 0;
    overlayUniforms_ = UniformCache(0);
}

void CompositePass::releaseStaleTextures() {
    if (noiseTex_ != 0) glDeleteTextures(1, &noiseTex_);
    noiseTex_ = 0;
    // zeroTex_ is only ever (re)created in create(), which callers invoke
    // again right after releaseStaleTextures() on surface recreation; delete
    // it here too so it doesn't leak the old context's texture name.
    if (zeroTex_ != 0) glDeleteTextures(1, &zeroTex_);
    zeroTex_ = 0;
    // W00: same reasoning applies to the overlay/underlay textures - Renderer
    // re-arms overlayDirty_/underlayDirty_ from its retained pixel buffers on
    // surface recreation (see Renderer::onSurfaceCreated), so it is safe (and
    // required, to avoid leaking the old context's texture names) to drop
    // these here rather than carry them across a surface loss.
    overlayTex_.release();
    underlayTex_.release();
    underlayBlendMode_ = 0;
    underlayAmount_ = 0.0f;
}

void CompositePass::uploadOverlay(const std::vector<uint32_t>& argbPixels, int width, int height) {
    if (width <= 0 || height <= 0 || argbPixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) {
        GEODE_LOGW(kTag, "uploadOverlay: invalid %dx%d for %zu pixels", width, height, argbPixels.size());
        clearOverlay();
        return;
    }
    std::vector<uint8_t> rgba;
    argbToRgbaBytes(argbPixels, rgba);
    if (!overlayTex_.ok() || overlayTex_.width() != width || overlayTex_.height() != height) {
        overlayTex_.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    overlayTex_.upload(GL_RGBA, GL_UNSIGNED_BYTE, rgba.data(), width, height);
}

void CompositePass::clearOverlay() { overlayTex_.release(); }

void CompositePass::uploadUnderlay(const std::vector<uint32_t>& argbPixels, int width, int height, int blend, float amount) {
    if (width <= 0 || height <= 0 || argbPixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height)) {
        GEODE_LOGW(kTag, "uploadUnderlay: invalid %dx%d for %zu pixels", width, height, argbPixels.size());
        clearUnderlay();
        return;
    }
    std::vector<uint8_t> rgba;
    argbToRgbaBytes(argbPixels, rgba);
    if (!underlayTex_.ok() || underlayTex_.width() != width || underlayTex_.height() != height) {
        underlayTex_.createImage(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, width, height, GL_LINEAR, GL_CLAMP_TO_EDGE);
    }
    underlayTex_.upload(GL_RGBA, GL_UNSIGNED_BYTE, rgba.data(), width, height);
    underlayBlendMode_ = std::clamp(blend, 0, 2);
    underlayAmount_ = std::clamp(amount, 0.0f, 1.0f);
}

void CompositePass::clearUnderlay() {
    underlayTex_.release();
    underlayBlendMode_ = 0;
    underlayAmount_ = 0.0f;
}

void CompositePass::drawOverlay(GLuint quadVao) {
    if (!overlayTex_.ok() || overlayProgram_ == 0) return;
    glEnable(GL_BLEND);
    // Premultiplied alpha: the overlay's own colour channels are already
    // scaled by its alpha, so the destination term needs no separate SRC_ALPHA.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(overlayProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayTex_.id());
    glUniform1i(overlayUniforms_.loc("uOverlay"), 0);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
}

GLuint CompositePass::createZeroTexture() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const unsigned char zero[4] = {0, 0, 0, 0};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
    return tex;
}

GLuint CompositePass::createNoiseTexture() { return blue_noise::createTexture(assets_); }

void CompositePass::draw(const Inputs& inputs) {
    glDisable(GL_BLEND);
    UniformCache& program = transitions_.programFor(inputs.transitionId);
    const TransitionCatalog::Def* definition = transitions_.definition(inputs.transitionId);
    glUseProgram(program.program());
    transitions_.uploadParamsIfNeeded(program, definition);

    bindTextures(program, inputs);
    uploadFrameUniforms(program, inputs, definition);
    uploadGradeUniforms(program, inputs);

    glUniform4fv(program.loc("uGateA"), 1, inputs.gateA.data());
    glUniform4fv(program.loc("uGateB"), 1, inputs.gateB.data());
    glBindVertexArray(inputs.quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void CompositePass::bindTextures(UniformCache& p, const Inputs& inputs) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputs.texA);
    glUniform1i(p.loc("uTexA"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, inputs.texB);
    glUniform1i(p.loc("uTexB"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, inputs.flowTex);
    glUniform1i(p.loc("uFlow"), 2);
    glUniform1f(p.loc("uFlowStrength"), inputs.flowStrength);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, inputs.rippleTex);
    glUniform1i(p.loc("uRipple"), 3);
    glUniform2f(p.loc("uRippleTexel"), inputs.rippleTexelW, inputs.rippleTexelH);
    glUniform1f(p.loc("uRippleStrength"), inputs.rippleStrength);
    glUniform1f(p.loc("uRippleSpecular"), inputs.rippleSpecular);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    glUniform1i(p.loc("uNoise"), 4);
    glUniform1f(p.loc("uDither"), noiseTex_ != 0 ? kDitherAmount : 0.0f);
    // W00: underlay, see the class comment in CompositePass.hpp.
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, inputs.underlayTex);
    glUniform1i(p.loc("uUnderlay"), 5);
    glUniform1i(p.loc("uUnderlayBlend"), inputs.underlayBlend);
    glUniform1f(p.loc("uUnderlayAmount"), inputs.underlayAmount);
}

void CompositePass::uploadFrameUniforms(UniformCache& p, const Inputs& inputs, const TransitionCatalog::Def* definition) {
    glUniform1f(p.loc("uProgress"), inputs.progress);
    glUniform1f(p.loc("uLayerMix"), inputs.layerMix);
    glUniform1i(p.loc("uBlendMode"), inputs.blendOrdinal);
    const int styleValue = inputs.hasLayer ? kStyleLayer
                           : !inputs.hasOutgoing ? static_cast<int>(TransitionStyle::Cut)
                           : definition ? TransitionCatalog::kStyleLibrary
                                        : static_cast<int>(inputs.transitionStyle);
    glUniform1i(p.loc("uStyle"), styleValue);
    glUniform1f(p.loc("uRatio"), inputs.ratio);
    glUniform1f(p.loc("uTime"), inputs.timeSeconds);
}

void CompositePass::uploadGradeUniforms(UniformCache& p, const Inputs& inputs) {
    const SceneParams& fx = inputs.fx;
    glUniform1f(p.loc("uChroma"), fx.chromaAb);
    glUniform1f(p.loc("uVignette"), fx.vignette);
    glUniform1f(p.loc("uScanline"), fx.scanlines);
    glUniform1f(p.loc("uGrain"), fx.grain);
    glUniform1f(p.loc("uGlitch"), fx.glitch);
    glUniform1f(p.loc("uFisheye"), fx.fisheye);
    glUniform1f(p.loc("uPostWarp"), fx.warp);
    glUniform1f(p.loc("uPostRipple"), fx.ripple);
    glUniform1f(p.loc("uPostSymmetry"), static_cast<float>(fx.symmetry));
    glUniform1f(p.loc("uPostKaleido"), fx.kaleidoscope ? 1.0f : 0.0f);
    glUniform1f(p.loc("uPostPixelate"), fx.pixelate);
    glUniform1f(p.loc("uPostTile"), fx.tile);
    glUniform1f(p.loc("uPostTwist"), fx.twist);
    glUniform1f(p.loc("uPostBloom"), fx.bloom);
    glUniform1f(p.loc("uPostPosterize"), fx.posterize);
    glUniform1f(p.loc("uPostDriftX"), fx.driftX);
    glUniform1f(p.loc("uPostDriftY"), fx.driftY);
    glUniform1f(p.loc("uPostSway"), fx.sway);
    glUniform1f(p.loc("uPostTemp"), fx.temperature);
    glUniform1f(p.loc("uPostSolarize"), fx.solarize ? 1.0f : 0.0f);
    glUniform1f(p.loc("uPostMirror"), fx.mirror ? 1.0f : 0.0f);
    glUniform1f(p.loc("uPostInvert"), fx.invert ? 1.0f : 0.0f);
    glUniform1f(p.loc("uPostZoom"), fx.zoom);
    glUniform1f(p.loc("uPostRotation"), inputs.postRotationAngle);
    glUniform1f(p.loc("uPostSat"), fx.saturation);
    glUniform1f(p.loc("uPostBright"), grade::brightness(fx.brightness, fx.intensity));
    glUniform1f(p.loc("uPostContrast"), fx.contrast);
    glUniform1f(p.loc("uPostGamma"), fx.gamma);
    glUniform1f(p.loc("uPostHue"), fx.colorShift + inputs.postCyclePhase);
}

}  // namespace geode::viz
