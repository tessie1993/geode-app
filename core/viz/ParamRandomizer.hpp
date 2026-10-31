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

// A new look from `seed`: every rolled control not in `locked`, owned by `tab` (every tab when
// empty), that reaches `styleId` (any style when empty). Tries up to eight seeds derived from
// `seed` until something changes; returns `current` unchanged when nothing can.
SceneParams randomize(const SceneParams& current, uint64_t seed, std::optional<Tab> tab, const std::string& styleId,
                      const std::set<std::string>& locked);

// `current` with exactly the fields `tab` rolls put back to their defaults.
SceneParams resetTab(const SceneParams& current, Tab tab);

}  // namespace geode::viz::randomizer
