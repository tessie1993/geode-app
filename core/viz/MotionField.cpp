#include "viz/MotionField.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz {

MotionField::MotionField() { reset(); }

void MotionField::reset() {
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

void MotionField::setVariant(std::uint32_t seed) {
    // Spread neighbouring ids apart, then take a few independent picks from the bits.
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    hueSource_ = static_cast<int>(seed % 4u);
    oscClock_ = static_cast<int>((seed >> 4) % 3u);
    retargetOnKick16_ = ((seed >> 8) & 1u) != 0u;
}

void MotionField::step(const GeodeSoundFrame& f, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);

    // ---- relative levels: the engine's own (a band over its ~4 s mean, ~1 typical), smoothed --
    // The fast att envelopes, so the slew below sets the feel rather than the raw FFT jitter.
    const float energyRatio = std::clamp(std::isfinite(f.volAtt) ? f.volAtt : 1.0f, 0.0f, 2.0f);
    const float bassRatio = std::clamp(std::isfinite(f.bassAtt) ? f.bassAtt : 1.0f, 0.0f, 2.0f);
    const float midRatio = std::clamp(std::isfinite(f.midAtt) ? f.midAtt : 1.0f, 0.0f, 2.0f);
    const float trebRatio = std::clamp(std::isfinite(f.trebleAtt) ? f.trebleAtt : 1.0f, 0.0f, 2.0f);
    state_.energyRel = slew(state_.energyRel, energyRatio, dt, kEnergyAttackSeconds, kEnergyReleaseSeconds);
    state_.bassRel = slew(state_.bassRel, bassRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);
    state_.midRel = slew(state_.midRel, midRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);
    state_.trebRel = slew(state_.trebRel, trebRatio, dt, kBandAttackSeconds, kBandReleaseSeconds);

    // ---- timbre: brightness and tonality (the engine's flatness, inverted) ----
    state_.bright = oneOle(state_.bright, std::clamp(f.centroid, 0.0f, 1.0f), dt, kBrightSeconds);
    state_.harmony = oneOle(state_.harmony, std::clamp(1.0f - f.flatness, 0.0f, 1.0f), dt, kHarmonySeconds);

    // ---- hue: no fixed driver. Each style follows one of the melody's pitch class (sure only
    // while there is a pitch), the timbre, the per-note random or the per-kick random; all of
    // them eased around the colour wheel over seconds, so a jump becomes a glide. ----
    float hueTarget = keyHuePhase_;
    float hueStrength = 0.0f;
    switch (hueSource_) {
        case 0:
            if (f.pitchConfidence > 0.0f) hueTarget = std::clamp(f.pitchClass, 0.0f, 1.0f);
            hueStrength = std::clamp(f.pitchConfidence, 0.0f, 1.0f);
            break;
        case 1:
            hueTarget = std::clamp(f.centroid, 0.0f, 1.0f);
            hueStrength = std::clamp(f.presence, 0.0f, 1.0f);
            break;
        case 2:
            hueTarget = std::clamp(f.noteRandom, 0.0f, 1.0f);
            hueStrength = std::clamp(f.presence, 0.0f, 1.0f) * 0.8f;
            break;
        default:
            hueTarget = std::clamp(f.kickRandom, 0.0f, 1.0f);
            hueStrength = std::clamp(f.presence, 0.0f, 1.0f) * 0.6f;
            break;
    }
    keyHuePhase_ = std::fmod(keyHuePhase_ + wrappedDelta01(keyHuePhase_, hueTarget) * (1.0f - std::exp(-dt / kKeyHueSeconds)) + 1.0f, 1.0f);
    state_.keyHue = keyHuePhase_;
    state_.keyStrength = oneOle(state_.keyStrength, hueStrength, dt, kKeyStrengthSeconds);

    // ---- oscillators on a clock: the engine has no tempo tracker, so the beat and bar waves run on
    // a clock the music drives (per style: the hit count, the bass clock or the loudness clock).
    // They only move while music plays, which is what the old confidence gate was for. ----
    rhythmLock_ = oneOle(rhythmLock_, std::clamp(f.presence, 0.0f, 1.0f), dt, kRhythmLockSeconds);
    const float clock = oscClock_ == 0 ? f.hitTime : (oscClock_ == 1 ? 0.5f * f.bassTime : 0.5f * f.curvedTime);
    const float beatPhase = std::isfinite(clock) ? clock - std::floor(clock) : 0.0f;
    const float barClock = std::isfinite(clock) ? 0.25f * clock : 0.0f;
    const float barPhase = barClock - std::floor(barClock);
    const float beatSin = std::sin(kTwoPi * beatPhase);
    const float barSin = std::sin(kTwoPi * barPhase);
    barSin_ = barSin;
    state_.beatOsc = 0.5f + 0.5f * beatSin * rhythmLock_;
    state_.barOsc = 0.5f + 0.5f * barSin * rhythmLock_;

    // ---- orbit: a slow wander point, re-targeted by big moments (and, for some styles, every
    // sixteenth kick), never by single hits ----
    orbitRetargetLockout_ = std::max(orbitRetargetLockout_ - dt, 0.0f);
    const bool bigMoment = f.hardCut > 0.0f;
    const bool retarget = bigMoment || (retargetOnKick16_ && f.kick16 > 0.0f);
    if (retarget && orbitRetargetLockout_ <= 0.0f) {
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
    if (bigMoment) driftSignTarget_ = nextRandom() < 0.5f ? -1.0f : 1.0f;
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
SceneParams MotionField::apply(const SceneParams& p, bool reducedMotion, float spin) const {
    const float g = reducedMotion ? safety::kReducedMotionScale : 1.0f;
    // motionOrbit is not read here: uOrbit is a raw shader uniform (see
    // lib_scene_motion.glsl), and its own motionOrbit scaling happens in
    // view() (wave three's R01), not in this SceneParams-facing table.
    const float motionAmount = std::clamp(p.motionAmount, 0.0f, 1.0f) * g;
    const float motionBreath = std::clamp(p.motionBreath, 0.0f, 1.0f) * g;
    const float motionDrift = std::clamp(p.motionDrift, 0.0f, 1.0f) * g;
    const float motionHue = std::clamp(p.motionHue, 0.0f, 1.0f) * g;
    const auto& s = state_;
    SceneParams o = p;

    // A breath of 1 leaves zoom untouched; the dial fades toward that, not to
    // zero motion, so turning motionBreath down settles the picture rather
    // than freezing it.
    const float breath = 1.0f + (s.breath - 1.0f) * motionBreath;
    o.zoom = std::clamp(p.zoom * breath, 0.3f, 3.0f);
    if (spin != 0.0f) {
        o.rotation = std::clamp(p.rotation + s.drift * motionDrift * spin, -3.0f, 3.0f);
        o.sway = std::clamp(p.sway + motionAmount * 0.35f * std::fabs(barSin_) * rhythmLock_, 0.0f, 1.0f);
    }
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
