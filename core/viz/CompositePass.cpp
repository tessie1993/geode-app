#include "viz/CompositePass.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/BlueNoise.hpp"
#include "viz/CompositeGrade.hpp"
#include "viz/TwistShape.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "GeodeCompositePass";
}  // namespace

bool CompositePass::create(const std::string& fadeVert, std::string* error) {
    if (!transitions_.create(fadeVert, error)) return false;
    noiseTex_.reset(createNoiseTexture());
    zeroTex_.reset(createZeroTexture());
    return true;
}

void CompositePass::release() {
    transitions_.release();
    releaseStaleTextures();
}

void CompositePass::forget() {
    transitions_.forget();
    noiseTex_.forget();
    zeroTex_.forget();
    underlayTex_.forget();
    underlayBlendMode_ = 0;
    underlayAmount_ = 0.0f;
}

void CompositePass::releaseStaleTextures() {
    noiseTex_.reset();
    // zeroTex_ is only ever (re)created in create(), which callers invoke
    // again right after releaseStaleTextures() on surface recreation; delete
    // it here too so it doesn't leak the old context's texture name.
    zeroTex_.reset();
    // W00: same reasoning applies to the underlay texture - Renderer re-arms
    // underlayDirty_ from its retained pixel buffer on surface recreation (see
    // Renderer::onSurfaceCreated), so it is safe (and required, to avoid
    // leaking the old context's texture name) to drop it here rather than
    // carry it across a surface loss.
    underlayTex_.release();
    underlayBlendMode_ = 0;
    underlayAmount_ = 0.0f;
}

void CompositePass::uploadUnderlay(const std::vector<uint8_t>& rgba, int width, int height, int blend, float amount) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        GEODE_LOGW(kTag, "uploadUnderlay: invalid %dx%d for %zu bytes", width, height, rgba.size());
        clearUnderlay();
        return;
    }
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

    glUniform4fv(program.loc(Uniform::uGateA), 1, inputs.gateA.data());
    glUniform4fv(program.loc(Uniform::uGateB), 1, inputs.gateB.data());
    glUniform4fv(program.loc(Uniform::uCamFlat), 1, inputs.camFlat.data());
    glUniform2fv(program.loc(Uniform::uCamTilt), 1, inputs.camTilt.data());
    glUniform2fv(program.loc(Uniform::uCamGate), 1, inputs.camGate.data());
    glUniform4fv(program.loc(Uniform::uGateL), 1, inputs.gateL.data());
    glUniform1f(program.loc(Uniform::uCamGateL), inputs.camGateL);
    glBindVertexArray(inputs.quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

// The samplers' units were set when each program was built (TransitionPrograms, composite_unit).
void CompositePass::bindTextures(UniformCache& p, const Inputs& inputs) {
    auto bind = [](int unit, GLuint tex) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, tex);
    };
    bind(composite_unit::kTexA, inputs.texA);
    bind(composite_unit::kTexB, inputs.texB);
    bind(composite_unit::kFlow, inputs.flowTex);
    glUniform1f(p.loc(Uniform::uFlowStrength), inputs.flowStrength);
    bind(composite_unit::kNoise, noiseTex_.get());
    glUniform1f(p.loc(Uniform::uDither), noiseTex_ ? kDitherAmount : 0.0f);
    // W00: underlay, see the class comment in CompositePass.hpp.
    bind(composite_unit::kUnderlay, inputs.underlayTex);
    glUniform1i(p.loc(Uniform::uUnderlayBlend), inputs.underlayBlend);
    glUniform1f(p.loc(Uniform::uUnderlayAmount), inputs.underlayAmount);
    bind(composite_unit::kTexL, inputs.texL != 0 ? inputs.texL : zeroTex_.get());
}

