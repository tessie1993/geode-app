#include "viz/MotionField.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {
// A relative level below this reads as silence; flooring the running average
// here (rather than only at the ratio) keeps a ratio computed against it from
// blowing up when the passage has been quiet for a while.
constexpr float kAvgFloor = 0.02f;
}  // namespace

MotionField::MotionField() { reset(); }

void MotionField::reset() {
    rmsWarm_ = 0.0f;
    bassWarm_ = 0.0f;
    midWarm_ = 0.0f;
    trebWarm_ = 0.0f;
    noveltyWarm_ = 0.0f;
    rmsAvg_ = 1.0f;
    bassAvg_ = 1.0f;
    midAvg_ = 1.0f;
    trebAvg_ = 1.0f;
    noveltyAvg_ = 0.0f;
    rhythmLock_ = 0.0f;
    keyHuePhase_ = 0.0f;
    orbitTargetX_ = 0.0f;
    orbitTargetY_ = 0.0f;
    orbitCurX_ = 0.0f;
    orbitCurY_ = 0.0f;
    orbitRetargetLockout_ = 0.0f;
    driftAngle_ = 0.0f;
    driftSign_ = 1.0f;
    driftSignTarget_ = 1.0f;
    barSin_ = 0.0f;
    state_ = State{};
    seedState_ = 0x51ed270bu;
}

// exp(-dt/tau) rather than a fixed per-frame fraction: the same wall-clock
// rise on a 30fps device and a 120fps one.
float MotionField::slew(float current, float target, float dt, float riseSeconds, float fallSeconds) {
    const float tau = target > current ? riseSeconds : fallSeconds;
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tau, 1e-4f));
    return current + (target - current) * k;
}

float MotionField::oneOle(float current, float target, float dt, float tauSeconds) {
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tauSeconds, 1e-4f));
    return current + (target - current) * k;
}

float MotionField::runningAverage(float current, float sample, float dt, float tauSeconds, float& warmSeconds) {
    warmSeconds += std::max(dt, 0.0f);
    // The effective time constant ramps from `dt` (first sample: average jumps
    // straight to it) up to `tauSeconds` once that much has been observed, so
    // a ratio taken against this average never divides by a near-zero warm-up
    // value.
    const float tau = std::max(std::min(tauSeconds, warmSeconds), std::max(dt, 1e-3f));
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / tau);
    return current + (sample - current) * k;
}

float MotionField::wrappedDelta01(float from, float to) {
    float d = std::fmod(to - from + 1.5f, 1.0f);
    if (d < 0.0f) d += 1.0f;
    return d - 0.5f;
}

