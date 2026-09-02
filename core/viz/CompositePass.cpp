#include "viz/CompositePass.hpp"

#include "viz/BlueNoise.hpp"
#include "viz/CompositeGrade.hpp"

namespace geode::viz {

bool CompositePass::create(const std::string& fadeVert, std::string* error) {
    if (!transitions_.create(fadeVert, error)) return false;
    noiseTex_ = createNoiseTexture();
    zeroTex_ = createZeroTexture();
    return true;
}

void CompositePass::release() {
    transitions_.release();
    releaseStaleTextures();
    if (zeroTex_ != 0) glDeleteTextures(1, &zeroTex_);
    zeroTex_ = 0;
}

void CompositePass::releaseStaleTextures() {
    if (noiseTex_ != 0) glDeleteTextures(1, &noiseTex_);
    noiseTex_ = 0;
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
    glUniform1f(p.loc("uBeat"), inputs.hitImpulse);
}

void CompositePass::uploadGradeUniforms(UniformCache& p, const Inputs& inputs) {
    const SceneParams& fx = inputs.fx;
    glUniform1f(p.loc("uChroma"), fx.chromaAb);
    glUniform1f(p.loc("uVignette"), fx.vignette);
    glUniform1f(p.loc("uScanline"), fx.scanlines);
    glUniform1f(p.loc("uGrain"), fx.grain);
    glUniform1f(p.loc("uGlitch"), fx.glitch);
    glUniform1f(p.loc("uFisheye"), fx.fisheye);
    glUniform1f(p.loc("uStrobe"), fx.strobe);
    glUniform1f(p.loc("uStrobeHz"), inputs.strobeHz);
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
    glUniform1f(p.loc("uPostShake"), fx.shake);
    glUniform1f(p.loc("uPostFlash"), inputs.flash);
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
    glUniform1f(p.loc("uPostPulse"), grade::pulseAmount(fx.pulse, inputs.postBeatPulse));
}

}  // namespace geode::viz