void CompositePass::uploadFrameUniforms(UniformCache& p, const Inputs& inputs, const TransitionCatalog::Def* definition) {
    glUniform1f(p.loc(Uniform::uProgress), inputs.progress);
    glUniform1f(p.loc(Uniform::uLayerMix), inputs.layerMix);
    glUniform1i(p.loc(Uniform::uBlendMode), inputs.blendOrdinal);
    // The layer goes over whatever the transition shows, so a style change runs under a layer as it does without.
    glUniform1i(p.loc(Uniform::uHasLayer), inputs.hasLayer ? 1 : 0);
    const int styleValue = !inputs.hasOutgoing ? static_cast<int>(TransitionStyle::Cut)
                           : definition ? TransitionCatalog::kStyleLibrary
                                        : static_cast<int>(inputs.transitionStyle);
    glUniform1i(p.loc(Uniform::uStyle), styleValue);
    glUniform1f(p.loc(Uniform::uRatio), inputs.ratio);
    glUniform1f(p.loc(Uniform::uTime), inputs.timeSeconds);
}

void CompositePass::uploadGradeUniforms(UniformCache& p, const Inputs& inputs) {
    const SceneParams& fx = inputs.fx;
    glUniform1f(p.loc(Uniform::uChroma), fx.chromaAb);
    glUniform1f(p.loc(Uniform::uVignette), fx.vignette);
    glUniform1f(p.loc(Uniform::uScanline), fx.scanlines);
    glUniform1f(p.loc(Uniform::uGrain), fx.grain);
    glUniform1f(p.loc(Uniform::uFisheye), fx.fisheye);
    glUniform1f(p.loc(Uniform::uPostWarp), fx.warp);
    glUniform1f(p.loc(Uniform::uPostRipple), fx.ripple);
    const SceneParams::KaleidoUniforms kaleido = SceneParams::kaleidoUniforms(fx.kaleidoFolds);
    glUniform1f(p.loc(Uniform::uPostSymmetry), kaleido.folds);
    glUniform1f(p.loc(Uniform::uPostKaleido), kaleido.on);
    glUniform1f(p.loc(Uniform::uPostPixelate), fx.pixelate);
    glUniform1f(p.loc(Uniform::uPostTile), fx.tile);
    glUniform1f(p.loc(Uniform::uPostTwist), fx.twist);
    {
        // The composite's picture spans half a unit each side of the middle where the shader scenes' spans one.
        const TwistShape twist = twistShape(inputs.twistSeed, inputs.timeSeconds);
        glUniform2f(p.loc(Uniform::uPostTwistCentre), twist.x * 0.5f, twist.y * 0.5f);
        glUniform1f(p.loc(Uniform::uPostTwistBend), twist.bend);
    }
    glUniform1f(p.loc(Uniform::uPostBloom), fx.bloom);
    glUniform1f(p.loc(Uniform::uPostPosterize), fx.posterize);
    glUniform1f(p.loc(Uniform::uPostDriftX), fx.driftX);
    glUniform1f(p.loc(Uniform::uPostDriftY), fx.driftY);
    glUniform1f(p.loc(Uniform::uPostSway), fx.sway);
    glUniform1f(p.loc(Uniform::uPostTemp), fx.temperature);
    glUniform1f(p.loc(Uniform::uPostSolarize), fx.solarize ? 1.0f : 0.0f);
    glUniform1f(p.loc(Uniform::uPostMirror), fx.mirror ? 1.0f : 0.0f);
    glUniform1f(p.loc(Uniform::uPostInvert), fx.invert ? 1.0f : 0.0f);
    glUniform1f(p.loc(Uniform::uPostZoom), fx.zoom);
    glUniform1f(p.loc(Uniform::uPostRotation), inputs.postRotationAngle);
    glUniform1f(p.loc(Uniform::uPostLife), fx.life);
    glUniform4fv(p.loc(Uniform::uPostBirths), p.arrayCount(Uniform::uPostBirths, 2 * births::kBirths), inputs.postBirths.data());
    glUniform1f(p.loc(Uniform::uPostSat), fx.saturation);
    glUniform1f(p.loc(Uniform::uPostBright), grade::brightness(fx.brightness, fx.intensity));
    glUniform1f(p.loc(Uniform::uPostContrast), fx.contrast);
    glUniform1f(p.loc(Uniform::uPostGamma), fx.gamma);
    glUniform1f(p.loc(Uniform::uPostHue), fx.colorShift);
}

}  // namespace geode::viz
