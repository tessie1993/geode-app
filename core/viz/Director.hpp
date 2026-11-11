#pragma once
#include <array>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/Params.hpp"
#include "viz/Reactions.hpp"

namespace geode::viz {

// The auto-director: the music's feeling (sound/Emotion) decides how every style looks and behaves, the
// way a VJ would, through the machinery every style already shares, so no style needs a line of its own:
//
//   palette ─► paletteBase/Range overrides, the second palette and its mix, saturation, brightness,
//              temperature, contrast (every shader style's pal(), the simulations' hue, MilkDrop's tint)
//   behaviour ─► five axes of its own (Energy, Speed, Complexity, Space, Chaos), each an offset on a
//              set of scene params, a scale on the camera rig's music moves and a few routes; the
//              camera's spring softness, the motion field's breath, orbit and drift. The user's macro
//              bank (viz/MacroBank) is never touched: it adds on top of whatever the director left
//   what reacts to what ─► a reaction engine of its own, re-dealt from the feeling's archetypes, with its
//              hands off every moment the user set by hand (Customize › Music, the hand mask of
//              viz/Reactions.hpp, handed to step()): the hand wins per moment
//
// It decides at musical moments only: the first confident key,
// a new section, a drop, a modulation that holds, and after 32 bars without one. Each decision is a
// "look", glided in over a bar (a drop cuts in a quarter second). Between decisions the look breathes with
// the live energy and tension, a build contracts it, and the melody's scale degree adds short accents.
//
// Same feeling, never the same picture: emotion picks a region (hue anchors from the colour-emotion
// studies, lightness and saturation ranges), the key turns it around the circle of fifths, the harmony
// picks its shape (analogous, split, near-monochrome), and a seeded draw that rejects anything close to
// the last six looks picks the point. The seed is set per play (setGlobal), so a replay is new and an
// export that uses the play's seed is the same film.
//
// A pure function of (seed, sound frames, dt): no clock, no randomness of its own, state reset with the
// renderer. Process-wide configuration, like the macro bank, so the screen, an export, the wallpaper and the
// dream all follow the same switch.
struct DirectorConfig {
    float amount = 0.0f;  // 0 off .. 1 full
    uint64_t seed = 0;
};

namespace director {
// The free taste (pro/FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS, which times the live taste in the shell): a free
// user's director drives the first minute of each song, and an export no longer than that throughout.
constexpr long long kFreeTasteMs = 60'000;
// `on` is the user's switch (Settings › Behaviour › Auto-director) as set, before any entitlement; `amount`
// is what live surfaces drive with now (the switch cut to the free taste by the shell's live rule).
void setGlobal(bool on, float amount, uint64_t seed);
// What live surfaces drive with: the live amount and the play's seed.
DirectorConfig global();
// The user's switch, as set.
bool switchOn();
// An export of `durationMs` (#350): directed throughout, or not at all, never by the live amount (the live
// taste may have run out while the range to render is short). Off when the switch is off; full when
// `entitled` (Pro or the trial); otherwise full only when the whole export fits the free taste. The play's
// `seed`, so it makes the same film. A pure function, so it is host-tested (resolver_test).
DirectorConfig forExport(bool switchOn, uint64_t seed, bool entitled, long long durationMs);
}  // namespace director

class Director {
public:
    static constexpr int kRoutes = 5;
    using Routes = std::array<ReactRoute, ReactionEngine::kRoutes>;
    // The behaviour axes, each -1..1, 0 = as styled. They were the old macro knobs' rows the director
    // turned (Mood never: the palette is the director's mood); now they are the director's alone.
    enum class Axis { Energy, Speed, Complexity, Space, Chaos };
    static constexpr int kAxes = 5;
    using Axes = std::array<float, kAxes>;

    struct Look {
        // Colour: hues 0..1 on the colour wheel (red 0, yellow 1/6, green 1/3, blue 2/3).
        float hue = 0.0f;
        float range = 0.3f;
        float hue2 = 0.5f;
        float range2 = 0.3f;
        float mix = 0.0f;
        float saturation = 1.0f;
        float brightness = 1.0f;
        float temperature = 0.0f;
        float contrast = 1.0f;
        // Behaviour.
        Axes axes{};
        float camSmooth = 0.5f;
        float motionBreath = 0.5f;
        float motionOrbit = 0.5f;
        float motionDrift = 0.5f;
        CameraRig::Moves moves{};
        Routes routes{};
        // The feeling it was made for, so the live breathing reads against it.
        float valence = 0.0f;
        float energy = 0.5f;
        float tension = 0.3f;
        int primary = 0;  // the GEMS factor that led
    };

    enum class Reason { First, Section, Drop, Modulation, Long };

