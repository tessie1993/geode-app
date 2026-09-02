#include "viz/scenes/CymaticsScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

void CymaticsScene::init() {
    program_ = 0;
    vao_ = 0;
    uniforms_ = UniformCache(0);
    programOk_ = false;
    plate_.reset();
    drops_.reset();
    touchPhase_ = 0.0f;
    std::string error;
    program_ = loader_.build("quad_vert.glsl", "cymatics_field_frag.glsl", &error);
    if (program_ == 0) {
        host_.onShaderError("Cymatics unavailable on this GPU: " + error);
        return;
    }
    programOk_ = true;
    uniforms_ = UniformCache(program_);
    glGenVertexArrays(1, &vao_);
}

void CymaticsScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

void CymaticsScene::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
}

void CymaticsScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeFeatureFrame f = hasPending_ ? pending_ : GeodeFeatureFrame{};
    hasPending_ = false;

    plate_.excite(driveSpectrum(f, dt), GEODE_BAND_COUNT, dt, p.cymaticsFundamental,
                  kDriveGain * cymatics::safeDrive(p.audioDrive) * (1.0f + kPcmStrikeGain * pcmStrike_), cymatics::ringSeconds(p.cymaticsRing),
                  p.cymaticsFocus);
    plate_.advancePhases(dt, p.speed);
    modeCount_ = plate_.snapshot(std::min(p.cymaticsModes, style_.modeCap), modes_.data(), static_cast<int>(modes_.size()));

    float totalAmplitude = 0.0f;
    for (int i = 0; i < modeCount_; ++i) totalAmplitude += modes_[static_cast<size_t>(i) * 4 + 2];

    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * std::clamp(p.beatResponse, 0.0f, 2.0f), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float swirlRate = std::clamp(p.cymaticsSwirl * style_.swirl, -1.0f, 1.0f) * speed;
    swirlPhase_ = cymatics::wrapPhase(swirlPhase_ + swirlRate * dt, kTwoPi);
    const float flowRate = std::clamp(p.cymaticsFlow * style_.flow, 0.0f, 1.0f) * speed;
    travelPhase_ = cymatics::wrapPhase(travelPhase_ + flowRate * kTravelOmega * dt, kTwoPi);
    driftShift_ = cymatics::wrapPhase(driftShift_ + flowRate * kDriftRate * dt, kDriftWrap);

    // The finger's ripples ring at the loudest mode's own frequency so they keep time with the plate.
    const float touchK = std::clamp(3.1415927f * plate_.dominantWavenumber(), kMinTouchK, kMaxTouchK);
    touchPhase_ = cymatics::wrapPhase(touchPhase_ + cymatics::vibrationHz(plate_.dominantWavenumber()) * speed * kTwoPi * dt, kTwoPi);

    if (style_.shaderStyle == kStyleFaraday) drops_.update(dt, hit);

    toneHue_ = cymatics::approachHue(toneHue_, live::brightness(f), cymatics::smoothing(dt, kToneTauSeconds));
    const float toneNudge = std::sin(toneHue_ * kTwoPi) * kToneHueSpan;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(program_);
    glUniform2f(loc("uResolution"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1f(loc("uTime"), time_);
    glUniform1i(loc("uStyle"), style_.shaderStyle);
    glUniform4fv(loc("uModes"), uniforms_.arrayCount("uModes", cymatics::kMaxRenderedModes), modes_.data());
    glUniform1i(loc("uModeCount"), modeCount_);
    const int geometry = style_.geometryOverride >= 0 ? style_.geometryOverride : p.cymaticsGeometry;
    glUniform1f(loc("uGeometry"), geometry == 1 ? 1.0f : 0.0f);
    glUniform1f(loc("uScale"), std::clamp(p.cymaticsScale * style_.scale, 0.5f, 8.0f));
    glUniform1f(loc("uHeightNorm"), 1.0f / std::max(totalAmplitude, kMinColorAmplitude));
    glUniform1f(loc("uFieldLive"), cymatics::fieldLiveness(totalAmplitude));
    glUniform1f(loc("uLine"), std::clamp(p.cymaticsLine * style_.line, 0.0f, 2.0f));
    glUniform1f(loc("uGlow"), std::clamp(p.cymaticsGlow * style_.glow, 0.0f, 2.0f));
    glUniform1f(loc("uFill"), std::clamp(p.cymaticsFill * style_.fill, 0.0f, 1.0f));
    glUniform1f(loc("uIridescence"), std::clamp(p.cymaticsIridescence * style_.iridescence, 0.0f, 1.0f));
    glUniform1f(loc("uCaustic"), std::clamp(p.cymaticsCaustic * style_.caustic, 0.0f, 1.5f));
    glUniform1f(loc("uSwirlPhase"), swirlPhase_);
    glUniform1f(loc("uTravelPhase"), travelPhase_);
    glUniform1f(loc("uDriftShift"), driftShift_);
    glUniform4fv(loc("uDrops"), uniforms_.arrayCount("uDrops", cymatics::Drops::kSlots), drops_.packed().data());
    glUniform1f(loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset + toneNudge);
    glUniform1f(loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(loc("uEnergy"), clampedBand(f.rms));
    glUniform1f(loc("uTreble"), clampedBand(f.treble));
    glUniform1f(loc("uBeat"), beatPulse_);
    glUniform1f(loc("uExposure"), kExposure);
    glUniform1f(loc("uTouchK"), touchK);
    glUniform1f(loc("uTouchPhase"), touchPhase_);
    uploadSceneTouch(uniforms_, touch_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

const float* CymaticsScene::driveSpectrum(const GeodeFeatureFrame& f, float dt) {
    const bool silent = f.rms < kIdleRms;
    const float step = dt / kIdleFadeSeconds;
    idleBlend_ = std::clamp(idleBlend_ + (silent ? step : -step * 3.0f), 0.0f, 1.0f);
    if (idleBlend_ <= 0.0f) return f.bands;
    const int count = GEODE_BAND_COUNT;
    idlePhase_ = std::fmod(idlePhase_ + dt * kIdleSweepHz, 1.0f);
    const float center = (0.5f - 0.42f * std::cos(idlePhase_ * kTwoPi)) * static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float d = (static_cast<float>(i) - center) / 2.6f;
        idleBands_[static_cast<size_t>(i)] = 0.62f * std::exp(-d * d);
    }
    if (idleBlend_ >= 1.0f) return idleBands_.data();
    for (int i = 0; i < count; ++i) {
        driveBands_[static_cast<size_t>(i)] = f.bands[i] * (1.0f - idleBlend_) + idleBands_[static_cast<size_t>(i)] * idleBlend_;
    }
    return driveBands_.data();
}

void CymaticsScene::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    program_ = 0;
    vao_ = 0;
    programOk_ = false;
    uniforms_ = UniformCache(0);
}

}  // namespace geode::viz
