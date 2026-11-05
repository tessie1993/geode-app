#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "scene/camera/CameraParams.hpp"
#include "signal/params/ParamSchema.hpp"
#include "signal/params/ParamStore.hpp"
#include "sound/SoundFrame.h"

// The Scene director (#519, building #302 and #511): what changes in a Scene at a musical moment, chosen
// from the music's feeling and a per-play seed. It only decides; it outputs choices and touches nothing.
// The Scene (#513) resolves them later: a camera shot on the scene camera, a spawn on the family's
// spawners, an environment shift as a genome glide. That wiring is not here yet.
//
// When it decides: on a moment only, never on a timer.
//   - a section start (the frame's sectionChange trigger),
//   - a drop (the frame's drop trigger; it wins over a section start on the same frame),
//   - a build-up crest (buildUp rising through kCrest, re-armed once it falls under kRearm),
//   - a track boundary (onTrackBoundary, #460's command with the new play seed).
// Every other frame step() returns an empty span. A Scene can last the whole song.
//
// What it picks: up to kMaxChoices Choices per moment, at most one of each kind. Nothing is a real
// choice (it is emitted, as the moment's only choice), so a calm song can keep its Scene throughout.
//
// How it picks: each kind's weight is its param (directorSchema()) × the feeling's pull (energy, valence,
// tension, the section's state, the drop's strength, bent by sceneDirector.feelingDepth) × a personality
// rolled from the seed at reset (spread by sceneDirector.personality). The draw for moment n is keyed on
// (seed, n) alone, never on the frame count or the clock, so the same seed and the same stream give the
// same choices at 30, 60 or 120 fps, live or in an export, and another seed gives others. The last
// moment's pick (Nothing excepted) is damped so the film does not repeat itself; no kind is fixed.
//
// Respecting the user: a moment the hand mask owns (viz::hand: Moment::NewSection, Drop, BuildUp) gets no
// automatic choice (an empty span), but still uses up its moment number, so every later moment keeps its
// own draw. Comfort: Calm never picks Flip or TurnRound. Disabled (the auto-director off) picks nothing.
//
// Pure: no GL, no clock of its own, no allocation after construction. One thread (the renderer's).

namespace geode::scene {

enum class MomentKind : std::uint8_t { Section, Drop, BuildCrest, TrackStart };

enum class ChoiceKind : std::uint8_t { Nothing, CameraShot, Spawn, EnvShift };
constexpr int kChoiceKinds = 4;

enum class ShotKind : std::uint8_t { Orbit, LockOn, Follow, Flip, TurnRound, Push, Hold };
constexpr int kShotKinds = 7;

struct CameraShot {
    ShotKind kind = ShotKind::Hold;
    int targetHint = -1;    // an object index for Orbit, LockOn and Follow; -1: the world (no subject)
    float duration = 0.0f;  // seconds, in whole bars of the frame's tempo; 0 for Hold: until the next moment
};

struct Spawn {
    int spawner = 0;  // index into the family's spawners (Context::spawners)
    int count = 1;
    bool burst = false;  // short-lived, all at once
};

struct EnvShift {
    float glideSeconds = 1.0f;
    float mutation = 0.3f;  // 0..1: how far the genome moves
};

// A tagged struct: `kind` says which of the three payloads is meant; the others stay at their defaults.
struct Choice {
    ChoiceKind kind = ChoiceKind::Nothing;
    MomentKind moment = MomentKind::Section;
    float strength = 0.0f;  // the moment's: the trigger's value (drop and section 0.4..1), kCrest, 1 for a track
    CameraShot shot{};
    Spawn spawn{};
    EnvShift env{};
};

// The feeling at the moment: sound::Emotion's axes, as the auto-director (viz::Director::emotion()) hears
// them, so the two directors read one feeling.
struct Feeling {
    float valence = 0.0f;  // -1..1
    float energy = 0.5f;   // 0..1
    float tension = 0.3f;  // 0..1
};

struct Context {
    Feeling feeling{};
    camera::Comfort comfort = camera::Comfort::Normal;  // the camera's effective level (reduced motion: Calm)
    std::uint32_t handMask = 0;  // viz::hand's mask (hand::global()): bit i is viz::Moment i set by hand
    bool enabled = true;         // the auto-director's switch
    int spawners = 0;            // the family's spawners; 0: Spawn is never picked
    int objects = 0;             // objects a shot can take as its subject; 0: no LockOn or Follow
};

enum DirectorParam : signal::ParamId {
    kNothingWeight = 0,  // base weight of each kind, 0..4
    kShotWeight,
    kSpawnWeight,
    kEnvWeight,
    kFeelingDepth,  // 0: the feeling bends nothing .. 1: as designed .. 2: twice as hard
    kPersonality,   // 0: every play weighs alike .. 1: the seed's personality in full
    kMoreChance,    // 0..1: the chance of a second (and third) kind at one moment, before the intensity
    kRepeatDamp,    // 0..1: the multiplier on the last moment's pick (1: no damping)
    kMaxChoices,    // Int 1..kMaxChoices
    kDirectorParamCount,
};

// One row per DirectorParam, ids "sceneDirector.*" (the auto-director's own are not params yet).
signal::ParamSchema directorSchema();

class SceneDirector {
public:
    static constexpr int kMaxChoices = 3;
    static constexpr float kCrest = 0.75f;
    static constexpr float kRearm = 0.3f;

    SceneDirector();

    // A new play: the seed, the personality, the moment count and the edge detectors.
    void reset(std::uint64_t seed);
    // A track boundary (#460): reset to the new seed, then decide at the next step(), as a TrackStart.
    void onTrackBoundary(std::uint64_t newSeed);

    // The choices for this frame: empty on every frame without a moment (and on a hand-owned one). The
    // span is valid until the next step() or reset().
    std::span<const Choice> step(const GeodeSoundFrame& f, float dt, const Context& ctx);

    signal::ParamStore& params() { return store_; }
    const signal::ParamStore& params() const { return store_; }

    std::uint64_t seed() const { return seed_; }
    // Moments heard since reset, hand-owned ones included.
    int moments() const { return moments_; }

private:
    struct Moment {
        MomentKind kind;
        float strength;
    };
    int decide(const Moment& m, int index, const GeodeSoundFrame& f, const Context& ctx);

    signal::ParamStore store_;
    std::uint64_t seed_ = 0;
    std::array<float, kChoiceKinds> kindPersona_{};
    std::array<float, kShotKinds> shotPersona_{};
    int moments_ = 0;
    bool trackPending_ = false;
    bool crestArmed_ = true;
    bool lastSection_ = false;
    bool lastDrop_ = false;
    int lastLabel_ = -1;   // 0 Nothing, 1..7 the shots, 8 Spawn, 9 EnvShift
    double clock_ = 0.0;   // seconds since reset, for the spacing between moments only
    double lastMomentAt_ = -1e9;
    std::array<Choice, kMaxChoices> out_{};
    int count_ = 0;
};

}  // namespace geode::scene