    void configure(const DirectorConfig& config);
    // `handMask` is Customize › Music's hand mask (viz::hand): the routes keep off every moment it sets.
    // An input, not hand::global, so a resolver's engine settings decide it (signal/Resolver, #344); 0 is
    // nothing the hand's, the global's own default.
    // Hears the frame and steps its own feeling with it (tests, and a renderer with no sound engine).
    void step(const GeodeSoundFrame& f, float dt, std::uint32_t handMask = 0);
    // Hears the frame with the feeling the sound engine stepped on its own sample clock: the renderer's
    // way, so the feeling is the same at every frame rate and in an export.
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& heard, float dt, std::uint32_t handMask = 0);

    // First in the chain: the palette and the motion character, lerped by the amount. `palWheel` says how
    // the scene reads paletteBase: true for the shader styles (lib_palette's pal() runs the colour wheel
    // backwards), false for MilkDrop's tint and the simulations, which read it as a plain hue.
    SceneParams apply(const SceneParams& p, bool palWheel = true) const;
    // The look's axes breathing with the live feeling, times the weight, each -1..1; all 0 while off.
    Axes axes() const;
    // After the note layer, before the user's macro bank: each axis moves its fields toward an end of
    // their range, added to the value it is given (base plus modulation).
    SceneParams applyAxes(const SceneParams& p) const;
    // The camera rig's music moves: Space and Chaos, times the look's own.
    CameraRig::Moves moves() const;
    // After the user's routes: the axes' routes, then the director's own.
    SceneParams applyRoutes(const SceneParams& p) const;

    // How much it is driving now: the switch, eased, times whether a look has been chosen yet.
    float weight() const { return amount_ * presence_; }
    const sound::Emotion::State& emotion() const { return feeling_; }
    const Look& look() const { return now_; }
    const Look& target() const { return to_; }
    int commits() const { return commits_; }
    Reason lastReason() const { return reason_; }
    // Whether the last commit should change the style: a new section, a drop, or a long stretch with
    // neither (MilkDrop's timer: music with no clear sections still gets a new style now and then), and
    // only while the director drives (Auto on). It steps with Auto off too, for its feeling (Note & key
    // reactions read it), and those commits must not step the user's chosen style.
    bool styleMoment() const {
        return config_.amount > 0.0f && (reason_ == Reason::Section || reason_ == Reason::Drop || reason_ == Reason::Long);
    }
    void reset();

    // Exposed for tests: the look this feeling would get on commit number `index`.
    Look make(const sound::Emotion::State& s, uint64_t seed, int index, bool drop) const;

private:
    void commit(Reason reason, float glideSeconds);
    bool farFromRecent(const Look& candidate) const;

    sound::Emotion emotion_;
    sound::Emotion::State feeling_{};  // what this step heard: its own emotion_'s, or the engine's
    DirectorConfig config_;
    bool configured_ = false;
    float amount_ = 0.0f;
    float presence_ = 0.0f;

    Look from_;
    Look to_;
    Look now_;
    float glide_ = 1.0f;
    float glideSeconds_ = 1.0f;

    ReactionEngine outgoing_;
    ReactionEngine incoming_;
    // The axes' routes, in an engine of their own; ticked at the end of each step.
    ReactionEngine axisReactions_;
    Routes outgoingBase_{};
    Routes incomingBase_{};

    int commits_ = 0;
    Reason reason_ = Reason::First;
    float heard_ = 0.0f;
    float sinceCommit_ = 0.0f;
    int barsSinceCommit_ = 0;
    // The key (tonic + 12 for minor) the last decision heard, and the one the frame hears now. A
    // modulation is judged on the key, not the scale: the scale detector may move between close relatives
    // (Hungarian minor, hirajoshi, iwato share most notes) inside one key, which colours the feeling but is
    // not a new key.
    int committedKey_ = -1;
    int key_ = -1;
    float modulationSeconds_ = 0.0f;

    static constexpr int kRecent = 6;
    std::array<Look, kRecent> recent_{};
    int recentCount_ = 0;

    // Live, between decisions.
    float buildUp_ = 0.0f;
    float dropBoost_ = 0.0f;
    float accent_ = 0.0f;
    float accentSign_ = 1.0f;
    float accentTension_ = 0.0f;
};

namespace director {
// The axes' maths, pure, so a test can turn one axis alone; Director feeds them its axes().
// p with each axis's fields moved toward an end of their slider range: at a full turn the style's
// default reaches the end, and a value the user moved keeps its whole offset (base plus modulation).
SceneParams applyAxes(const SceneParams& p, const Director::Axes& axes);
// Space widens or narrows every music move; Chaos scales the shake, the reframes and the hard cuts
// (-1 stops cuts). All 1 at rest.
CameraRig::Moves axisMoves(const Director::Axes& axes);
// Each axis's routes at a full turn up, scaled by the axis; off at or below 0.
Director::Routes axisRoutes(const Director::Axes& axes);
}  // namespace director

}  // namespace geode::viz
