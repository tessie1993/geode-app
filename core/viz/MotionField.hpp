#pragma once
#include "api/geode_api.h"
#include "viz/Params.hpp"

namespace geode::viz {

// Wave three's continuous replacement for FormDrive. Nothing here is a
// trigger: every output is either a running-average ratio with attack/release
// smoothing, a phase-locked oscillator gated by confidence, or a slow
// re-target that eases over seconds. Nothing reads kick/snare/hat/drop,
// transient, onset, flux, beat, downbeat or any one-hop flag - see
// GeodeFeatureFrame in core/api/geode_api.h for what those are and why they
// are excluded.
//
// Two independent instances of this class exist in the running app: one owned
// by Renderer (Renderer::resolveParams), whose apply() reshapes the
// SceneParams every family reads; and one owned by each ShaderScene, whose
// state() feeds the raw uniform contract in lib_scene_motion.glsl. Both are
// pure functions of the same GeodeFeatureFrame/dt sequence, so both settle to
// nearly the same values without being bit-identical to each other; each is
// deterministic given its own input sequence, which is what keeps an
// exported frame reproducible.
class MotionField {
public:
    // The continuous state ShaderScene (and, from R07, the CPU-side families)
    // read to build the uniform contract. Deliberately plain data: nothing
    // here is a SceneParams field, because none of it is a user dial.
    struct State {
        float energyRel = 1.0f;   // uEnergyRel: rms / 20s running average, 0..2
        float bassRel = 1.0f;     // uBassRel
        float midRel = 1.0f;      // uMidRel
        float trebRel = 1.0f;     // uTrebRel
        float bright = 0.0f;      // uBright: spectral centroid, 0..1
        float harmony = 0.5f;     // uHarmony: harmonicity, 0..1
        float keyHue = 0.0f;      // uKeyHue: chroma argmax hue, 0..1
        float keyStrength = 0.0f; // uKeyStrength: chromaConfidence, 0..1
        float beatOsc = 0.5f;     // uBeatOsc: phase-locked oscillator, 0..1
        float barOsc = 0.5f;      // uBarOsc: phase-locked oscillator, 0..1
        float orbitX = 0.0f;      // uOrbit.x, -1..1
        float orbitY = 0.0f;      // uOrbit.y, -1..1
        float drift = 0.0f;       // uDrift: accumulated angle, radians
        float breath = 1.0f;      // uBreath, 0.9..1.1
        float flowPhase = 0.0f;   // uFlowPhase: monotonic integrator
    };

    MotionField();

    void reset();
    // Advances every running average, oscillator and integrator by one frame.
    void step(const GeodeFeatureFrame& f, float dt);
    // Reshapes p with the state step() just computed; never touches
    // brightness/intensity/contrast/flash/strobe/pulse/shake. Reduced motion
    // scales the geometric terms the same way safety::apply scales rates.
    SceneParams apply(const SceneParams& p, bool reducedMotion) const;

    const State& state() const { return state_; }

private:
    // Running-average time constants (seconds).
    static constexpr float kRmsAvgSeconds = 20.0f;
    static constexpr float kBandAvgSeconds = 20.0f;
    static constexpr float kNoveltyAvgSeconds = 10.0f;

    // Relative-level attack/release (seconds).
    static constexpr float kEnergyAttackSeconds = 0.25f;
    static constexpr float kEnergyReleaseSeconds = 1.0f;
    static constexpr float kBandAttackSeconds = 0.15f;
    static constexpr float kBandReleaseSeconds = 0.6f;

    // Symmetric one-pole time constants (seconds).
    static constexpr float kBrightSeconds = 0.5f;
    static constexpr float kHarmonySeconds = 1.0f;
    static constexpr float kKeyHueSeconds = 3.0f;
    static constexpr float kKeyStrengthSeconds = 1.0f;
    static constexpr float kRhythmLockSeconds = 1.0f;
    static constexpr float kBreathSeconds = 0.5f;

    // Orbit: novelty threshold, target ease and radius.
    static constexpr float kNoveltyJumpRatio = 1.5f;
    static constexpr float kOrbitEaseSeconds = 3.0f;
    static constexpr float kOrbitRadius = 0.8f;
    // Minimum spacing between re-targets so a noisy novelty signal cannot
    // re-draw the target every frame once it crosses the threshold.
    static constexpr float kOrbitRetargetRefractorySeconds = 1.0f;

    // Drift integrator.
    static constexpr float kDriftBaseRadPerSec = 0.02f;
    static constexpr float kDriftMidRadPerSec = 0.08f;
    static constexpr float kDriftSignEaseSeconds = 3.0f;

    // Breath shaping.
    static constexpr float kBreathBassWeight = 0.06f;
    static constexpr float kBreathBarWeight = 0.02f;
    static constexpr float kBreathLo = 0.9f;
    static constexpr float kBreathHi = 1.1f;

    // Flow phase (unchanged from ShaderScene's original constants).
    static constexpr float kFlowBaseHz = 0.08f;
    static constexpr float kFlowEnergyHz = 0.22f;

    static constexpr float kTwoPi = 6.2831853f;
    static constexpr float kTimeWrapSeconds = 7100.0f;

    static float slew(float current, float target, float dt, float riseSeconds, float fallSeconds);
    static float oneOle(float current, float target, float dt, float tauSeconds);
    // Exponential running average with a warm-up so the first seconds do not
    // divide by ~0: the average starts equal to the first sample and its
    // effective time constant ramps from `dt` up to `tauSeconds`.
    static float runningAverage(float current, float sample, float dt, float tauSeconds, float& warmSeconds);
    // Shortest signed distance from `from` to `to` on a unit-period circle.
    static float wrappedDelta01(float from, float to);
    float nextRandom();

    // Warm-up accumulators for the running averages (seconds observed so far).
    float rmsWarm_ = 0.0f;
    float bassWarm_ = 0.0f;
    float midWarm_ = 0.0f;
    float trebWarm_ = 0.0f;
    float noveltyWarm_ = 0.0f;

    float rmsAvg_ = 1.0f;
    float bassAvg_ = 1.0f;
    float midAvg_ = 1.0f;
    float trebAvg_ = 1.0f;
    float noveltyAvg_ = 0.0f;

    float rhythmLock_ = 0.0f;

    float keyHuePhase_ = 0.0f;

    float orbitTargetX_ = 0.0f;
    float orbitTargetY_ = 0.0f;
    float orbitCurX_ = 0.0f;
    float orbitCurY_ = 0.0f;
    float orbitRetargetLockout_ = 0.0f;

    float driftAngle_ = 0.0f;
    float driftSign_ = 1.0f;
    float driftSignTarget_ = 1.0f;

    // The current bar phase's sine, cached from step() for sway in apply();
    // apply() has no GeodeFeatureFrame of its own to read barPhase from.
    float barSin_ = 0.0f;

    State state_;

    // Deterministic per instance, so the same feature-frame sequence always
    // draws the same orbit targets and an exported frame is reproducible.
    unsigned int seedState_ = 0x51ed270bu;
};

}  // namespace geode::viz
