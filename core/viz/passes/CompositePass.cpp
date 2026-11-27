#include "viz/passes/CompositePass.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/gl/BlueNoise.hpp"
#include "viz/passes/CompositeGrade.hpp"
#include "viz/passes/CompositeTerms.hpp"
#include "viz/passes/TwistShape.hpp"

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

    program.setFloats(Uniform::uCamFlat, 4, 1, inputs.camFlat.data());
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
    bind(composite_unit::kNoise, noiseTex_.get());
    p.set1f(Uniform::uDither, noiseTex_ ? kDitherAmount : 0.0f);
    // W00: underlay, see the class comment in CompositePass.hpp.
    bind(composite_unit::kUnderlay, inputs.underlayTex);
    p.set1i(Uniform::uUnderlayBlend, inputs.underlayBlend);
    p.set1f(Uniform::uUnderlayAmount, inputs.underlayAmount);
    bind(composite_unit::kTexL, inputs.texL != 0 ? inputs.texL : zeroTex_.get());
}

void CompositePass::uploadFrameUniforms(UniformCache& p, const Inputs& inputs, const TransitionCatalog::Def* definition) {
    p.set1f(Uniform::uProgress, inputs.progress);
    p.set1f(Uniform::uLayerMix, inputs.layerMix);
    p.set1i(Uniform::uBlendMode, inputs.blendOrdinal);
    // The layer goes over whatever the transition shows, so a style change runs under a layer as it does without.
    p.set1i(Uniform::uHasLayer, inputs.hasLayer ? 1 : 0);
    const int styleValue = !inputs.hasOutgoing ? static_cast<int>(TransitionStyle::Cut)
                           : definition ? TransitionCatalog::kStyleLibrary
                                        : static_cast<int>(inputs.transitionStyle);
    p.set1i(Uniform::uStyle, styleValue);
    p.set1f(Uniform::uRatio, inputs.ratio);
    p.set1f(Uniform::uTime, inputs.timeSeconds);
}

void CompositePass::uploadGradeUniforms(UniformCache& p, const Inputs& inputs) {
    const SceneParams& fx = inputs.fx;
    const SceneParams::KaleidoUniforms kaleido = SceneParams::kaleidoUniforms(fx.kaleidoFolds);
    // The composite's picture spans half a unit each side of the middle where the shader scenes' spans one.
    const TwistShape twist = twistShape(inputs.twistSeed, inputs.timeSeconds);

    composite::Look look;
    look.chroma = fx.chromaAb;
    look.vignette = fx.vignette;
    look.scanlines = fx.scanlines;
    look.grain = fx.grain;
    look.fisheye = fx.fisheye;
    look.warp = fx.warp;
    look.ripple = fx.ripple;
    look.kaleidoFolds = kaleido.folds;
    look.kaleidoOn = kaleido.on;
    look.pixelate = fx.pixelate;
    look.tile = fx.tile;
    look.twist = fx.twist;
    look.twistBend = twist.bend;
    look.bloom = fx.bloom;
    look.posterize = fx.posterize;
    look.driftX = fx.driftX;
    look.driftY = fx.driftY;
    look.sway = fx.sway;
    look.temperature = fx.temperature;
    look.solarize = fx.solarize;
    look.mirror = fx.mirror;
    look.invert = fx.invert;
    look.zoom = fx.zoom;
    look.life = fx.life;
    look.saturation = fx.saturation;
    look.brightness = grade::brightness(fx.brightness, fx.intensity);
    look.contrast = fx.contrast;
    look.gamma = fx.gamma;
    look.hue = fx.colorShift;
    look.rotation = inputs.postRotationAngle;
    look.flowStrength = inputs.flowStrength;
    look.time = inputs.timeSeconds;
    look.camFlat = inputs.camFlat;
    look.camTilt = inputs.camTilt;

    auto gateOf = [](const std::array<float, 4>& g) { return grade::Gate{g[0] > 0.5f, g[1] > 0.5f, g[2] > 0.5f}; };
    const composite::TextureTerms a = composite::textureTerms(look, gateOf(inputs.gateA), inputs.camGate[0] > 0.5f);
    const composite::TextureTerms b = composite::textureTerms(look, gateOf(inputs.gateB), inputs.camGate[1] > 0.5f);
    const composite::TextureTerms l = composite::textureTerms(look, gateOf(inputs.gateL), inputs.camGateL > 0.5f);
    const composite::FrameTerms frame = composite::frameTerms(look);

    p.set1i(Uniform::uFxA, a.flags);
    p.set1i(Uniform::uFxB, b.flags);
    p.set1i(Uniform::uFxL, l.flags);
    p.set4f(Uniform::uRotAB, a.rotCos, a.rotSin, b.rotCos, b.rotSin);
    p.set2f(Uniform::uRotL, l.rotCos, l.rotSin);
    p.set1i(Uniform::uFxFrame, frame.frameFlags);
    p.setFloats(Uniform::uCamTiltCS, 4, 1, frame.camTiltCS.data());
    p.setFloats(Uniform::uCamRoll, 4, 1, frame.camRoll.data());
    p.setFloats(Uniform::uPostGeo, 4, 1, frame.geo.data());
    p.setFloats(Uniform::uPostGeo2, 4, 1, frame.geo2.data());
    p.setFloats(Uniform::uPostShade, 4, 1, frame.shade.data());
    p.set2f(Uniform::uPostHueCS, frame.hueCS[0], frame.hueCS[1]);
    p.setFloats(Uniform::uPostLens, 4, 1, frame.lens.data());
    p.setFloats(Uniform::uPostLens2, 4, 1, frame.lens2.data());

    p.set1f(Uniform::uPostTile, fx.tile);
    p.set2f(Uniform::uPostTwistCentre, twist.x * 0.5f, twist.y * 0.5f);
    p.set1f(Uniform::uPostBloom, fx.bloom);
    p.setFloats(Uniform::uPostBirths, 4, p.arrayCount(Uniform::uPostBirths, 2 * births::kBirths), inputs.postBirths.data());
    p.set1f(Uniform::uPostSat, fx.saturation);
    p.set1f(Uniform::uPostBright, look.brightness);
    p.set1f(Uniform::uPostContrast, fx.contrast);
}
}  // namespace geode::viz
