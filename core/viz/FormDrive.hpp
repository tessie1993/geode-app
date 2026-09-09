#pragma once
#include <array>

#include "api/geode_api.h"
#include "viz/Params.hpp"

namespace geode::viz {

// The Gielis superformula as the ONE modulator every scene family is driven
// through:
//
//     r(theta) = ( |cos(m*theta/4) / a|^n2 + |sin(m*theta/4) / b|^n3 )^(-1/n1)
//
// Six numbers draw a circle (n1 = n2 = n3 = 2), a rounded m-gon (n2, n3
// above 2), an m-pointed star (n1 well below 1) and everything between, and
// they draw them CONTINUOUSLY. That is what makes the formula the right thing
// to hang a music reaction on: a level can only make a picture bigger, a
// shape can make it a different thing.
//
// Where it sits: Renderer::resolveParams, after the LFO and ADSR slots and
// before the safety clamp - the stage every scene's SceneParams come out of.
// Nothing style-specific is touched. The fragment styles read the modulated
// zoom/rotation/warp/ripple/morph/twist/tile/sway through the shared view()
// uniforms, the particle and simulation families through the composite
// pass's geo() plus their own speed/turbulence/particle/trail reads, Fluid,
// Water and Cymatics through their solver parameters, MilkDrop through zoom,
// rotation and hue. One driver, one place, all families.
//
// What feeds it: the mono PCM block the renderer already receives every frame
// (block RMS, peak, crest factor and zero-crossing rate, sample-accurate) plus
// the analyser's feature frame (bands, drums, harmonicity, buildup/drop/
// section/arrival, bar phase, stereo field). The PCM path is what keeps the
// reaction tight; the feature path is what makes it musical.
//
// What the music moves:
//   m   (lobe count)   an INTEGER plateau. Snares walk it (upward through a
//                      buildup), a drop resets it to a bold low count, a
//                      section boundary re-rolls it. Every change is a
//                      crossfade between the frozen outgoing curve and the
//                      live incoming one, never a glide of m itself: a
//                      non-integer m does not close over 2*pi.
//   n1  (sharpness)    tonal passages sit round, percussive ones spiky, and a
//                      kick pinches the curve toward a star for a moment.
//   n2, n3 (lobe form) the spectral tilt sets one body exponent (bass fattens,
//                      brightness thins); hats and the PCM crest split the
//                      cosine and sine lobes apart.
//   a, b (weights)     stereo pan and width.
//   spin               energy sets the rate, a section boundary the sense, a
//                      snare a bounded turn.
//
// How it reaches a parameter: the curve is sampled at kTaps evenly spaced
// angles riding the spin and expressed as "how far from the circle here",
// -1..1: the radius minus the mean radius, over the curve's own largest
// deviation. Dividing by the deviation is what keeps the reaction full-scale
// whatever the exponents happen to draw - a rounded m-gon and a star both
// swing the taps to their limits, one softly and one sharply - while a floor
// on the deviation lets a true circle (silence, no verdict on the music yet)
// leave everything alone. Each tap is a bipolar modulator with the character
// of the shape, and every tap feeds several parameters. A `presence` gate
// (the PCM level) fades the whole reaction out in silence, so a paused player
// rests instead of wobbling.
//
// Deterministic: every random choice comes from a seeded LCG, so the same
// audio gives the same shapes and an exported frame is reproducible.
class FormDrive {
public:
    static constexpr int kTaps = 8;

    struct Curve {
        int m = 6;
        float n1 = 2.0f;
        float n2 = 2.0f;
        float n3 = 2.0f;
        float a = 1.0f;
        float b = 1.0f;
    };

    FormDrive();

    void reset();
    // The freshest mono PCM block; call once per push, not once per frame.
    void acceptPcm(const float* mono, int count);
    void step(const GeodeFeatureFrame& f, float dt);
    // Modulates every family's parameters by p.formDrive; reduced motion scales
    // the geometric part down the same way the safety clamp scales rates.
    SceneParams apply(const SceneParams& p, bool reducedMotion) const;

    // The raw formula. `theta` in radians; a and b must be positive, n1 non-zero.
    static float radius(float theta, const Curve& curve);

    const Curve& curve() const { return live_; }
    float spin() const { return spin_; }
    float presence() const { return presence_; }
    const std::array<float, kTaps>& taps() const { return taps_; }

private:
    static constexpr float kTwoPi = 6.2831853f;
    static constexpr int kMeanSamples = 64;
    // The deviation (as a fraction of the mean radius) a curve needs before
    // the taps reach full scale; rounder curves modulate proportionally less.
    static constexpr float kDeviationFloor = 0.05f;

    // Lobe counts. Twelve is where a silhouette stops reading as a count.
    static constexpr int kMinM = 3;
    static constexpr int kMaxM = 12;
    static constexpr int kDropMinM = 3;
    static constexpr int kDropMaxM = 5;
    // How long after a snare-driven count change the next snare may change it
    // again; at kCountBlendHz the crossfade is 97% done by then.
    static constexpr float kCountRefractorySeconds = 1.4f;
    static constexpr float kCountBlendHz = 2.5f;
    static constexpr float kSnareGate = 0.3f;
    static constexpr float kBuildClimbLevel = 0.45f;

