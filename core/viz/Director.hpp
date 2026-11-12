#pragma once
#include <array>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

// The auto-director: the music's feeling (sound/Emotion) decides how a style behaves, the way a VJ
// would turn a few knobs between sections, through the machinery every style already shares:
//
//   behaviour ─► five axes of its own (Energy, Speed, Complexity, Space, Chaos), each an offset on a
//              set of scene params and a scale on the camera rig's music moves; the camera's spring
//              softness and the motion field's breath. The user's macro bank (viz/MacroBank) is never
//              touched: it adds on top of whatever the director left. Spin (motionDrift) is never the
//              director's either (#1181): whether a turning style spins is the user's choice
//
// Two things it never does (owner's rule). It never touches colour: the palette, saturation,
// brightness, temperature and contrast are the style's and the user's. And it plays no reaction of its
// own: what moves on a kick, a note or a drop is the reaction matrix alone (the user's routes, else the
// style's DNA, viz/ReactionDna), which the user can see and edit. A look is decided, glided in and then
// held; nothing in it follows the frame.
//
// It decides at musical moments only: the first confident key, a new section, a drop, a modulation that
// holds, and after 32 bars without one. Each decision is a "look". It follows the song at three spans:
//
//   the bar     a look lands on a downbeat whenever a beat is heard (a drop cuts in at once, a quarter
//               second, where Form has moved the downbeat). How it lands is the feeling's: peace,
//               tenderness and transcendence glide in over up to two bars, power and joy cut in over a
//               beat (changeBars). The bar is whatever the tracker hears; no tempo is preferred
//   the build   while Form says the song is in a build, the look leans toward the drop over four bars:
//               faster, closer, rougher (kBuildLean). The drop's cut takes it away; a build that fades
//               without one lets go over a bar. It follows Form's state, never a level or a hit
//   the form    each section Form names keeps the look first chosen for it, so a returning chorus brings
//               back the chorus's look and the song's structure is visible. A new section gets a new
//               look. Form names a section a bar after it reports the change, so a section's look waits
//               for that downbeat (held still, never a look that is then undone); a drop cuts at once to
//               the look of the section the last drop opened, and settles once Form names where it is
//
// The seed is set per play (setGlobal), so a replay is new and an export that uses the play's seed is
// the same film.
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
// `on` is the user's switch (Visuals › Settings › Auto Director) as set, before any entitlement; `amount`
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
    // The behaviour axes, each -1..1, 0 = as styled. No axis is a mood: colour is never the director's.
    enum class Axis { Energy, Speed, Complexity, Space, Chaos };
    static constexpr int kAxes = 5;
    using Axes = std::array<float, kAxes>;

    struct Look {
        // The feeling's energy the look was made for (a drop's at least 0.85): how hard the reach shuffles.
        float energy = 0.5f;
        Axes axes{};
        float camSmooth = 0.5f;
        float motionBreath = 0.5f;
        CameraRig::Moves moves{};
    };

    enum class Reason { First, Section, Drop, Modulation, Long };

    void configure(const DirectorConfig& config);
    // Hears the frame and steps its own feeling with it (tests, and a renderer with no sound engine).
    void step(const GeodeSoundFrame& f, float dt);
    // Hears the frame with the feeling the sound engine stepped on its own sample clock: the renderer's
    // way, so the feeling is the same at every frame rate and in an export.
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& heard, float dt);

    // Before the macros: the camera's softness added by the amount, the breath dial turned by a factor
    // (a dial at 0 stays off). Never a colour param, never Spin.
    SceneParams apply(const SceneParams& p) const;
    // The look's axes, with the build's lean, times the weight, each -1..1; all 0 while off. Held
    // between decisions, but for the lean, which moves only when Form's section state does.
    Axes axes() const;
    // Before the user's macro bank: each axis moves its fields toward an end of their range, added to
    // the value it is given (base plus modulation). A Motion dial given at 0 stays 0.
    SceneParams applyAxes(const SceneParams& p) const;
    // The camera rig's music moves: Space and Chaos, times the look's own.
    CameraRig::Moves moves() const;

    // How much it is driving now: the switch, eased, times whether a look has been chosen yet.
    float weight() const { return amount_ * presence_; }
    const sound::Emotion::State& emotion() const { return feeling_; }
    const Look& look() const { return now_; }
    const Look& target() const { return to_; }
    int commits() const { return commits_; }
    Reason lastReason() const { return reason_; }
    // How far the build's lean is in, 0..1 (before its ease and the weight).
    float lean() const { return lean_; }
    // Whether the last section Form named was one heard before, whose remembered look came back.
    bool returned() const { return returned_; }
    // Whether a section event waits for Form's name (a section's look holds still meanwhile).
    bool naming() const { return naming_; }
    // The glide the look on screen lands with, in seconds.
    float glideSeconds() const { return glideSeconds_; }
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
    // How many bars a look takes to land, from the kind of feeling: 2 for the calmest (peace, tenderness,
    // transcendence), 1 between, a quarter (a beat) for the most driven (power, joy). Never the tempo's:
    // the bar is whatever the tracker hears.
    static float changeBars(const sound::Emotion::State& s);

    // Form's section slots (sound/Form kSections): a sectionId is one of these.
    static constexpr int kSectionSlots = 8;

private:
    // `chosen`: a look already decided (a remembered one); else one is made for the feeling.
    void commit(Reason reason, float glideSeconds, const Look* chosen = nullptr);
    // Form named the section a section event opened: bring back its look, or remember the new one.
    void named(int section, float glideSeconds);

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

    // Section memory: the look first chosen for each section Form names, for this play.
    std::array<Look, kSectionSlots> remembered_{};
    std::array<bool, kSectionSlots> known_{};
    // A section event waiting for Form's name: the id the frame carried at the event, the downbeats and
    // seconds since, and whether the event was a drop (whose look is already in) or a section (whose look
    // holds still until named).
    bool naming_ = false;
    bool namingDrop_ = false;
    int namingFrom_ = -1;
    int namingBars_ = 0;
    float namingSeconds_ = 0.0f;
    int lastDropSection_ = -1;
    bool returned_ = false;

    // The build's lean, 0..1, toward 1 while Form's state is a build.
    float lean_ = 0.0f;
};

namespace director {
// The axes' maths, pure, so a test can turn one axis alone; Director feeds them its axes().
// p with each axis's fields moved toward an end of their slider range: at a full turn the style's
// default reaches the end, and a value the user moved keeps its whole offset (base plus modulation).
SceneParams applyAxes(const SceneParams& p, const Director::Axes& axes);
// Space widens or narrows every music move; Chaos scales the shake, the reframes and the hard cuts
// (-1 stops cuts). All 1 at rest.
CameraRig::Moves axisMoves(const Director::Axes& axes);
}  // namespace director

}  // namespace geode::viz
