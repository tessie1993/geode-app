#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "viz/Adsr.hpp"
#include "viz/Lfo.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Reactions.hpp"
#include "viz/WaveRandomizer.hpp"

// The user's whole modulation: the three waves, the two envelopes and the reaction routes set by hand,
// with the text each is saved as, every edit an editor makes and every value an editor draws. The shell
// keeps only the three texts (its prefs values) and asks here for the rest, so a default, a range, a
// clamp or "which target may this slot move" exists once. Pure: no GL, the host tests build it.
//
// The saved texts:
//   waves      a JSON array of up to LfoEngine::kSlots objects: enabled, source, target, wave, rateSeconds,
//              depth, polarity, curve, beats, knob, moment (enums by name)
//   envelopes  a JSON array of up to AdsrEngine::kCount objects: enabled, targets (names), attack, decay,
//              sustain, release, amount, band, gateThreshold, sustainTrack, retrigger, knob, moment
//   reactions  viz/ReactionText's "reactions/2" list; no text at all is Auto (the style's own routes play)
// Reading forgives: text that is no array gives every slot its defaults, a missing slot or field its
// default, an unknown name its default (an unknown envelope target is dropped), a number out of range the
// nearest the slot allows. A wave saved with rateHz (cycles a second) and no rateSeconds reads as its period.
namespace geode::viz {

// A wave's fields, in the order lfoValues() writes them.
enum class LfoField { Enabled, Source, Target, Wave, RateSeconds, Depth, Polarity, Curve, Beats, Knob, Moment };
constexpr int kLfoFieldCount = static_cast<int>(LfoField::Moment) + 1;
// The fields, then 1 while the target moves something on the style asked about.
constexpr int kLfoValues = kLfoFieldCount + 1;

// An envelope's fields, in the order adsrValues() writes them; Target is edit-only: it adds the target
// when the envelope does not move it yet and takes it away when it does.
enum class AdsrField {
    Enabled, Attack, Decay, Sustain, Release, Amount, Band, GateThreshold, SustainTrack, Retrigger, Knob, Moment, Target,
};
// The fields before Target; the envelope's target ordinals follow them.
constexpr int kAdsrValues = static_cast<int>(AdsrField::Target);

// A route's fields, in the order playingValues() writes them.
enum class RouteField { Enabled, Source, Target, Mode, Amount, Curve, AttackSeconds, ReleaseSeconds };
constexpr int kRouteFieldCount = static_cast<int>(RouteField::ReleaseSeconds) + 1;
// The fields, then 1 while the target moves something on the style asked about.
constexpr int kRouteValues = kRouteFieldCount + 1;

// One edit of the reactions. The route and recipe edits act on what plays and make that list the
// user's; the music edits change one moment and leave every other with whoever had it.
enum class ReactionEdit {
    RouteSet,       // a = route, b = RouteField, value
    RouteAdd,       // one more route, while the matrix has room
    RouteRemove,    // a = route
    RecipeToggle,   // a = EventRecipe
    MusicToggle,    // a = Moment, b = MusicAction
    MusicStrength,  // a = Moment, value
    MusicReset,     // a = Moment, back to Auto
    Clear,          // every route back to Auto
};
constexpr int kReactionEditCount = static_cast<int>(ReactionEdit::Clear) + 1;

// recipeState()'s bits.
constexpr unsigned kRecipeOn = 1u;       // every route it would add to the style plays
constexpr unsigned kRecipeFits = 2u;     // turning it on stays within the matrix
constexpr unsigned kRecipeOffered = 4u;  // it has a route the style can show

// musicValues() per moment: mine, route count, strength, on-action bits, fits-action bits.
constexpr int kMusicValues = 5;

struct ModDocument {
    using Envelopes = std::array<AdsrConfig, AdsrEngine::kCount>;

    waves::Set lfos{};
    Envelopes adsrs{};
    HandRoutes hand{};

    void readLfos(std::string_view text);
    void readAdsrs(std::string_view text);
    // Text that is no reaction list leaves Auto, whatever the mask.
    void readReactions(std::string_view text, std::uint32_t mask);

    std::string lfosText() const;
    std::string adsrsText() const;
    // "" while Auto.
    std::string reactionsText() const;
    // The hand mask to save beside reactionsText(); 0 while Auto.
    std::uint32_t handMask() const { return hand.user ? hand.mask : 0u; }

    // One field of one slot. An unknown slot or field, or an ordinal outside its enum, changes nothing;
    // a number lands inside the slot's range. Picking a source also picks the direction it is useful in.
    void setLfo(int slot, int field, float value);
    void setAdsr(int slot, int field, float value);
    void editReactions(const StyleDna& dna, const std::string& styleId, int op, int a, int b, float value);

    // Each writes at most `capacity` values and returns how many it wrote.
    int lfoValues(int slot, const std::string& styleId, float* out, int capacity) const;
    int adsrValues(int slot, float* out, int capacity) const;
    int playingValues(const StyleDna& dna, const std::string& styleId, float* out, int capacity) const;
    int musicValues(const StyleDna& dna, float* out, int capacity) const;
    unsigned recipeState(const StyleDna& dna, const std::string& styleId, int recipe) const;

    // The target ordinals a picker lists on a style, in wire order: a wave slot's (0..kSlots-1, with None)
    // or the route editor's (modtargets::kPickerReaction, without None).
    static int targetsOffered(const std::string& styleId, int picker, int* out, int capacity);
    // The envelope's: every target the style shows, and the ones it already moves so they can be taken away.
    int adsrTargetsOffered(int slot, const std::string& styleId, int* out, int capacity) const;
};

}  // namespace geode::viz
