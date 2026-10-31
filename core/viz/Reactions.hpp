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
// per frame with the frame's sound (core/sound); the same frame/dt sequence always
// gives the same output, which is what keeps an exported frame equal to the
// one on screen.

// Wire ordinals; mirrored by dev.geode.render.ReactSource. Append only.
//
// One source per GeodeSoundFrame value (core/sound/SoundFrame.h), grouped as the Customize picker shows
// them. This is the second numbering: the old analyser's 34 sources were replaced wholesale, and saved
// routes from before it are dropped (ReactionRouteCodec's "reactions/2"), so each style restarts on its DNA.
// What a route reads for each source is a 0..1 (StereoPan and Interval -1..1) view of the field; see
// matrix::sources() in ParamMatrix.cpp for how each kind is shaped.
enum class ReactSource {
    // Levels: relative to the song (~0.5 typical, 1 a strong hit), then the shaped 0..1 layer.
    Bass, Mid, Treble, Vol, BassAtt, MidAtt, TrebleAtt, VolAtt, Sub, SubAtt, MidHigh, MidHighAtt,
    Low, Middle, High, Level, BassPresence, MidPresence, TreblePresence, Intensity, Presence,
    // Clocks: how fast each clock runs right now (Spin integrates it back into the clock).
    BassTime, MidTime, TrebleTime, CurvedTime, HitTime, NoteTime,
    // Texture.
    Loudness, Centroid, Rolloff, Flatness, DensitySlow, DensityFast, Flux, EnergyTrend, PeakFreq, PeakMag,
    StereoWidth, StereoPan /* -1..1 */,
    // Drums: envelopes, toggles and the per-hit random, then the triggers (every hit, every Nth).
    KickEnv, SnareEnv, HatEnv, OnsetEnv, KickToggle, SnareToggle, KickRandom,
    Kick, Snare, Hat, Kick2, Kick4, Kick8, Kick16, Snare2, Snare4,
    // Events.
    HardCut, Onset, Silence, Returned, Surge,
    // Melody.
    NoteChange, Legato, Reattack, NoteRandom, PitchClass, PitchHeight, PitchConfidence, Interval /* -1..1 */,
    Glide, Vibrato, Evenness, RunDensity,
    // EQ: the 16 Band EQ centres (25 Hz … 22 kHz), each relative like the levels.
    Eq25, Eq40, Eq60, Eq90, Eq150, Eq240, Eq370, Eq590, Eq930, Eq1k5, Eq2k3, Eq3k6, Eq5k6, Eq8k9, Eq14k, Eq22k,
};
constexpr int kReactSourceCount = static_cast<int>(ReactSource::Eq22k) + 1;

// Names, labels, groups and how each source is shaped: viz/ParamMatrix (the one list of every sound
// value and every visual parameter).

// Follow: target += amount * curve(source).
// Step: each rising edge of the source adds amount to an accumulator (a hue
//   jump, a quarter turn, a new camera angle), glided by the route smoothing.
// Spin: the accumulator integrates amount * source per second (keeps turning
//   while the sound lasts).
// Jump: each rising edge holds a new seeded random value in -amount..amount
//   (sample and hold), glided by the route smoothing: a new hue, angle or
//   warp per hit instead of the same push every time.
// Toggle: each rising edge flips the target between 0 and amount (a palette
//   swap, a mirror on or off), glided by the route smoothing. Step cannot do
//   this: an amount of 2 folds back to 0 on every edge.
// Step and Spin accumulators are folded through a triangle wave into -1..1, so
// a target stepped forever stays bounded and never wraps with a pop.
// Drive: the music is what moves the target. A motion that would otherwise run all the time (rotation,
//   scene speed, sway, drift, camera fly, colour cycle, dive) runs at its slider's rate times how busy
//   the source is (typical music = the slider, silence = still); see matrix::drive. Without a Drive
//   route a motion target still waits for the music, on its default source in ParamMatrix.
enum class ReactMode { Follow, Step, Spin, Jump, Toggle, Drive };
constexpr int kReactModeCount = static_cast<int>(ReactMode::Drive) + 1;

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
    void step(const GeodeSoundFrame& f, float dt);
    float value(ReactSource s) const { return values_[static_cast<size_t>(s)]; }
    // Onsets seen so far, wrapping at kCountWrap: a per-hit seed for shaders.
    float onsetCount() const { return onsetCount_; }
    // Hard cuts seen so far, same wrap: lets a shader hold a variation per big moment.
    float cutCount() const { return cutCount_; }
    static constexpr float kCountWrap = 4096.0f;

private:
    std::array<float, kReactSourceCount> values_{};
    // The clocks' values last frame, for their rates.
    std::array<float, kReactSourceCount> lastClock_{};
    bool clocksPrimed_ = false;
    float onsetCount_ = 0.0f;
    float cutCount_ = 0.0f;
};

class ReactionEngine {
public:
    static constexpr int kRoutes = 32;
    static constexpr int kConfigFloats = 8; // enabled, source, target, mode, amount, curve, attack, release

    std::array<ReactRoute, kRoutes> routes{};

    void reset();
    void tick(const GeodeSoundFrame& f, float dt);
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
    std::array<unsigned int, kRoutes> jumpSeed_{};

    float nextJump(int route);
};

}  // namespace geode::viz
