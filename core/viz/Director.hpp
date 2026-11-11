#pragma once
#include <array>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/Macros.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

// The auto-director: the music's feeling (sound/Emotion) decides how a style behaves, the way a VJ
// would turn a few knobs between sections, through the machinery every style already shares:
//
//   behaviour ─► the macro knobs (Energy, Speed, Complexity, Space, Chaos; never Mood) laid over the
//              user's, the camera rig's music moves, its spring softness, the motion field's breath,
//              orbit and drift
//
// Two things it never does (owner's rule). It never touches colour: the palette, saturation,
// brightness, temperature and contrast are the style's and the user's. And it plays no reaction of its
// own: what moves on a kick, a note or a drop is the reaction matrix alone (the user's routes, else the
// style's DNA, viz/ReactionDna), which the user can see and edit. A look is decided, glided in and then
// held; nothing in it follows the frame.
//
// It decides at musical moments only: the first confident key, a new section, a drop, a modulation that
// holds, and after 32 bars without one. Each decision is a "look", glided in over a bar (a drop cuts in
// a quarter second). The seed is set per play (setGlobal), so a replay is new and an export that uses
// the play's seed is the same film.
//
// A pure function of (seed, sound frames, dt): no clock, no randomness of its own, state reset with the
// renderer. Process-wide configuration, like the macros, so the screen, an export, the wallpaper and the
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
    struct Look {
        MacroKnobs knobs{};
        float camSmooth = 0.5f;
        float motionBreath = 0.5f;
        float motionOrbit = 0.5f;
        float motionDrift = 0.5f;
        CameraRig::Moves moves{};
    };

    enum class Reason { First, Section, Drop, Modulation, Long };

    void configure(const DirectorConfig& config);
    // Hears the frame and steps its own feeling with it (tests, and a renderer with no sound engine).
    void step(const GeodeSoundFrame& f, float dt);
    // Hears the frame with the feeling the sound engine stepped on its own sample clock: the renderer's
    // way, so the feeling is the same at every frame rate and in an export.
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& heard, float dt);

    // Before the macros: the camera's softness added by the amount, the motion dials turned by a factor
    // (a dial at 0 stays off). Never a colour param.
    SceneParams apply(const SceneParams& p) const;
    // The params the rest of the chain starts from: apply(), then the macros with knobs(user). The look
    // turns the Motion dials through its knobs too (Energy, Speed, Space), but a dial the user's own
    // settings hold at 0 is the music's push switched off, and stays off.
    SceneParams direct(const SceneParams& p, const MacroKnobs& user) const;
    // The user's knobs plus the look's, each clamped to -1..1.
    MacroKnobs knobs(const MacroKnobs& user) const;
    // The camera rig's music moves, multiplied by the look's.
    CameraRig::Moves moves(const CameraRig::Moves& base) const;

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
};

}  // namespace geode::viz