// A 32-bit LCG: cheap enough for a per-frame path, deterministic per instance.
float MotionField::nextRandom() {
    seedState_ = seedState_ * 1664525u + 1013904223u;
    return static_cast<float>((seedState_ >> 8) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

void MotionField::step(const GeodeFeatureFrame& f, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);

    // ---- relative levels: band / running average, attack/release smoothed --
    const float rmsSample = std::clamp(f.rms, 0.0f, 1.0f);
    const float bassSample = std::clamp(f.bass, 0.0f, 1.5f);
    const float midSample = std::clamp(f.mid, 0.0f, 1.5f);
    const float trebSample = std::clamp(f.treble, 0.0f, 1.5f);
    rmsAvg_ = runningAverage(rmsAvg_, rmsSample, dt, kRmsAvgSeconds, rmsWarm_);
    bassAvg_ = runningAverage(bassAvg_, bassSample, dt, kBandAvgSeconds, bassWarm_);
    midAvg_ = runningAverage(midAvg_, midSample, dt, kBandAvgSeconds, midWarm_);
    trebAvg_ = runningAverage(trebAvg_, trebSample, dt, kBandAvgSeconds, trebWarm_);

    const float energyRatio = std::clamp(rmsSample / std::max(rmsAvg_, kAvgFloor), 0.0f, 2.0f);
    const float bassRatio = std::clamp(bassSample / std::max(bassAvg_, kAvgFloor), 0.0f, 2.0f);
    const float midRatio = std::clamp(midSample / std::max(midAvg_, kAvgFloor), 0.0f, 2.0f);
    const float trebRatio = std::clamp(trebSample / std::max(trebAvg_, kAvgFloor), 0.0f, 2.0f);
    state_.energyRel = slew(state_.energyRel, energyRatio, dt, kEnergyAttackSeconds, kEnergyReleaseSeconds);
    state_.bassRel = slew(state_.bassRel, bassRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);
    state_.midRel = slew(state_.midRel, midRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);
    state_.trebRel = slew(state_.trebRel, trebRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);

    // ---- timbre: brightness and harmonicity, symmetric one-poles ------------
    state_.bright = oneOle(state_.bright, std::clamp(f.centroid, 0.0f, 1.0f), dt, kBrightSeconds);
    state_.harmony = oneOle(state_.harmony, std::clamp(f.harmonicity, 0.0f, 1.0f), dt, kHarmonySeconds);

    // ---- key: chroma argmax hue, circularly eased ---------------------------
    int argmax = 0;
    float best = f.chroma[0];
    for (int i = 1; i < GEODE_CHROMA_BINS; ++i) {
        if (f.chroma[i] > best) {
            best = f.chroma[i];
            argmax = i;
        }
    }
    const float keyHueTarget = static_cast<float>(argmax) / static_cast<float>(GEODE_CHROMA_BINS);
    keyHuePhase_ = std::fmod(keyHuePhase_ + wrappedDelta01(keyHuePhase_, keyHueTarget) * (1.0f - std::exp(-dt / kKeyHueSeconds)) + 1.0f, 1.0f);
    state_.keyHue = keyHuePhase_;
    state_.keyStrength = oneOle(state_.keyStrength, std::clamp(f.chromaConfidence, 0.0f, 1.0f), dt, kKeyStrengthSeconds);

    // ---- tempo phase: phase-locked oscillators, gated by confidence ---------
    const float lockTarget = std::clamp(f.pulseConfidence, 0.0f, 1.0f) * std::clamp(f.tempoStability, 0.0f, 1.0f);
    rhythmLock_ = oneOle(rhythmLock_, lockTarget, dt, kRhythmLockSeconds);
    const float beatSin = std::sin(kTwoPi * f.beatPhase);
    const float barSin = std::sin(kTwoPi * f.barPhase);
    barSin_ = barSin;
    state_.beatOsc = 0.5f + 0.5f * beatSin * rhythmLock_;
    state_.barOsc = 0.5f + 0.5f * barSin * rhythmLock_;

    // ---- orbit: a slow wander point, re-targeted by structure not hits ------
    const float noveltySample = std::max(f.novelty, 0.0f);
    noveltyAvg_ = runningAverage(noveltyAvg_, noveltySample, dt, kNoveltyAvgSeconds, noveltyWarm_);
    orbitRetargetLockout_ = std::max(orbitRetargetLockout_ - dt, 0.0f);
    const bool noveltySpike = noveltySample > noveltyAvg_ * kNoveltyJumpRatio && noveltyAvg_ > 1e-3f;
    const bool sectionBoundary = f.sectionBoundary > 0.0f;
    if ((noveltySpike || sectionBoundary) && orbitRetargetLockout_ <= 0.0f) {
        const float theta = nextRandom() * kTwoPi;
        orbitTargetX_ = std::cos(theta);
        orbitTargetY_ = std::sin(theta);
        orbitRetargetLockout_ = kOrbitRetargetRefractorySeconds;
    }
    orbitCurX_ = oneOle(orbitCurX_, orbitTargetX_, dt, kOrbitEaseSeconds);
    orbitCurY_ = oneOle(orbitCurY_, orbitTargetY_, dt, kOrbitEaseSeconds);
    const float orbitScale = kOrbitRadius * state_.energyRel;
    state_.orbitX = std::clamp(orbitCurX_ * orbitScale, -1.0f, 1.0f);
    state_.orbitY = std::clamp(orbitCurY_ * orbitScale, -1.0f, 1.0f);

    // ---- drift: a rotation that eases its sign, never flips it outright -----
    if (sectionBoundary) driftSignTarget_ = nextRandom() < 0.5f ? -1.0f : 1.0f;
    driftSign_ = oneOle(driftSign_, driftSignTarget_, dt, kDriftSignEaseSeconds);
    const float driftRate = driftSign_ * (kDriftBaseRadPerSec + kDriftMidRadPerSec * state_.midRel);
    // Wrapped rather than free-running: state_.drift only ever feeds a bounded
    // rotation-rate contribution in apply(), never an absolute angle drawn on
    // screen, so wrapping it costs nothing and keeps float precision over a
    // long session.
    driftAngle_ = std::fmod(driftAngle_ + driftRate * dt + 2.0f * kTwoPi, 2.0f * kTwoPi);
    if (driftAngle_ > kTwoPi) driftAngle_ -= 2.0f * kTwoPi;
    state_.drift = driftAngle_;

    // ---- breath: a slow, low-amplitude scale wobble --------------------------
    const float breathTarget = 1.0f + kBreathBassWeight * (state_.bassRel - 1.0f) + kBreathBarWeight * (state_.barOsc - 0.5f);
    state_.breath = std::clamp(oneOle(state_.breath, breathTarget, dt, kBreathSeconds), kBreathLo, kBreathHi);

    // ---- flow phase: monotonic, rate set by loudness, never the sign --------
    state_.flowPhase = std::fmod(state_.flowPhase + dt * (kFlowBaseHz + kFlowEnergyHz * state_.energyRel), kTimeWrapSeconds);
}

// Reshapes every family's parameters from the continuous state step() just
// computed. Nothing here is a trigger, and nothing here writes brightness,
// intensity, contrast, flash, strobe, pulse or shake: those stay exactly what
// the caller passed in and go through the safety clamp unchanged.
SceneParams MotionField::apply(const SceneParams& p, bool reducedMotion) const {
    const float g = reducedMotion ? safety::kReducedMotionScale : 1.0f;
    const float motionAmount = std::clamp(p.motionAmount, 0.0f, 1.0f) * g;
    const float motionBreath = std::clamp(p.motionBreath, 0.0f, 1.0f) * g;
    const float motionDrift = std::clamp(p.motionDrift, 0.0f, 1.0f) * g;
    const float motionHue = std::clamp(p.motionHue, 0.0f, 1.0f) * g;
    const float motionOrbit = std::clamp(p.motionOrbit, 0.0f, 1.0f) * g;
    const auto& s = state_;
    SceneParams o = p;

    // A breath of 1 leaves zoom untouched; the dial fades toward that, not to
    // zero motion, so turning motionBreath down settles the picture rather
    // than freezing it.
    const float breath = 1.0f + (s.breath - 1.0f) * motionBreath;
    o.zoom = std::clamp(p.zoom * breath, 0.3f, 3.0f);
    o.rotation = std::clamp(p.rotation + s.drift * motionDrift, -3.0f, 3.0f);
    // uOrbit itself cannot be read from view() (lib_scene_motion.glsl, which
    // declares it, is included after lib_scene_uniforms.glsl - see the note
    // that used to live here), so the orbit reaches the picture through
    // driftX/driftY instead, which view() already consumes. kOrbitToDrift
    // caps the orbit's contribution at 10% of driftX/driftY's own -1..1
    // range, so at the orbit's full swing and motionOrbit at max it reads as
    // a gentle bias on top of the user's own drift, never a second drift
    // dial in disguise.
    constexpr float kOrbitToDrift = 0.1f;
    o.driftX = std::clamp(p.driftX + s.orbitX * motionOrbit * kOrbitToDrift, -1.0f, 1.0f);
    o.driftY = std::clamp(p.driftY + s.orbitY * motionOrbit * kOrbitToDrift, -1.0f, 1.0f);
    o.sway = std::clamp(p.sway + motionAmount * 0.35f * std::fabs(barSin_) * rhythmLock_, 0.0f, 1.0f);
    o.warp = std::clamp(p.warp + motionAmount * 0.3f * (1.0f - s.harmony), 0.0f, 1.0f);
    o.morph = std::clamp(p.morph + motionAmount * (1.0f - s.harmony), 0.0f, 1.0f);
    o.speed = std::clamp(p.speed * (0.7f + 0.5f * s.energyRel), 0.05f, 4.0f);
    // The key's hue, gated by how confident the key detector is, offset
    // slightly further by how bright the passage reads.
    const float keyHueOffset = s.keyStrength * wrappedDelta01(0.5f, s.keyHue);
    o.colorShift = p.colorShift + motionHue * (keyHueOffset + 0.1f * (s.bright - 0.5f));

    o.turbulence = std::clamp(p.turbulence + motionAmount * 0.35f * (s.trebRel - 1.0f), 0.0f, 1.5f);
    o.particleSize = std::clamp(p.particleSize * (1.0f + motionAmount * 0.2f * (s.energyRel - 1.0f)), 0.3f, 2.5f);
    o.density = std::clamp(p.density * (1.0f + motionAmount * 0.15f * (s.energyRel - 1.0f)), 0.1f, 1.0f);
    o.trailLength = std::clamp(p.trailLength + motionAmount * 0.15f * (s.energyRel - 1.0f), 0.05f, 0.98f);

    o.fluidCurl = std::clamp(p.fluidCurl + motionAmount * 10.0f * (s.bassRel - 1.0f), 0.0f, 50.0f);
    o.fluidStirrerSpeed = std::clamp(p.fluidStirrerSpeed * (1.0f + motionAmount * 0.4f * (s.midRel - 1.0f)), 0.0f, 2.0f);
    o.fluidSplatForce = std::clamp(p.fluidSplatForce * (1.0f + motionAmount * 0.3f * (s.bassRel - 1.0f)), 0.0f, 3.0f);

    o.cymaticsFlow = std::clamp(p.cymaticsFlow + motionAmount * 0.25f * (s.bassRel - 1.0f), 0.0f, 1.0f);
    o.cymaticsSwirl = std::clamp(p.cymaticsSwirl + motionAmount * 0.2f * (1.0f - s.harmony), -1.0f, 1.0f);
    o.cymaticsRing = std::clamp(p.cymaticsRing + motionAmount * 0.15f * (s.bassRel - 1.0f), 0.0f, 1.0f);

    return o;
}

}  // namespace geode::viz
