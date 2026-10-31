#include "viz/scenes/SilkScene.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"
#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"
#include "viz/Reactions.hpp"
#include "viz/fluid/FluidBuffers.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "SilkScene";

// lib_relief's looks, by number (RELIEF_SATIN ... in the shader).
enum Look : int { Satin = 0, Glass = 1, Chrome = 2, Neon = 3, Terra = 4, Membrane = 5, Nebula = 6 };

// What each style becomes besides its catalogue numbers, indexed by its field (unique per style):
// the material it starts in and the one a hard cut swaps to, the field the hard cut swaps to, the two
// extra fold counts kick16 steps through (between returns to the style's own), and the relief depth.
struct Variety {
    int lookBase;
    int lookAlt;
    int altField;
    int foldA;
    int foldB;
    float depth;
};
constexpr Variety kVariety[10] = {
    {Satin, Glass, 5, 0, 6, 0.055f},     // web
    {Membrane, Nebula, 3, 5, 0, 0.07f},  // bloom
    {Satin, Chrome, 4, 0, 4, 0.05f},     // weave
    {Glass, Membrane, 1, 4, 0, 0.065f},  // shell
    {Neon, Satin, 7, 3, 6, 0.06f},       // spiral
    {Chrome, Neon, 2, 6, 4, 0.06f},      // fold
    {Nebula, Glass, 8, 0, 3, 0.08f},     // hyper
    {Terra, Membrane, 0, 2, 0, 0.045f},  // resonance
    {Membrane, Chrome, 6, 0, 5, 0.06f},  // curl
    {Glass, Neon, 8, 6, 5, 0.065f},      // pendulum
};

const Variety& varietyOf(int field) { return kVariety[std::clamp(field, 0, 9)]; }

// A sound clock's advance since last frame: never negative, and 0 across its wrap or a seek.
float clockStep(float now, float& last) {
    const float d = now - last;
    last = now;
    return (std::isfinite(d) && d > 0.0f && d < 2.0f) ? d : 0.0f;
}

// One-pole slew toward `target` with time constant `seconds`, frame-rate independent.
float ease(float current, float target, float dt, float seconds) {
    if (!std::isfinite(target)) return current;
    return current + (target - current) * (1.0f - std::exp(-dt / std::max(seconds, 1e-3f)));
}

float unit01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
}  // namespace

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
    sound_.init();
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

void SilkScene::update(const GeodeSoundFrame& features, float dt) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 1.0f / 15.0f) : 0.0f;
    pcmStrike_ = pcmPulse_.tick(dt);
    sound_.step(features, dt);
    stepSound(sound_.frame(), dt);
}

