#include "viz/scenes/SilkScene.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"
#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"
#include "viz/fluid/FluidBuffers.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "SilkScene";
}

void SilkScene::init() {
    sim_.reset();
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    std::string error;
    sim::SimSpec spec;
    spec.label = id_;
    spec.stepBody = loader_.source("silk_step.glsl", &error);
    // Every texel back-traces along the flow and samples between texels: the filterable half-float role.
    spec.sampling = sim::SimSampling::BetweenTexels;
    spec.stateScale = kByteStateScale;
    auto built = sim::SimPass::build(spec, *profile_, loader_.cache, [](const std::string& line) { GEODE_LOGI(kTag, "%s", line.c_str()); });
    if (auto* failed = std::get_if<sim::SimPass::Failed>(&built)) {
        host_.onShaderError("Silk unavailable on this GPU: " + failed->message);
        return;
    }
    sim_ = std::move(std::get<std::unique_ptr<sim::SimPass>>(built));
    applySimSize();
    showProgram_ = loader_.buildSource(loader_.source("quad_vert.glsl", &error), sim_->displayShader(loader_.source("silk_show.glsl", &error)), &error);
    if (showProgram_ == 0) {
        host_.onShaderError("Silk unavailable on this GPU: " + error);
        return;
    }
    showLocs_ = UniformCache(showProgram_);
    glGenVertexArrays(1, &vao_);
    programOk_ = true;
}

void SilkScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (sim_) applySimSize();
}

void SilkScene::applySimSize() {
    const auto [w, h] = fluid::resolution(kSimRes, width_, height_);
    sim_->resize(w, h);
}

void SilkScene::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
}

void SilkScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_ || !sim_) return;
    resetFrameState();
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeFeatureFrame f = hasPending_ ? pending_ : GeodeFeatureFrame{};
    hasPending_ = false;

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    envBass_ = slewEnvelope(envBass_, clampedBand(f.bass), dt, kEnvRisePerSec, kEnvFallPerSec);
    envMid_ = slewEnvelope(envMid_, clampedBand(f.mid), dt, kEnvRisePerSec, kEnvFallPerSec);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(f.treble), dt, kEnvRisePerSec, kEnvFallPerSec);
    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * std::clamp(p.beatResponse, 0.0f, 2.0f), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
    if (hit * p.beatResponse > kBeatThreshold) ringRadius_ = 0.0f;
    if (ringRadius_ >= 0.0f) {
        ringRadius_ += dt * kRingSpeed * speed;
        if (ringRadius_ > kRingMax) ringRadius_ = -1.0f;
    }

    slabTurn_ = std::fmod(slabTurn_ + dt * style_.slabRate * speed, 1.0f);
    foldPhase_ = std::fmod(foldPhase_ + dt * 0.03f * speed * kTwoPi, kTwoPi);
    drift_ = std::fmod(drift_ + dt * 0.05f * speed, 1024.0f);
    stepB_ = style_.bBase + style_.bAmp * std::sin(kTwoPi * time_ / style_.bPeriod);
    stepSeedEpoch_ = static_cast<float>(static_cast<int>(time_ / kSeedEpochSeconds));
    stepAdvect_ = dt * 0.18f * style_.flow * speed;
    stepDrive_ = safeAudioDrive(p.audioDrive);

    float decay = style_.decay;
    if (p.trails) decay += (1.0f - decay) * 0.6f * std::clamp(p.trailLength, 0.0f, 1.0f);
    stepDecay_ = std::pow(decay, dt * 60.0f);

    if (!sim_->step([this](sim::SimUniforms& u) { bindStep(u); })) return;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);
    glUseProgram(showProgram_);
    sim_->bindStateFor(showLocs_, 0);
    glUniform2f(showLocs_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1f(showLocs_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(showLocs_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(showLocs_.loc("uExposure"), style_.exposure);
    glUniform1i(showLocs_.loc("uFold"), style_.fold);
    glUniform1f(showLocs_.loc("uFoldPhase"), foldPhase_);
    glUniform1f(showLocs_.loc("uEnergy"), clampedBand(f.rms));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void SilkScene::bindStep(sim::SimUniforms& u) {
    u.i1("uField", style_.field);
    u.f1("uB", stepB_);
    u.f1("uAdvect", stepAdvect_);
    u.f1("uDecay", stepDecay_);
    u.f1("uFieldScale", style_.fieldScale);
    u.f1("uSwirl", style_.swirl);
    u.f1("uSlabX", std::cos(slabTurn_ * kTwoPi));
    u.f1("uSlabY", std::sin(slabTurn_ * kTwoPi));
    u.f1("uSeedEpoch", stepSeedEpoch_);
    u.f1("uDrift", drift_);
    u.f1("uStrokes", style_.strokes);
    u.f1("uElong", style_.elong);
    u.f1("uDrive", stepDrive_);
    u.f1("uBass", envBass_);
    u.f1("uMid", envMid_);
    u.f1("uTreble", envTreble_);
    u.f1("uBeat", beatPulse_);
    u.f1("uStrike", std::clamp(pcmStrike_, 0.0f, 1.5f));
    u.f1("uBeatRing", ringRadius_);
    const int count = touch_ ? touch_->count() : 0;
    u.i1("uTouchCount", count);
    if (count > 0 && touch_) u.f4Array("uTouchPoints", touch_->points().data(), count, TouchField::kMaxPoints);
}

void SilkScene::release() {
    if (sim_) sim_->release();
    sim_.reset();
    if (showProgram_ != 0) glDeleteProgram(showProgram_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    showLocs_ = UniformCache(0);
}

}  // namespace geode::viz
