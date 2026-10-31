#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "viz/Params.hpp"

// The Customize panel's Randomize and Reset-tab buttons: what each control rolls to, which tab owns
// it, and which fields it writes. Pure: no GL, no JNI, so the host tests build it.
//
// A control is named by its ParamKeys string (the label on screen, the key the user locks by).
// A roll touches only what the style on screen can show (catalogue::reaches), so Randomize never
// spends itself on a parameter the user cannot see.
namespace geode::viz::randomizer {

// The tabs that own parameters. Names match Kotlin's CustomizeTab; EVENTS owns none.
enum class Tab { Shape, Color, Motion, Fx, Reactivity, Scene, Fluid, Cymatics };

// The lock and roll key of the style's reaction DNA seed (the Reactivity tab's Re-roll button).
constexpr const char* kReactionDna = "Reaction DNA";

struct RolledKey {
    Tab tab;
    const char* key;
    // The SceneParams fields the roll writes (Kotlin property names); nullptr past the last.
    std::array<const char*, 3> fields;
};

// Every rolled control in roll order, tab by tab.
const std::vector<RolledKey>& rolledKeys();

// The fields no roll writes, each with why. Every SceneParams field is either rolled by exactly
// one control or listed here.
const std::vector<std::pair<const char*, const char*>>& neverRolled();

std::optional<Tab> tabNamed(std::string_view name);
const char* name(Tab tab);

// How far a roll goes. Randomize re-rolls every control it covers. Mutate keeps the look and
// nudges it: each float moves kMutateReach of the way toward a fresh roll, and each choice or
// switch (a palette, the kaleidoscope, an effect that is off) takes the fresh roll only
// kMutateFlip of the time. Mutate never re-rolls the reaction DNA, because a new personality is
// not a nudge.
enum class Strength { Mutate, Randomize };
constexpr float kMutateReach = 0.2f;
constexpr float kMutateFlip = 0.12f;

struct RollRequest {
    std::optional<Tab> tab;          // every tab when empty
    std::string styleId;             // any style when empty
    std::set<std::string> locked;    // ParamKeys strings the roll leaves alone
    Strength strength = Strength::Randomize;
    std::set<std::string> only;      // when not empty, only these controls (auto-visuals' colour roll)
};

// A new look from `seed`: every rolled control the request covers that reaches its style, drawn
// from the style's own good ranges (boundsFor), then kept coherent (one heavy effect, one
// symmetry, a light and a warp budget) and in colour harmony (palette 2 against palette 1). Tries
// up to eight seeds derived from `seed` until something changes; returns `current` unchanged when
// nothing can. The same arguments always give the same look.
SceneParams randomize(const SceneParams& current, uint64_t seed, const RollRequest& request);
SceneParams randomize(const SceneParams& current, uint64_t seed, std::optional<Tab> tab, const std::string& styleId,
                      const std::set<std::string>& locked);

// The controls auto-visuals rolls when it changes the colours with the look.
const std::set<std::string>& paletteKeys();

// A style's own range for a control, where it narrows the global one. A full range on every style
// is what makes a roll look broken: a domain warp that smears a raymarched scene into soup, a
// kaleidoscope folded over a landscape, a tiled tunnel. Rules stack from broad to narrow: kind,
// then marched and still styles, then the family; the last rule for a key wins.
struct Bounds {
    bool hasRange = false;
    float lo = 0.0f;
    float hi = 0.0f;
    float chance = -1.0f;  // the odds an optional effect is on; < 0 keeps the roll's own
};
std::optional<Bounds> boundsFor(const std::string& styleId, std::string_view key);
// Every key a bounds rule names, so the tests can check each one is a rolled control.
std::vector<std::string> boundedKeys();

// Colour harmony on the palettes' base hues (0..1 around the wheel). Wide palettes (range at least
// kWidePalette, a rainbow already) only take analogous partners; narrow ones also take the
// complement, a split complement or a triad.
enum class Harmony { Analogous, Complementary, SplitComplementary, Triad };
constexpr float kWidePalette = 0.45f;
// The hue a partner sits at; `side` (+1 or -1) picks which neighbour or which split.
float harmonyHue(float base, Harmony harmony, int side);
// The palette whose base hue is nearest `hue`, never `avoid`, narrow ones first.
int nearestPalette(float hue, int avoid);

// `current` with exactly the fields `tab` rolls put back to their defaults.
SceneParams resetTab(const SceneParams& current, Tab tab);

}  // namespace geode::viz::randomizer
