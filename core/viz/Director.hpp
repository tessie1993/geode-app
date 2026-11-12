#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/DirectorTuning.hpp"
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
// style's DNA, viz/ReactionDna), which the user can see and edit. It hears nothing of the sound but
// Form's states and events and the feeling: a look is decided, glided in and then held.
//
// It decides at musical moments only: the first confident key, a new section, a drop, a modulation that
// holds, and after a long stretch without one. Each decision is a "look". It follows the song at four
// spans (owner: "make it morph and progress, but nothing hardcoded"; every number is a setting,
// director::Tune, with the old numbers as defaults):
//
//   the bar     a look lands on a downbeat whenever a beat is heard (a drop cuts in at once, a quarter
//               second, where Form has moved the downbeat). How it lands is the feeling's: peace,
//               tenderness and transcendence glide in over up to two bars, power and joy over a beat
//               (changeBars). The bar is whatever the tracker hears; no tempo is preferred
//   the build   while Form says the song is in a build, the look leans toward the drop over four bars:
//               faster, closer, rougher. The drop's cut takes it away; a build that fades without one
//               lets go over a bar. It follows Form's state, never a level or a hit
//   the form    each section Form names keeps the look first chosen for it, the way that look was
//               heading and the style it was first shown on. A returning section morphs into that look
//               grown on: further the way it was heading with each return, seeded (chorus 1, 2 and 3
//               grow), never a copy and never a cut. A new section gets a new look. Form names a section
//               a bar after it reports the change, so the decision waits for that downbeat (the look
//               holds still meanwhile): then the look and the style change together, knowing whether the
//               section is new. A drop cuts at once, to the grown look of the section the last drop
//               opened when it opens one, and settles once Form names where it is
//   the song    across phrases and sections the look travels an arc: Complexity, Detail, Fractal and
//               Formula climb toward the song's peak (its drops, its later sections) and relax in its
//               breakdowns and outro, from Form's section state and the phrases heard. A new song starts
//               the climb again
//
// At a style moment it also says which style should come next (styleIntent): a returning section's
// style family stepped on by its returns, or the next style along the catalogue, never a random jump.
// Auto's pick (viz/DirectorStyle) reads it when the shell takes the moment, and the switch morphs through
// Auto's own transition.
//
// The seed is set per play (setGlobal), so a replay is new and an export that uses the play's seed is
// the same film.
//
// A pure function of (seed, tuning, sound frames, dt): no clock, no randomness of its own, state reset with
// the renderer. Process-wide configuration, like the macro bank, so the screen, an export, the wallpaper and
// the dream all follow the same switch.
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

// What a style moment asks of Auto's pick (viz/DirectorStyle).
struct StyleIntent {
    enum class Kind {
        None,      // no moment yet: Auto draws as it always has
        Progress,  // the next style along the catalogue from `from` (a new section, a long stretch)
        Leap,      // the first style of the next family along (a drop that opens a new section)
        Return,    // `remembered`'s family, `step` styles on from it (a returning section)
    };
    Kind kind = Kind::None;
    std::string from;        // the style on screen at the moment
    std::string remembered;  // Return: the style the section was first shown on
    int step = 0;            // Return: how many times the section has come back
    bool progress = true;    // the Styles progress setting; off, a return brings its own style back and else it stays
};
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
    // The renderer's way: also the tuning to direct by (null: the defaults) and the style on screen, which
    // a section remembers and a style moment steps on from.
    void configure(const DirectorConfig& config, std::shared_ptr<const director::Tuning> tuning, const std::string& style);
    // Hears the frame and steps its own feeling with it (tests, and a renderer with no sound engine).
    void step(const GeodeSoundFrame& f, float dt);
    // Hears the frame with the feeling the sound engine stepped on its own sample clock: the renderer's
    // way, so the feeling is the same at every frame rate and in an export.
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& heard, float dt);

    // Before the macros: the camera's softness added by the amount, the breath dial turned by a factor
    // (a dial at 0 stays off), and the song's arc on its params (director::applyArc). Never a colour
    // param, never Spin.
    SceneParams apply(const SceneParams& p) const;
    // The look's axes, with the build's lean and the song's arc, times the weight, each -1..1; all 0 while
    // off. Held between decisions, but for the lean and the arc, which move only with Form's states.
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
    // Where the song's arc stands, 0 (at rest) .. 1 (its peak), before the depth and the weight.
    float arc() const { return arc_; }
    // Whether the last section Form named was one heard before, whose look came back grown.
    bool returned() const { return returned_; }
    // Whether a section event waits for Form's name (the look holds still meanwhile).
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
    // Which style the last style moment asks for.
    const director::StyleIntent& styleIntent() const { return intent_; }
    void reset();

    // Exposed for tests: the look this feeling would get on commit number `index`.
    Look make(const sound::Emotion::State& s, uint64_t seed, int index, bool drop) const;
    // How many bars a look takes to land, from the kind of feeling: the calm end (peace, tenderness,
    // transcendence) at the tuning's calm glide, one bar between, the driven end (power, joy) at its
    // driven glide. Never the tempo's: the bar is whatever the tracker hears.
    static float changeBars(const sound::Emotion::State& s, const director::Tuning& tuning = director::defaultTuning());
    // A remembered look grown on for its `returns`-th return: further the way it was heading, plus a
    // seeded turn of its own, both growing with the returns, held to each dial's range. Pure.
    static Look grow(const Look& look, const Look& heading, int returns, float growth, uint64_t seed, int section);

    // Form's section slots (sound/Form kSections): a sectionId is one of these.
    static constexpr int kSectionSlots = 8;