    // Exponents.
    static constexpr float kN1Percussive = 0.45f;
    static constexpr float kN1Tonal = 2.4f;
    static constexpr float kN1Floor = 0.25f;
    static constexpr float kN1Ceiling = 6.0f;
    static constexpr float kKickPinch = 0.55f;
    static constexpr float kBodyExp = 2.0f;
    static constexpr float kBodyTiltGain = 1.2f;
    static constexpr float kExpFloor = 0.3f;
    static constexpr float kExpCeiling = 8.0f;
    static constexpr float kBrightSplit = 0.2f;
    static constexpr float kHatSplit = 0.5f;
    static constexpr float kCrestSplit = 0.25f;
    static constexpr float kPanWeight = 0.35f;
    static constexpr float kWidthWeight = 0.3f;
    static constexpr float kWeightFloor = 0.5f;

    // Slew rates, per second (one-pole, framerate independent).
    static constexpr float kBandRiseHz = 8.0f;
    static constexpr float kBandFallHz = 2.6f;
    static constexpr float kSwellRiseHz = 1.6f;
    static constexpr float kSwellFallHz = 0.8f;
    static constexpr float kTiltHz = 3.0f;
    static constexpr float kSharpnessHz = 6.0f;
    static constexpr float kLobeHz = 4.0f;
    static constexpr float kWeightHz = 2.0f;
    static constexpr float kPresenceRiseHz = 4.0f;
    static constexpr float kPresenceFallHz = 0.7f;
    static constexpr float kPcmDecayHz = 2.0f;

    // Drum envelopes: one-hop impulses shaped into something a picture can
    // follow, with a linear attack and an exponential release.
    static constexpr float kDrumAttackSeconds = 0.03f;
    static constexpr float kKickReleaseHz = 5.0f;
    static constexpr float kSnareReleaseHz = 4.0f;
    static constexpr float kHatReleaseHz = 9.0f;
    static constexpr float kDropReleaseHz = 0.6f;

    // Spin.
    static constexpr float kSpinBaseRadPerSec = 0.05f;
    static constexpr float kSpinEnergyRadPerSec = 0.4f;
    static constexpr float kTurnMinRad = 0.25f;
    static constexpr float kTurnMaxRad = 0.6f;
    static constexpr float kTurnGlideHz = 1.5f;

    // Presence: the PCM level a passage has to reach for the full reaction.
    static constexpr float kPresenceFloor = 0.02f;
    static constexpr float kPresenceFull = 0.18f;

    struct HitEnvelope {
        float level = 0.0f;
        float attackLeft = 0.0f;
        float peak = 0.0f;
        void trigger(float strength) {
            peak = strength > level ? strength : level;
            attackLeft = kDrumAttackSeconds;
        }
        float tick(float dt, float releaseHz);
    };

    static float slew(float current, float target, float dt, float riseHz, float fallHz);
    float nextRandom();
    int randomCount(int lo, int hi, int avoid);
    void retarget(int m);
    void stepCurve(const GeodeFeatureFrame& f, float dt);
    void stepSpin(const GeodeFeatureFrame& f, float dt, bool snared, bool sectioned);
    struct RowStats {
        float mean = 1.0f;
        float deviation = 0.0f;
    };
    void sampleTaps();
    static RowStats rowStats(const Curve& curve);
    static float tapOf(float r, const RowStats& stats);

    Curve live_;
    Curve frozen_;
    float blend_ = 1.0f;
    float countLockout_ = 0.0f;
    float spinRate_ = kSpinBaseRadPerSec;
    float spinSign_ = 1.0f;
    float turnAngle_ = 0.0f;
    float turnTarget_ = 0.0f;
    float spin_ = 0.0f;

    // PCM-derived, sample accurate.
    bool pcmFresh_ = false;
    float pcmLevel_ = 0.0f;
    float pcmPeak_ = 0.0f;
    float pcmCrest_ = 0.0f;
    float pcmGrit_ = 0.0f;
    float level_ = 0.0f;
    float swell_ = 0.0f;
    float bite_ = 0.0f;
    float grit_ = 0.0f;
    float presence_ = 0.0f;

    // Feature-derived.
    float bass_ = 0.0f;
    float treble_ = 0.0f;
    float bright_ = 0.0f;
    float tilt_ = 0.0f;
    float harmonic_ = 0.5f;
    float build_ = 0.0f;
    float pan_ = 0.0f;
    float width_ = 0.0f;
    HitEnvelope kick_;
    HitEnvelope snare_;
    HitEnvelope hat_;
    HitEnvelope drop_;
    float kickEnv_ = 0.0f;
    float snareEnv_ = 0.0f;
    float hatEnv_ = 0.0f;
    float dropEnv_ = 0.0f;
    bool snareArmed_ = true;

    std::array<float, kTaps> taps_{};
    std::array<float, kTaps> frozenTaps_{};
    unsigned int seedState_ = 0x2545f491u;
};

}  // namespace geode::viz
