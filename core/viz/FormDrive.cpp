#include "viz/FormDrive.hpp"

#include <algorithm>
#include <cmath>

#include "analysis/FrameLevels.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {

float smoothstep01(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float mix(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

FormDrive::FormDrive() { reset(); }

void FormDrive::reset() {
    live_ = Curve{};
    frozen_ = live_;
    blend_ = 1.0f;
    countLockout_ = 0.0f;
    spinRate_ = kSpinBaseRadPerSec;
    spinSign_ = 1.0f;
    turnAngle_ = 0.0f;
    turnTarget_ = 0.0f;
    spin_ = 0.0f;
    pcmFresh_ = false;
    pcmLevel_ = 0.0f;
    pcmPeak_ = 0.0f;
    pcmCrest_ = 0.0f;
    pcmGrit_ = 0.0f;
    level_ = 0.0f;
    swell_ = 0.0f;
    bite_ = 0.0f;
    grit_ = 0.0f;
    presence_ = 0.0f;
    bass_ = 0.0f;
    treble_ = 0.0f;
    bright_ = 0.0f;
    tilt_ = 0.0f;
    harmonic_ = 0.5f;
    build_ = 0.0f;
    pan_ = 0.0f;
    width_ = 0.0f;
    kick_ = HitEnvelope{};
    snare_ = HitEnvelope{};
    hat_ = HitEnvelope{};
    drop_ = HitEnvelope{};
    kickEnv_ = 0.0f;
    snareEnv_ = 0.0f;
    hatEnv_ = 0.0f;
    dropEnv_ = 0.0f;
    snareArmed_ = true;
    taps_.fill(0.0f);
    frozenTaps_.fill(0.0f);
    seedState_ = 0x2545f491u;
}

float FormDrive::radius(float theta, const Curve& c) {
    const float mt = static_cast<float>(c.m) * theta * 0.25f;
    const float ca = std::fabs(std::cos(mt) / c.a);
    const float sb = std::fabs(std::sin(mt) / c.b);
    const float sum = std::pow(ca, c.n2) + std::pow(sb, c.n3);
    if (!(sum > 0.0f) || !std::isfinite(sum)) return 1.0f;
    const float r = std::pow(sum, -1.0f / c.n1);
    return std::isfinite(r) ? r : 1.0f;
}

// The mean radius and the largest distance from it, over one turn. 4*pi is
// the true period of an odd count, but the mean over 2*pi is the same and
// the deviation only ever underestimates by the seam, which the floor covers.
FormDrive::RowStats FormDrive::rowStats(const Curve& curve) {
    float samples[kMeanSamples];
    float acc = 0.0f;
    for (int i = 0; i < kMeanSamples; i++) {
        samples[i] = radius(static_cast<float>(i) * kTwoPi / kMeanSamples, curve);
        acc += samples[i];
    }
    RowStats stats;
    stats.mean = acc / kMeanSamples;
    if (!(stats.mean > 1e-4f)) stats.mean = 1.0f;
    for (int i = 0; i < kMeanSamples; i++) stats.deviation = std::max(stats.deviation, std::fabs(samples[i] - stats.mean));
    return stats;
}

float FormDrive::tapOf(float r, const RowStats& stats) {
    const float scale = std::max(stats.deviation, kDeviationFloor * stats.mean);
    return std::clamp((r - stats.mean) / scale, -1.0f, 1.0f);
}

// exp(-dt*hz) rather than a fixed per-frame fraction: the same wall-clock rise
// on a 30fps device and a 120fps one.
float FormDrive::slew(float current, float target, float dt, float riseHz, float fallHz) {
    const float hz = target > current ? riseHz : fallHz;
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) * hz);
    return current + (target - current) * k;
}

float FormDrive::HitEnvelope::tick(float dt, float releaseHz) {
    if (attackLeft > 0.0f) {
        attackLeft -= dt;
        level = std::min(level + dt * peak / kDrumAttackSeconds, peak);
        if (level >= peak) attackLeft = 0.0f;
    } else {
        level *= std::exp(-dt * releaseHz);
        if (level < 1e-4f) level = 0.0f;
    }
    return level;
}