// Everything the music does to the silk, integrated once per frame. Clocks only move forward, every
// envelope is dt-scaled, and every value that reaches a shader is clamped.
void SilkScene::stepSound(const GeodeSoundFrame& f, float dt) {
    const SceneParams& p = params_;
    const ReactSignals& sig = sound_.signals();
    const Variety& v = varietyOf(style_.field);
    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float beat = std::clamp(p.beatResponse, 0.0f, 2.0f);
    stepDrive_ = safeAudioDrive(p.audioDrive);

    // Band lanes: the dye each lane injects.
    envBass_ = slewEnvelope(envBass_, clampedBand(live::bass(f)), dt, kEnvRisePerSec, kEnvFallPerSec);
    envMid_ = slewEnvelope(envMid_, clampedBand(live::mid(f)), dt, kEnvRisePerSec, kEnvFallPerSec);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(live::treble(f)), dt, kEnvRisePerSec, kEnvFallPerSec);

    // Drums as three separate forces: kick pushes out from the ring centre, snare shears a vortex
    // around it (the turn flips with each snare), hat shimmers the fine treble strokes.
    kick_ = std::clamp(unit01(f.kickEnv) * beat, 0.0f, 1.5f);
    snare_ = std::clamp(unit01(f.snareEnv) * beat, 0.0f, 1.5f);
    hat_ = std::clamp(unit01(f.hatEnv) * beat, 0.0f, 1.5f);
    snareSign_ = f.snareToggle > 0.5f ? 1.0f : -1.0f;

    // Clocks: motion that speeds with the music and never runs backwards. A small floor of scene
    // time keeps the slab and folds drifting in silence, slowly.
    const float dBass = clockStep(f.bassTime, lastBassTime_);
    const float dMid = clockStep(f.midTime, lastMidTime_);
    const float dCurved = clockStep(f.curvedTime, lastCurvedTime_);
    const float dNote = clockStep(f.noteTime, lastNoteTime_);
    const float dHit = clockStep(f.hitTime, lastHitTime_);
    slabTurn_ = std::fmod(slabTurn_ + (dt * 0.25f + dBass) * style_.slabRate * speed, 1.0f);
    foldPhase_ = std::fmod(foldPhase_ + (dt * 0.01f + dMid * 0.05f + dHit * 0.04f) * speed * kTwoPi, kTwoPi);
    drift_ = std::fmod(drift_ + (dt * 0.01f + dCurved * 0.06f) * speed, 1024.0f);
    bPhase_ = std::fmod(bPhase_ + (dt * 0.2f + dCurved) / std::max(style_.bPeriod, 1.0f), 1.0f);
    melodyPhase_ = std::fmod(melodyPhase_ + dNote * 0.13f + dMid * 0.015f, 1.0f);

    // Build and release: the curved clock's rate is how hard the music drives the flow (quiet barely
    // moves the silk, loud races it), a held surge lifts it, silence hushes it until sound returns.
    const float curvedRate = dt > 0.0f ? std::clamp(dCurved / dt, 0.0f, 3.0f) : 0.0f;
    flowRate_ = ease(flowRate_, curvedRate, dt, 0.5f);
    surge_ = unit01(sig.value(ReactSource::Surge));
    if (f.silence > 0.0f) hush_ = 1.0f;
    if (f.returned > 0.0f || f.onset > 0.0f) hush_ = 0.0f;
    hush_ = std::max(hush_ - dt * 0.05f, 0.0f);
    trend_ = ease(trend_, unit01(f.energyTrend), dt, 4.0f);
    writhe_ = ease(writhe_, std::isfinite(f.flux) ? std::clamp((f.flux - 0.9f) * 0.7f, 0.0f, 1.2f) : 0.0f, dt, 0.25f);
    windX_ = ease(windX_, live::pan(f) * 0.5f, dt, 1.5f);

    // Ring: a kick throws a bass ring from a centre it picks (sides alternate on the kick toggle,
    // offset by the kick's random); a hard cut or sound returning throws a white one from the middle.
    if (f.kick * beat > 0.15f) {
        const float side = f.kickToggle > 0.5f ? 1.0f : -1.0f;
        ringCenterX_ = 0.5f + side * (0.08f + 0.22f * unit01(f.kickRandom));
        ringCenterY_ = 0.5f + (unit01(f.kickRandom) - 0.5f) * 0.4f;
        ringRadius_ = 0.0f;
        const float k = std::clamp(f.kick * beat, 0.0f, 1.5f);
        ringTint_[0] = k;
        ringTint_[1] = 0.15f * k;
        ringTint_[2] = 0.0f;
    }

    // Counted changes. Every 4th kick re-seats the stroke lattice; every 8th jumps the field's damping
    // to a new value; every 16th steps the fold count and the field zoom.
    sinceSeed_ += dt;
    if (f.kick4 > 0.0f || sinceSeed_ > kSeedFallbackSeconds) {
        seedEpoch_ = std::fmod(seedEpoch_ + 1.0f, kCountWrap);
        sinceSeed_ = 0.0f;
    }
    if (f.kick8 > 0.0f) bJumpTarget_ = (unit01(f.kickRandom) - 0.5f) * 1.6f * style_.bAmp;
    if (f.kick16 > 0.0f) kick16Count_ = (kick16Count_ + 1) % 4;
    // A hard cut (a section change) re-seeds big: the strokes, the slab, a white ring, and a flush
    // that clears most of the old dye so the alternate field (and material) start on a fresh canvas.
    if (f.hardCut > 0.0f || f.returned > 0.0f) {
        ringCenterX_ = 0.5f;
        ringCenterY_ = 0.5f;
        ringRadius_ = 0.0f;
        ringTint_[0] = ringTint_[1] = ringTint_[2] = 1.1f;
    }
    if (f.hardCut > 0.0f) {
        seedEpoch_ = std::fmod(seedEpoch_ + 7.0f, kCountWrap);
        sinceSeed_ = 0.0f;
        slabTurn_ = std::fmod(slabTurn_ + 0.25f + 0.5f * unit01(f.kickRandom), 1.0f);
        flush_ = 1.0f;
    }
    flush_ = std::max(flush_ - dt * 2.5f, 0.0f);
    if (ringRadius_ >= 0.0f) {
        ringRadius_ += dt * kRingSpeed * speed;
        const float fade = std::exp(-dt * 1.4f);
        for (float& c : ringTint_) c *= fade;
        if (ringRadius_ > kRingMax) ringRadius_ = -1.0f;
    }
    bJump_ = ease(bJump_, bJumpTarget_, dt, 0.8f);
    constexpr float kScaleSteps[4] = {1.0f, 0.85f, 1.0f, 1.2f};
    scale_ = ease(scale_, kScaleSteps[kick16Count_], dt, 1.2f);

    // Melody: an emitter that rises with the pitch and wanders on the note clock, brightest just after
    // a note change; glide and vibrato swirl the silk around it, the interval's sign picks the turn.
    const float confidence = unit01(f.pitchConfidence);
    if (f.noteChange > 0.0f && std::fabs(f.interval) > 1e-3f) swirlSign_ = f.interval > 0.0f ? 1.0f : -1.0f;
    const float targetX = 0.5f + 0.3f * std::sin(kTwoPi * melodyPhase_) + 0.08f * (unit01(f.noteRandom) - 0.5f);
    const float targetY = 0.18f + 0.64f * unit01(f.pitchHeight);
    const float follow = (f.legato > 0.0f || f.glide > 0.2f) ? 0.08f : 0.2f;
    emitX_ = std::clamp(ease(emitX_, targetX, dt, follow), 0.0f, 1.0f);
    emitY_ = std::clamp(ease(emitY_, targetY, dt, follow), 0.0f, 1.0f);
    const float noteHeld = unit01(sig.value(ReactSource::NoteChange));
    emit_ = ease(emit_, confidence * (0.2f + 0.8f * noteHeld), dt, 0.06f);
    const float swirlAmount = std::clamp(0.25f + unit01(f.glide) + 0.5f * unit01(f.vibrato), 0.0f, 1.5f);
    emitSwirl_ = ease(emitSwirl_, swirlSign_ * confidence * swirlAmount, dt, 0.3f);
    if (confidence > 0.3f) {
        float d = unit01(f.pitchClass) - melodyHue_;
        d -= std::round(d);
        melodyHue_ = hue::wrap01(melodyHue_ + d * (1.0f - std::exp(-dt / 0.35f)));
    }
    melodyAmt_ = ease(melodyAmt_, confidence, dt, 1.0f);

    // The step's numbers.
    stepB_ = std::clamp(style_.bBase + style_.bAmp * std::sin(kTwoPi * bPhase_) + bJump_, 0.06f, 0.4f);
    stepAdvect_ = dt * 0.18f * style_.flow * speed * (0.25f + 0.75f * flowRate_) * (1.0f + 0.7f * surge_) * (1.0f - 0.85f * hush_);
    float decay = style_.decay;
    if (p.trails) decay += (1.0f - decay) * 0.6f * std::clamp(p.trailLength, 0.0f, 1.0f);
    // Hushed, the last picture hangs; flushed, the old dye clears within a second.
    if (decay < 0.997f) decay += (0.997f - decay) * hush_;
    decay *= 1.0f - 0.08f * flush_;
    stepDecay_ = std::pow(std::clamp(decay, 0.5f, 0.999f), dt * 60.0f);
    // Brightness follows the energy trend only, gently (0.5 is steady).
    exposure_ = style_.exposure * (0.82f + 0.36f * trend_);
    depth_ = v.depth * (0.85f + 0.3f * unit01(f.presence));
}

void SilkScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_ || !sim_) return;
    resetFrameState();
    const SceneParams& p = params_;
    const Variety& v = varietyOf(style_.field);

    if (!sim_->step([this](sim::SimUniforms& u) { bindStep(u); })) return;

    // kick16 steps through the style's own fold, the first extra, its own again, the second extra.
    const int fold = kick16Count_ == 1 ? v.foldA : (kick16Count_ == 3 ? v.foldB : style_.fold);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);
    glUseProgram(showProgram_);
    sim_->bindStateFor(showLocs_, 0);
    sound_.upload(showLocs_, stepDrive_, kSoundTexUnit);
    glUniform2f(showLocs_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1f(showLocs_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(showLocs_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(showLocs_.loc("uExposure"), exposure_);
    glUniform1i(showLocs_.loc("uFold"), fold);
    glUniform1f(showLocs_.loc("uFoldPhase"), foldPhase_);
    glUniform1f(showLocs_.loc("uEnergy"), clampedBand(sound_.frame().rms));
    glUniform1f(showLocs_.loc("uMelodyHue"), melodyHue_);
    glUniform1f(showLocs_.loc("uMelodyAmt"), melodyAmt_);
    glUniform1i(showLocs_.loc("uLookBase"), v.lookBase);
    glUniform1i(showLocs_.loc("uLookAlt"), v.lookAlt);
    glUniform1f(showLocs_.loc("uDepth"), depth_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void SilkScene::bindStep(sim::SimUniforms& u) {
    // A hard cut swaps to the style's alternate field and the next swaps back: the same parity the
    // present pass's reliefLook uses, so the field and the material change together.
    const bool alt = static_cast<int>(sound_.signals().cutCount()) % 2 == 1;
    u.i1("uField", alt ? varietyOf(style_.field).altField : style_.field);
    u.f1("uB", stepB_);
    u.f1("uAdvect", stepAdvect_);
    u.f1("uDecay", stepDecay_);
    u.f1("uFieldScale", style_.fieldScale * scale_);
    u.f1("uSwirl", style_.swirl);
    u.f1("uSlabX", std::cos(slabTurn_ * kTwoPi));
    u.f1("uSlabY", std::sin(slabTurn_ * kTwoPi));
    u.f1("uSeedEpoch", seedEpoch_);
    u.f1("uDrift", drift_);
    u.f1("uStrokes", style_.strokes * (1.0f + 0.4f * surge_));
    u.f1("uElong", style_.elong);
    u.f1("uDrive", stepDrive_);
    u.f1("uBass", envBass_);
    u.f1("uMid", envMid_);
    u.f1("uTreble", envTreble_);
    u.f1("uKick", kick_);
    u.f1("uSnare", snare_);
    u.f1("uSnareSign", snareSign_);
    u.f1("uHat", hat_);
    u.f1("uStrike", std::clamp(pcmStrike_, 0.0f, 1.5f));
    u.f1("uBeatRing", ringRadius_);
    u.f2("uRingCenter", ringCenterX_, ringCenterY_);
    u.f3("uRingTint", ringTint_[0], ringTint_[1], ringTint_[2]);
    u.f2("uEmitter", emitX_, emitY_);
    u.f1("uEmit", std::clamp(emit_ * stepDrive_, 0.0f, 2.0f));
    u.f1("uEmitSwirl", emitSwirl_);
    u.f2("uWind", windX_, 0.0f);
    u.f1("uWrithe", writhe_);
    const int count = touch_ ? touch_->count() : 0;
    u.i1("uTouchCount", count);
    if (count > 0 && touch_) u.f4Array("uTouchPoints", touch_->points().data(), count, TouchField::kMaxPoints);
}

void SilkScene::release() {
    if (sim_) sim_->release();
    sim_.reset();
    sound_.release();
    if (showProgram_ != 0) glDeleteProgram(showProgram_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    showLocs_ = UniformCache(0);
}

}  // namespace geode::viz
