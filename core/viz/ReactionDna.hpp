#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "viz/Reactions.hpp"

namespace geode::viz {

// Style DNA: the reaction routes a style plays until the user edits them,
// built from the style id and a seed.
//
// Each style draws a personality: what its picture does on the pulse. A Chroma
// style changes hue, a Flash style blooms, a Swing style looks somewhere new, a
// Warp style bends. It draws a second one from another group for the snare
// side of the groove, then layers for the big moments, the sustained levels,
// the top end, colour over the song, rises and the melody, then the
// transients, which every style hears, and last the song's long time (every
// 4 bars or every phrase, counted on the music's own bars) and its tune (the
// melody's line, height, notes or presence), its harmony or its form (chords
// and their motion, tension, key, scale; sections, their return and progress,
// the song part, builds, drops, novelty) and its feel and timbre (mood, the
// key's character, the kind of feeling, brightness, noisiness, how the tune is
// played), which every style hears too. No continuous layer follows the absolute loudness: volume is not
// what the picture hears. No layer has a
// fixed source: each draws its own from a pool of GeodeSoundFrame sources
// (which kick, which level, what drives hue), so styles that share a
// personality still listen to different parts of the music.
//
// Pure and deterministic: the same id, seed and traits always give the same
// routes, on every platform, so the live view, the wallpaper, the dream and an
// export all react alike.

enum class Personality {
    Pulse,   // the camera pushes in on the beat (the old behaviour, now one of many)
    Chroma,  // the hue lands somewhere new on each beat
    Flash,   // brightness and bloom swell on the beat (soft: luminance floors and safety apply)
    Twist,   // a radial twist, a new direction per beat
    Warp,    // the domain bends on the beat
    Ripple,  // rings run out on the beat
    Split,   // the colour channels split apart on the beat
    Swing,   // the camera looks somewhere new per beat
    Lens,    // a fisheye bulge on the beat
    Spin,    // turns a notch per beat (only styles that turn)
    Morph,   // the shape morphs on the beat (only shader styles)
};
constexpr int kPersonalityCount = static_cast<int>(Personality::Morph) + 1;

// What the DNA needs to know about a style, from SceneRegistry.
struct StyleTraits {
    // SceneRegistry::spinRate: 0 for a style that holds still. Rotation and
    // camera roll routes go only to styles that turn.
    float spinRate = 0.0f;
    // A shader style reads morph and the second palette (ParamScope.SHADER_LOOK).
    bool shaderLook = false;
};

struct StyleDna {
    // The DNA leaves the last slots free, so the user and the Events tab's
    // recipes can add routes without removing any.
    static constexpr int kMaxRoutes = ReactionEngine::kRoutes - 3;
    // The groove's layers (the pulse … the transient) fill at most this many;
    // the musical layers (bars, tune, harmony and form, feel and timbre) come
    // after, within kMaxRoutes. The groove gave up one slot for them: a full
    // groove now drops its last-drawn optional layer (a texture, an accent, a
    // rise, a cruise, a lean, a lift) where it used to keep it. A zoom
    // personality's beat zoom brings one more slot of its own.
    static constexpr int kGrooveRoutes = 8;
    static constexpr int kMusicalLayers = 4;
    static_assert(kGrooveRoutes + 1 + kMusicalLayers <= kMaxRoutes, "the beat zoom and the musical layers fit");

    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    int count = 0;
    Personality lead = Personality::Pulse;    // on the kick side
    Personality second = Personality::Chroma; // on the snare side
};

StyleDna buildStyleDna(std::string_view styleId, int seed, const StyleTraits& traits);

// ── Customize › Music: the hand wins per moment ─────────────────────────────────────────────────────
//
// The moments and the hand mask are in viz/Reactions.hpp, where the director reads them. Here: how the
// user's routes and the live DNA merge, and what each chip on the page does to the routes. Kotlin only
// labels the rows and chips and calls these through viz/ModDocument (api/geode_modulation_api.h).

// What a moment can do to the picture, one chip each. The ordinal is the edit call's value, never saved.
enum class MusicAction { Flash, Zoom, Shake, Colour, Camera, Warp };
constexpr int kMusicActionCount = static_cast<int>(MusicAction::Warp) + 1;

using RouteArray = std::array<ReactRoute, ReactionEngine::kRoutes>;

// The user's side of the matrix: their routes and the mask of what is theirs. `user` false is "all Auto"
// (no list of their own), whatever `mask` says.
struct HandRoutes {
    RouteArray routes{};
    int count = 0;
    bool user = false;
    std::uint32_t mask = 0;
};

// What plays: the user's routes on the hand's sources, then the DNA's on every other source, so an
// untouched moment re-deals with the style and seed. The hand's routes come first: a full matrix drops a
// DNA route, never one the user set. Returns the count; slots past it are disabled.
int mergeHand(const HandRoutes& user, const StyleDna& dna, RouteArray& out);

// One row of the Music page, read from what plays now.
struct MomentState {
    bool mine = false;      // the hand's: the "Mine" mark and the ⋮ › Reset to Auto
    int routes = 0;         // how many routes play for it (its Strength shows while > 0)
    float strength = 1.0f;  // its routes' amounts against each action's 1×, averaged; 1 when it does nothing
    std::uint32_t on = 0;   // bit per MusicAction: one of the things it does now
    std::uint32_t fits = 0; // bit per MusicAction: turning it on still fits the matrix's kRoutes
};
MomentState momentState(const HandRoutes& user, const RouteArray& playing, int count, Moment m);

enum class MusicEdit { Toggle, Strength, ResetToAuto };
constexpr int kMusicEditCount = static_cast<int>(MusicEdit::ResetToAuto) + 1;
constexpr float kMaxMomentStrength = 2.0f;

// One tap on the page. Toggle turns `action` on or off for `m`; Strength scales `m`'s routes to `value`;
// both make `m` the hand's, with its routes as they played (DNA ones included) plus the change.
// ResetToAuto hands `m` back to the DNA; with nothing left the hand's, the result is "all Auto".
HandRoutes editMusic(const HandRoutes& user, const StyleDna& dna, MusicEdit op, Moment m, MusicAction action, float value);

// The target a new route of `action` moves: the shell hides chips whose target the style ignores.
LfoTarget musicActionTarget(MusicAction action);

// ── Customize › Music: the ready-made recipes ───────────────────────────────────────────────────────
//
// "When this happens in the music, do that": each recipe is only a bundle of routes, so it runs in the
// same matrix as the DNA and the user's routes, and turning it off takes its routes back out. The amounts
// and attack/release times come from docs/CUSTOMIZE_MACROS_RESEARCH.md § Event reactions; the flash-prone
// ones (bloom, brightness, colour steps) still pass through safety::apply. Each one's title and sentence are
// viz/ModCatalog's; the shell toggles one through viz/ModDocument.

// Wire ordinals, in the menu's order; append only.
enum class EventRecipe {
    KickPunch, SnareCrack, HatShimmer, SubWeight, LoudnessGlow, SwellBreathe, BuildupRush, SectionColours,
    BeatTurn, TimbreTurn, PitchTint, FluxTexture, StereoSway, BarHue, HitChaos,
    // The sound engine's signals the recipes above leave alone: toggles and randoms per hit, melody,
    // silence and return, slow activity and the music-driven clocks.
    KickSwap, KickAngle, SnareRoll, MelodyLift, NoteHue, GlideTwist, VibratoShimmer, SlideSmear, SilenceFade,
    ReturnFlash, BassPull, ClockOrbit, BarTurn, ScaleMood, BuildAndDrop,
};
constexpr int kEventRecipeCount = static_cast<int>(EventRecipe::BuildAndDrop) + 1;
constexpr int kMaxRecipeRoutes = 3;

struct RecipeRoutes {
    std::array<ReactRoute, kMaxRecipeRoutes> routes{};
    int count = 0;
};

// Whether a route on `target` can move the style `styleId` (the target's catalogue::Scope reaches it), so a
// recipe never adds a route the picture ignores (fluid splat force on a shader style).
bool targetReaches(LfoTarget target, const std::string& styleId);

// Every route the recipe holds, whatever the style.
RecipeRoutes recipeRoutes(EventRecipe r);
// The recipe's routes the style can actually show; none means the menu leaves the recipe out.
RecipeRoutes recipeRoutesFor(EventRecipe r, const std::string& styleId);
// Every route the recipe would add to the style is in `current` (on the same source and target).
bool recipeIsOn(EventRecipe r, const RouteArray& current, int count, const std::string& styleId);
// Turning it on leaves `current` within ReactionEngine::kRoutes.
bool recipeFits(EventRecipe r, const RouteArray& current, int count, const std::string& styleId);
// `current` with the recipe turned off if it is on, or on if it is off and fits; returns the count. Turning
// on replaces a route already on the same source and target rather than stacking a second one, so a recipe
// tapped over the DNA's own kick push does not double it; a full matrix refuses it and keeps `current`.
int toggleRecipe(EventRecipe r, const RouteArray& current, int count, const std::string& styleId, RouteArray& out);

}  // namespace geode::viz
