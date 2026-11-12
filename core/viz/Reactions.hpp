#pragma once
#include <array>
#include <bitset>
#include <cstdint>

#include "api/geode_api.h"
#include "viz/Circular.hpp"
#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

// The reaction matrix: many audio features routed to many targets, each route
// with its own amount, curve, mode and smoothing. MotionField is one fixed
// table every style shares; this is the data-driven layer on top of it, and
// viz/ReactionDna seeds it per style, so two
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
// routes from before it are dropped (ReactionText's "reactions/2"), so each style restarts on its DNA.
// What a route reads for each source is a 0..1 (StereoPan and Interval -1..1) view of the field; see
// kSourceTable in Reactions.cpp for how each kind is shaped.
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
    // The musical layer (core/sound: Rhythm, Harmony, Form). Rhythm: phases ramp 0..1 per beat, bar and
    // 8-bar phrase; tempo reads bpm over the fastest the detector measures (0 while none is heard); the onset kinds are relative.
    BeatPhase, BarPhase, PhrasePhase, BeatConf, Bpm, Beat, Downbeat, Phrase, FluxLow, FluxMid, FluxHigh, Hfc,
    // Harmony: key, chord, tension, and the scale's mood from the musical intelligence database.
    KeyHue, KeyRoot, KeyMajor, KeyClarity, ChordHue, Tension, HarmonicChange, ScaleRoot, ScaleBrightness, ScaleTension, ScaleFit,
    // Form: the song part reads state / 5 (GEODE_SECTION_*), the section number id / 7.
    Novelty, SectionChange, SectionState, SectionId, BuildUp, Drop,
    // The player (core/sound/PlayerSignals): its fades, which the tap before the volume never hears.
    FadeGain,
    // Transients (core/sound/Transient): the sharp start of any sound, its envelope, and how steep the last one was.
    Transient, TransientEnv, TransientSharpness,
};
constexpr int kReactSourceCount = static_cast<int>(ReactSource::TransientSharpness) + 1;

// The source catalogue: what the Customize picker, the Events recipes and the saved formats call each
// source. C++ owns it (the user's rule: the reaction feature lives in C++); the shell reads it in
// viz/ModCatalog's table and does not keep its own list. `name` is the stable saved name,
// `group` the picker section (LEVELS, DRUMS, EVENTS, MELODY, CLOCKS, TEXTURE, EQ, RHYTHM, HARMONY, FORM), `event` a trigger
// held as an envelope rather than a level, `circle` a value on a circle (a hue, a pitch class, a phase):
// the route smoothing follows it round the seam (viz/Circular.hpp) instead of sweeping back across the
// whole circle, so the rule is data, not a list in the smoother.
struct ReactSourceInfo {
    const char* name;
    const char* label;
    const char* group;
    bool event;
    Circle circle = Circle::None;
};
const ReactSourceInfo& reactSourceInfo(ReactSource s);

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
enum class ReactMode { Follow, Step, Spin, Jump, Toggle };
constexpr int kReactModeCount = static_cast<int>(ReactMode::Toggle) + 1;

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

// ── Customize › Music's moments and the hand mask ───────────────────────────────────────────────────
//
// The owner's rule: "Auto drives every untouched moment; a hand-set moment shows a 'mine' mark and the
// director leaves it alone; 'Reset to Auto' is in its ⋮." All of it is native (ADR 0003): the moments and
// the mask here, where the director reads them; the merge with the live DNA and the page's edits in
// viz/ReactionDna. Kotlin only labels the rows and passes the mask along.
//
// WHY a moment is a set of sources, not one: the DNA and the director deal routes from every drum signal
// (the kick's envelope, its hit, every 4th kick, its toggle), and a listener hears all of them as "the
// kick". The sets never overlap, so a route belongs to at most one moment. Sources no moment claims (the
// mids, the stereo pan, an EQ band) are the "unclaimed" part, which only the whole-list route editor
// (Modulate › Reactions) takes over.

// Wire ordinals, in the page's order; the shell labels them in that order. Append only.
enum class Moment { Kick, Snare, Hat, Bass, Drop, BuildUp, ChordChange, NewSection, Note, Loudness };
constexpr int kMomentCount = static_cast<int>(Moment::Loudness) + 1;

using ReactSourceMask = std::bitset<kReactSourceCount>;

// The hand mask: bit i is Moment i set by hand; kUnclaimed is every source no moment claims. kAll is the
// whole list (the route editor's edit, and a list saved before the mask existed).
namespace hand {
constexpr std::uint32_t kUnclaimed = 1u << 30;
constexpr std::uint32_t kAll = ((1u << kMomentCount) - 1u) | kUnclaimed;
constexpr std::uint32_t bit(Moment m) { return 1u << static_cast<int>(m); }
// Every source the mask hands to the user.
ReactSourceMask sources(std::uint32_t mask);
// The moment `s` belongs to, or -1 when no moment claims it.
int momentOf(ReactSource s);
// Process-wide like the macro bank and the director, so every renderer's director (screen, export, wallpaper,
// dream) keeps off the same moments. 0 (nothing the hand's) until the shell pushes the saved mask.
// A mask counts only while the user has a list of their own (`hasUser`): with none the DNA plays every
// moment, so a mask left over from a stale or unreadable saved list (#355) must not switch off the
// director's routes. global() is then 0, whatever mask came with it.
void setGlobal(std::uint32_t mask, bool hasUser);
std::uint32_t global();
// The source a moment is heard on as a trigger: the first event in its set (the kick's hit, a drop, a new section),
// else its first source (bass, build-up, loudness have no event).
ReactSource trigger(Moment m);
}  // namespace hand

// Customize › Music's moments as triggers, for a wave or an envelope the user set "On a moment" (viz/Lfo, viz/Adsr).
// A moment comes when its trigger (hand::trigger) rises through 0.5, and comes again only after it fell below 0.3: the
// reaction matrix's Step rule, so a held level is one moment, not one a frame. Silence brings none.
class MomentWatch {
public:
    static constexpr float kRise = 0.5f;
    static constexpr float kRearm = 0.3f;
    // The bit per Moment that came this frame.
    std::uint32_t step(const ReactSignals& signals);
    std::uint32_t step(const std::array<float, kMomentCount>& triggers);
    void reset() { high_.fill(false); }

private:
    std::array<bool, kMomentCount> high_{};
};

class ReactionEngine {
public:
    static constexpr int kRoutes = 12;
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
    // The music's absolute reach (core/sound/DriveGain) of the frame last ticked: apply() scales what the audio
    // adds by it, never the params' base values.
    float gain_ = 1.0f;
    std::array<bool, kRoutes> high_{};
    std::array<unsigned int, kRoutes> jumpSeed_{};
    // A Follow route on a circular source smooths the source on its circle, before the curve and the
    // amount; `circ_` is that smoothed value, `circSource_` the source it follows plus one (0: not primed, so a fresh engine needs no reset).
    std::array<float, kRoutes> circ_{};
    std::array<int, kRoutes> circSource_{};
    // Where the output was when the route started following its circle, and how far (0..1) it has
    // glided from there, so a route the director or the DNA swaps in eases in like any other.
    std::array<float, kRoutes> circFrom_{};
    std::array<float, kRoutes> circBlend_{};

    float nextJump(int route);
};

}  // namespace geode::viz
