#pragma once
#include <array>

#include "api/geode_api.h"
#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

// The reaction matrix: many audio features routed to many targets, each route
// with its own amount, curve, mode and smoothing. MotionField is one fixed
// table every style shares; this is the data-driven layer on top of it, and
// the Kotlin side seeds it per style (BuildStyleReactionsUseCase), so two
// styles hear the same bar of music differently.
//
// Pure: no GL, no allocation, no locks. tick() runs on the render thread once
// per frame with the frame's features; the same feature/dt sequence always
// gives the same output, which is what keeps an exported frame equal to the
// one on screen.

// Wire ordinals; mirrored by dev.geode.render.ReactSource. Append only.
enum class ReactSource {
    // Continuous, 0..1, each normalised against its own slow running peak.
    SubBass, Bass, LowMid, Mid, HighMid, Presence, Air,
    Loudness, Swell, Brightness, Noisiness, StereoWidth, StereoPan /* -1..1 */,
    BeatOsc, BarOsc, Tempo, Buildup, Flux, Kick, Snare, Hat,
    // Events: an instant rise and an exponential fall, so a one-hop flag
    // becomes something a picture can follow without snapping.
    Onset, Beat, Downbeat, Drop, Section, PitchChange, TimbreJump,
    // Appended after the events, so they break the grouping but no wire value.
    // MacroEnergy: the song's level against its own 20 s peak (the analyser's
    // macroEnergy), smoothed over seconds: where the track is in its arc.
    // Arrival: an event when the music comes back after two quiet seconds.
    MacroEnergy, Arrival,
};
constexpr int kReactSourceCount = static_cast<int>(ReactSource::Arrival) + 1;

// Follow: target += amount * curve(source).
// Step: each rising edge of the source adds amount to an accumulator (a hue
//   jump, a quarter turn, a new camera angle), glided by the route smoothing.
// Spin: the accumulator integrates amount * source per second (keeps turning
//   while the sound lasts).
// Accumulators are folded through a triangle wave into -1..1, so a target
// stepped forever stays bounded and never wraps with a pop.
enum class ReactMode { Follow, Step, Spin };
constexpr int kReactModeCount = static_cast<int>(ReactMode::Spin) + 1;

struct ReactRoute {
    bool enabled = false;
    ReactSource source = ReactSource::Bass;
    LfoTarget target = LfoTarget::None;
    ReactMode mode = ReactMode::Follow;
    float amount = 0.3f;      // -2..2
    ModCurve curve = ModCurve::Linear;
    float attackSeconds = 0.05f;
    float releaseSeconds = 0.3f;
};

// Every source value for one frame, computed once and shared by all routes.
class ReactSignals {
public:
    void reset();
    void step(const GeodeFeatureFrame& f, float dt);
    float value(ReactSource s) const { return values_[static_cast<size_t>(s)]; }
    // Onsets seen so far, wrapping at kOnsetCountWrap: a per-hit seed for shaders.
    float onsetCount() const { return onsetCount_; }
    // Section boundaries seen so far, same wrap: lets a shader hold a variation per section.
    float sectionCount() const { return sectionCount_; }
    static constexpr float kOnsetCountWrap = 4096.0f;

private:
    static constexpr float kPeakDecaySeconds = 6.0f;
    static constexpr float kPeakFloor = 0.03f;
    static constexpr float kSwellSeconds = 20.0f;
    static constexpr float kMacroSeconds = 1.5f;

    float normalised(int slot, float x, float dt);
    void trigger(ReactSource s, float strength);
    void decay(ReactSource s, float dt, float seconds);

    std::array<float, kReactSourceCount> values_{};
    std::array<float, kReactSourceCount> peaks_{};
    float swellAvg_ = 0.0f;
    float swellWarm_ = 0.0f;
    float macro_ = 0.0f;
    int lastKey_ = -1;
    float lastCentroid_ = 0.0f;
    float centroidSlow_ = 0.0f;
    float onsetArmed_ = 1.0f;
    float onsetCount_ = 0.0f;
    float sectionCount_ = 0.0f;
};

class ReactionEngine {
public:
    static constexpr int kRoutes = 12;
    static constexpr int kConfigFloats = 8; // enabled, source, target, mode, amount, curve, attack, release

    std::array<ReactRoute, kRoutes> routes{};

    void reset();
    void tick(const GeodeFeatureFrame& f, float dt);
    SceneParams apply(const SceneParams& p) const;

    const ReactSignals& signals() const { return signals_; }
    float output(int route) const { return out_[static_cast<size_t>(route)]; }

    // Luminance targets get a floor on their smoothing so no route can turn a
    // drum into a strobe; the magnitude is still clamped by safety::apply.
    static constexpr float kLuminanceMinAttack = 0.08f;
    static constexpr float kLuminanceMinRelease = 0.35f;

    static float fold(float accumulator);

private:
    ReactSignals signals_;
    std::array<float, kRoutes> acc_{};
    std::array<float, kRoutes> out_{};
    std::array<bool, kRoutes> high_{};
};

}  // namespace geode::viz