private:
    // What the director keeps of a section Form named, for this play.
    struct Section {
        bool known = false;
        Look look;             // the look first chosen for it
        Look heading;          // that look less the one it replaced: the way it was heading
        std::string style;     // the style it was first shown on
        int returns = 0;       // how many times it has come back
    };

    const director::Tuning& tune() const { return tuning_ ? *tuning_ : director::defaultTuning(); }
    // The song's arc as a turn, -1 at rest .. 1 at the peak, times its depth.
    float arcTurn() const;
    // `chosen`: a look already decided (a grown one); else one is made for the feeling.
    void commit(Reason reason, float glideSeconds, const Look* chosen = nullptr);
    // The section a commit just opened keeps the look it landed on.
    void remember(int section);
    // Form named the section a section event opened: grow its look back, or give it a new one.
    void named(int section, float change, float morph);
    // The intent of the style moment just committed.
    void intend(director::StyleIntent::Kind kind, int section);

    sound::Emotion emotion_;
    sound::Emotion::State feeling_{};  // what this step heard: its own emotion_'s, or the engine's
    DirectorConfig config_;
    bool configured_ = false;
    std::shared_ptr<const director::Tuning> tuning_;
    std::string style_;
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

    // Section memory.
    std::array<Section, kSectionSlots> sections_{};
    // The section on screen, and whether this is its first visit (its style is still being learnt: the
    // switch a style moment asks for lands a few frames after it).
    int current_ = -1;
    bool firstVisit_ = false;
    // A section event waiting for Form's name: the id the frame carried at the event, the downbeats and
    // seconds since, and whether it was a drop (whose look is already in, `guessed` the section it cut to)
    // or a section (decided once named).
    bool naming_ = false;
    bool namingDrop_ = false;
    int namingFrom_ = -1;
    int namingBars_ = 0;
    float namingSeconds_ = 0.0f;
    int guessed_ = -1;
    Look beforeDrop_;
    int lastDropSection_ = -1;
    bool returned_ = false;
    director::StyleIntent intent_;

    // The build's lean, 0..1, toward 1 while Form's state is a build.
    float lean_ = 0.0f;
    // The song's arc, 0..1, and the phrases heard this song.
    float arc_ = 0.0f;
    int phrases_ = 0;
};

namespace director {
// The axes' maths, pure, so a test can turn one axis alone; Director feeds them its axes().
// p with each axis's fields moved toward an end of their slider range: at a full turn the style's
// default reaches the end, and a value the user moved keeps its whole offset (base plus modulation).
SceneParams applyAxes(const SceneParams& p, const Director::Axes& axes);
// Space widens or narrows every music move; Chaos scales the shake, the reframes and the hard cuts
// (-1 stops cuts). All 1 at rest.
CameraRig::Moves axisMoves(const Director::Axes& axes);
// The song's arc on its params, the one seam where it reaches one by name: Detail (marchDetail), Fractal and
// Formula, each by its tuning share of `turn` (-1 at rest .. 1 at the peak, the depth already in), moved
// toward an end of its range by that share of the room left (base plus modulation). A name the param table
// does not hold is skipped, so a param joins the arc by name the day it lands.
SceneParams applyArc(const SceneParams& p, float turn, const Tuning& tuning);
// The level the arc climbs toward: Form's section state (rest in an intro, breakdown or outro; up in a
// verse; higher in a build; the peak in a drop) times how far the song has climbed (half at the start,
// all of it after many phrases: `phrases` heard over the tuning's half-way count). Pure, 0..1.
float arcLevel(int sectionState, int phrases, const Tuning& tuning);
}  // namespace director

}  // namespace geode::viz