// A 32-bit LCG: cheap enough for a per-frame path, deterministic per instance.
float FormDrive::nextRandom() {
    seedState_ = seedState_ * 1664525u + 1013904223u;
    return static_cast<float>((seedState_ >> 8) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

int FormDrive::randomCount(int lo, int hi, int avoid) {
    const int span = hi - lo + 1;
    int pick = lo + std::min(static_cast<int>(nextRandom() * span), span - 1);
    if (pick == avoid) pick = pick < hi ? pick + 1 : lo;
    return pick;
}

void FormDrive::acceptPcm(const float* mono, int count) {
    if (!mono || count <= 0) return;
    const float rms = static_cast<float>(analysis::levels::rms(mono, count));
    const float peak = analysis::levels::peak(mono, count);
    pcmLevel_ = std::clamp(rms, 0.0f, 1.0f);
    pcmPeak_ = std::clamp(peak, 0.0f, 1.0f);
    // Crest factor above the 3 dB of a sine, mapped to 0..1: the higher, the
    // more the block is transient (a snare) rather than sustained (a pad).
    pcmCrest_ = rms > 1e-4f ? std::clamp((peak / rms - 1.41f) / 4.0f, 0.0f, 1.0f) : 0.0f;
    // Zero crossings per sample: 0 for sub bass, toward 0.5 for noise.
    pcmGrit_ = std::clamp(static_cast<float>(analysis::levels::zeroCrossingRate(mono, count)) * 2.5f, 0.0f, 1.0f);
    pcmFresh_ = true;
}

// A one-hop impulse only arrives once; a change of count is allowed on the
// frame it arrives and locked until the crossfade is readable as a count.
void FormDrive::retarget(int m) {
    m = std::clamp(m, kMinM, kMaxM);
    if (m == live_.m) return;
    frozen_ = live_;
    frozenTaps_ = taps_;
    live_.m = m;
    blend_ = 0.0f;
}

void FormDrive::stepCurve(const GeodeFeatureFrame& f, float dt) {
    const float n1Base = mix(kN1Percussive, kN1Tonal, std::clamp(harmonic_, 0.0f, 1.0f));
    const float n1Target = std::clamp(n1Base * (1.0f - kKickPinch * kickEnv_) * (1.0f - 0.3f * dropEnv_), kN1Floor, kN1Ceiling);
    const float body = kBodyExp * std::pow(2.0f, tilt_ * kBodyTiltGain);
    const float split = kBrightSplit * (bright_ - 0.5f) * 2.0f + kHatSplit * hatEnv_ + kCrestSplit * bite_;
    const float n2Target = std::clamp(body * (1.0f + split), kExpFloor, kExpCeiling);
    const float n3Target = std::clamp(body * (1.0f - split), kExpFloor, kExpCeiling);
    const float aTarget = std::max(1.0f + kPanWeight * pan_, kWeightFloor);
    const float bTarget = std::max(1.0f + kWidthWeight * (width_ - 0.5f) * 2.0f, kWeightFloor);
    live_.n1 = slew(live_.n1, n1Target, dt, kSharpnessHz, kSharpnessHz);
    live_.n2 = slew(live_.n2, n2Target, dt, kLobeHz, kLobeHz);
    live_.n3 = slew(live_.n3, n3Target, dt, kLobeHz, kLobeHz);
    live_.a = slew(live_.a, aTarget, dt, kWeightHz, kWeightHz);
    live_.b = slew(live_.b, bTarget, dt, kWeightHz, kWeightHz);
    (void) f;
}

void FormDrive::stepSpin(const GeodeFeatureFrame& f, float dt, bool snared, bool sectioned) {
    (void) f;
    if (sectioned) spinSign_ = -spinSign_;
    if (snared) turnTarget_ += spinSign_ * (kTurnMinRad + nextRandom() * (kTurnMaxRad - kTurnMinRad));
    const float turnStep = (turnTarget_ - turnAngle_) * (1.0f - std::exp(-dt * kTurnGlideHz));
    turnAngle_ += turnStep;
    // Keep the turn pair near zero without changing their difference.
    if (std::fabs(turnAngle_) > 64.0f) {
        turnTarget_ -= turnAngle_;
        turnAngle_ = 0.0f;
    }
    spinRate_ = kSpinBaseRadPerSec + kSpinEnergyRadPerSec * swell_;
    // 4*pi is a period of |cos(m*theta/4)| for every integer m, so wrapping
    // there is seamless whatever the count.
    spin_ = std::fmod(spin_ + spinSign_ * spinRate_ * dt + turnStep + 2.0f * kTwoPi, 2.0f * kTwoPi);
}

void FormDrive::sampleTaps() {
    const RowStats live = rowStats(live_);
    const bool fading = blend_ < 1.0f;
    const RowStats frozen = fading ? rowStats(frozen_) : RowStats{};
    for (int i = 0; i < kTaps; i++) {
        const float theta = spin_ + static_cast<float>(i) * kTwoPi / kTaps;
        float tap = tapOf(radius(theta, live_), live);
        if (fading) tap = mix(tapOf(radius(theta, frozen_), frozen), tap, blend_);
        taps_[static_cast<size_t>(i)] = tap;
    }
}

void FormDrive::step(const GeodeFeatureFrame& f, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);

    // ---- the PCM path: sample accurate, decays when no block arrived --------
    if (!pcmFresh_) {
        const float decay = std::exp(-dt * kPcmDecayHz);
        pcmLevel_ *= decay;
        pcmPeak_ *= decay;
    }
    pcmFresh_ = false;
    // The analyser's window RMS is on the same 0..1 scale; taking the louder of
    // the two keeps the driver alive on a host that never pushes PCM.
    const float levelTarget = std::max(pcmLevel_, std::clamp(f.rms, 0.0f, 1.0f));
    level_ = slew(level_, levelTarget, dt, kBandRiseHz, kBandFallHz);
    swell_ = slew(swell_, levelTarget, dt, kSwellRiseHz, kSwellFallHz);
    bite_ = slew(bite_, pcmCrest_, dt, kBandRiseHz, kBandFallHz);
    grit_ = slew(grit_, pcmGrit_, dt, kTiltHz, kTiltHz);
    presence_ = slew(presence_, smoothstep01(kPresenceFloor, kPresenceFull, levelTarget), dt, kPresenceRiseHz, kPresenceFallHz);

    // ---- the feature path: what the analyser knows about the music ----------
    bass_ = slew(bass_, std::clamp(f.bass, 0.0f, 1.5f), dt, kBandRiseHz, kBandFallHz);
    treble_ = slew(treble_, std::clamp(f.treble, 0.0f, 1.5f), dt, kBandRiseHz, kBandFallHz);
    bright_ = slew(bright_, std::clamp(f.centroid, 0.0f, 1.0f), dt, kTiltHz, kTiltHz);
    tilt_ = slew(tilt_, std::clamp(bass_ - treble_, -1.0f, 1.0f), dt, kTiltHz, kTiltHz);
    harmonic_ = slew(harmonic_, std::clamp(f.harmonicity, 0.0f, 1.0f), dt, kTiltHz, kTiltHz);
    build_ = slew(build_, std::clamp(f.buildup, 0.0f, 1.0f), dt, kTiltHz, kTiltHz);
    pan_ = slew(pan_, std::clamp(f.stereoPan, -1.0f, 1.0f), dt, kWeightHz, kWeightHz);
    width_ = slew(width_, std::clamp(f.stereoWidth, 0.0f, 1.0f), dt, kWeightHz, kWeightHz);

    if (f.kick > 0.0f) kick_.trigger(std::clamp(f.kick, 0.0f, 1.0f));
    if (f.snare > 0.0f) snare_.trigger(std::clamp(f.snare, 0.0f, 1.0f));
    if (f.hat > 0.0f) hat_.trigger(std::clamp(f.hat, 0.0f, 1.0f));
    if (f.drop > 0.0f) drop_.trigger(1.0f);
    kickEnv_ = kick_.tick(dt, kKickReleaseHz);
    snareEnv_ = snare_.tick(dt, kSnareReleaseHz);
    hatEnv_ = hat_.tick(dt, kHatReleaseHz);
    dropEnv_ = drop_.tick(dt, kDropReleaseHz);

    // ---- the count walk -----------------------------------------------------
    countLockout_ = std::max(countLockout_ - dt, 0.0f);
    const bool snareHot = f.snare >= kSnareGate;
    const bool snared = snareHot && snareArmed_;
    snareArmed_ = !snareHot;
    const bool sectioned = f.sectionBoundary > 0.0f;
    const bool dropped = f.drop > 0.0f;
    if (dropped) {
        retarget(randomCount(kDropMinM, kDropMaxM, live_.m));
        countLockout_ = kCountRefractorySeconds;
    } else if (sectioned) {
        retarget(randomCount(kMinM, kMaxM, live_.m));
        countLockout_ = kCountRefractorySeconds;
    } else if (snared && countLockout_ <= 0.0f) {
        int next;
        if (build_ > kBuildClimbLevel) {
            next = std::min(live_.m + 1, kMaxM);
        } else {
            next = live_.m + (nextRandom() < 0.5f ? -1 : 1);
            if (next < kMinM || next > kMaxM) next = live_.m - (next - live_.m);
        }
        retarget(next);
        countLockout_ = kCountRefractorySeconds;
    }
    if (blend_ < 1.0f) {
        blend_ += (1.0f - blend_) * (1.0f - std::exp(-dt * kCountBlendHz));
        if (blend_ >= 0.995f) blend_ = 1.0f;
    }

    stepCurve(f, dt);
    stepSpin(f, dt, snared, sectioned);
    sampleTaps();
}

// One tap feeds several parameters and every family reads some of them; the
// amounts are tuned so that formDrive = 1 on a loud passage moves a picture
// as far as the LFO slots at half depth would, and nothing here can flash:
// every luminance parameter is left to the safety clamp that runs next.
SceneParams FormDrive::apply(const SceneParams& p, bool reducedMotion) const {
    const float amount = std::clamp(p.formDrive, 0.0f, 1.0f) * presence_;
    if (amount <= 1e-4f) return p;
    const float g = reducedMotion ? amount * safety::kReducedMotionScale : amount;
    const auto& t = taps_;
    const float lobes = static_cast<float>(live_.m);
    SceneParams o = p;

    // Geometry every family sees: the shader styles through view(), the rest
    // through the composite pass. Rates (rotation, speed) get a bipolar tap so
    // the sense wanders with the shape; amplitudes get the lobe height.
    o.zoom = std::clamp(p.zoom * (1.0f + g * (0.16f * t[0] + 0.10f * kickEnv_ + 0.12f * dropEnv_)), 0.3f, 3.0f);
    o.rotation = std::clamp(p.rotation + g * (0.30f * t[1] + 0.15f * spinSign_ * swell_), -3.0f, 3.0f);
    o.sway = std::clamp(p.sway + g * 0.45f * std::fabs(t[2]) * swell_, 0.0f, 1.0f);
    o.warp = std::clamp(p.warp + g * 0.55f * (0.5f + 0.5f * t[3]) * (0.3f + 0.7f * bite_), 0.0f, 1.0f);
    o.ripple = std::clamp(p.ripple + g * 0.45f * (0.5f + 0.5f * t[4]) * level_, 0.0f, 1.0f);
    o.morph = std::clamp(p.morph + g * 0.22f * (0.5f + 0.5f * t[5]) * (1.0f - harmonic_), 0.0f, 1.0f);
    o.twist = std::clamp(p.twist + g * 0.55f * t[6], -1.0f, 1.0f);
    // The plane tiles up through a buildup and releases on the drop.
    o.tile = std::clamp(p.tile + g * 1.5f * build_ * (0.5f + 0.5f * t[7]), 1.0f, 6.0f);
    // The kaleidoscope, when the user turned it on, folds by the lobe count.
    if (p.kaleidoscope) o.symmetry = std::clamp(live_.m, 2, 12);
    o.fisheye = std::clamp(p.fisheye + g * 0.25f * t[0] * swell_, -1.0f, 1.0f);
    o.chromaAb = std::clamp(p.chromaAb + g * 0.30f * bite_ * std::max(t[1], 0.0f), 0.0f, 1.0f);

    // The scene clock and the forces inside the simulations.
    o.speed = std::clamp(p.speed * (1.0f + g * 0.5f * (swell_ - 0.3f)), 0.05f, 4.0f);
    o.turbulence = std::clamp(p.turbulence + g * 0.5f * (0.5f + 0.5f * t[2]) * grit_, 0.0f, 1.5f);
    o.particleSize = std::clamp(p.particleSize * (1.0f + g * 0.4f * t[3]), 0.3f, 2.5f);
    o.density = std::clamp(p.density * (1.0f + g * 0.3f * t[4] * swell_), 0.1f, 1.0f);
    o.trailLength = std::clamp(p.trailLength + g * 0.2f * t[5] * swell_, 0.05f, 0.98f);
    o.trailWarp = std::clamp(p.trailWarp + g * 0.3f * std::max(t[6], 0.0f) * bite_, 0.0f, 1.0f);

    // Colour: the palette breathes with the shape; nothing here is luminance.
    o.hueRange = std::clamp(p.hueRange * (1.0f + g * 0.3f * t[7]), 0.1f, 1.5f);
    o.colorShift = p.colorShift + g * 0.06f * t[0];

    // Fluid, Water and Cymatics: their own forces.
    o.fluidCurl = std::clamp(p.fluidCurl + g * 15.0f * t[1] * level_, 0.0f, 50.0f);
    o.fluidSplatRadius = std::clamp(p.fluidSplatRadius * (1.0f + g * 0.5f * t[2]), 0.02f, 0.4f);
    o.fluidSplatForce = std::clamp(p.fluidSplatForce * (1.0f + g * 0.6f * kickEnv_), 0.0f, 3.0f);
    o.fluidStirrerSpeed = std::clamp(p.fluidStirrerSpeed * (1.0f + g * 0.5f * swell_), 0.0f, 2.0f);
    o.flowStrength = std::clamp(p.flowStrength + g * 0.3f * std::max(t[3], 0.0f), 0.0f, 1.0f);
    o.waterFlow = std::clamp(p.waterFlow + g * 0.3f * t[4], 0.0f, 1.0f);
    o.waterRippleStrength = std::clamp(p.waterRippleStrength * (1.0f + g * 0.5f * t[5] * level_), 0.0f, 2.0f);
    o.waterWaveSpeed = std::clamp(p.waterWaveSpeed * (1.0f + g * 0.4f * swell_), 0.2f, 2.0f);
    o.cymaticsScale = std::clamp(p.cymaticsScale * (1.0f + g * 0.35f * t[6]), 0.5f, 8.0f);
    o.cymaticsRing = std::clamp(p.cymaticsRing + g * 0.2f * t[7], 0.0f, 1.0f);
    o.cymaticsFocus = std::clamp(p.cymaticsFocus + g * 0.2f * t[0], 0.0f, 1.0f);
    o.cymaticsSwirl = std::clamp(p.cymaticsSwirl + g * 0.15f * t[1] * grit_, -1.0f, 1.0f);
    o.cymaticsFlow = std::clamp(p.cymaticsFlow + g * 0.2f * (lobes / kMaxM) * swell_, 0.0f, 1.0f);
    return o;
}

}  // namespace geode::viz
