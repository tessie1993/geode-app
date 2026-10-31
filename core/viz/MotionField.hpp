#pragma once
#include <cstdint>
#include <string_view>

#include "api/geode_api.h"
#include "viz/Params.hpp"

namespace geode::viz {

// FNV-1a of a style id: MotionField::setVariant's seed, the same wherever the style is drawn.
inline std::uint32_t styleVariantSeed(std::string_view id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

// Wave three's continuous replacement for FormDrive. Nothing here is a
// trigger: every output is either the sound engine's relative level with
// attack/release smoothing, an oscillator on a music-driven clock, or a slow
// re-target that eases over seconds. Nothing follows a single drum hit; the
// only triggers read are the rare structural ones (a hard cut, and for some
// styles every sixteenth kick) that re-aim the orbit and the drift.
//
// Two independent instances of this class exist in the running app: one owned
// by Renderer (Renderer::resolveParams), whose apply() reshapes the
// SceneParams every family reads; and one owned by each ShaderScene, whose
// state() feeds the raw uniform contract in lib_scene_motion.glsl. Both are
// pure functions of the same GeodeSoundFrame/dt sequence, so both settle to
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
        float harmony = 0.5f;     // uHarmony: tonality (1 - flatness), 0..1
        float keyHue = 0.0f;      // uKeyHue: the style's hue driver, eased round the wheel, 0..1
        float keyStrength = 0.0f; // uKeyStrength: how much that driver is worth trusting, 0..1
        float beatOsc = 0.5f;     // uBeatOsc: oscillator on a music-driven clock, 0..1
        float barOsc = 0.5f;      // uBarOsc: the same clock, a quarter as fast, 0..1
        float orbitX = 0.0f;      // uOrbit.x, -1..1
        float orbitY = 0.0f;      // uOrbit.y, -1..1
        float drift = 0.0f;       // uDrift: accumulated angle, radians
        float breath = 1.0f;      // uBreath, 0.9..1.1
        float flowPhase = 0.0f;   // uFlowPhase: monotonic integrator
    };

    MotionField();

    void reset();
    // Which hue driver, oscillator clock and orbit re-target this instance
    // uses, picked from the style id's hash, so styles differ (the user asked
    // for no fixed default). The renderer's and the scene's instances get the
    // same seed for the same style, so they agree.
    void setVariant(std::uint32_t seed);
    // Advances every average, oscillator and integrator by one frame.
    void step(const GeodeSoundFrame& f, float dt);
    // Reshapes p with the state step() just computed; never touches
    // brightness/intensity/contrast/flash/strobe/pulse/shake. Reduced motion
    // scales the geometric terms the same way safety::apply scales rates.
    // spin is the style's SceneRegistry::spinRate: it scales and signs the drift
    // spin, and 0 (a style that does not turn) adds no drift spin or sway, so
    // rotation and sway stay what the user and the routes set.
    SceneParams apply(const SceneParams& p, bool reducedMotion, float spin = 1.0f) const;

    const State& state() const { return state_; }

private:

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

    // Orbit: target ease and radius.
    static constexpr float kOrbitEaseSeconds = 3.0f;
    static constexpr float kOrbitRadius = 0.8f;
    // Minimum spacing between re-targets so a burst of big moments cannot
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
    // Shortest signed distance from `from` to `to` on a unit-period circle.
    static float wrappedDelta01(float from, float to);
    float nextRandom();



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
    // apply() has no frame of its own to read the clock from.
    float barSin_ = 0.0f;
    // setVariant's picks: hue from pitch class (0), centroid (1), note random
    // (2) or kick random (3); oscillators on the hit (0), bass (1) or
    // loudness (2) clock; the orbit also re-aims every sixteenth kick.
    int hueSource_ = 0;
    int oscClock_ = 0;
    bool retargetOnKick16_ = false;

    State state_;

    // Deterministic per instance, so the same feature-frame sequence always
    // draws the same orbit targets and an exported frame is reproducible.
    unsigned int seedState_ = 0x51ed270bu;
};

}  // namespace geode::viz
