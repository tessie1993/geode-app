#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "viz/Params.hpp"

// Randomize everything: the two-circle gesture's action (Settings › Behaviour › Gestures). A new
// style, a look for it, and every Customize category rolled on top. Pure: no GL, no JNI, so the host
// tests build it (core/tests/randomize_all_test.cpp).
//
// WHY native (ADR 0003): which style and which look are decisions, and the roll is parameter maths;
// the shell only gathers what it alone knows (the styles the entitlement locks, the looks it holds,
// how many MilkDrop files are on disk), applies the pick through its usual gates and hands back the
// look on screen for the roll.
//
// The order, and why:
//   1. pick() chooses the style, among every catalogue style the shell did not name as locked
//      (pro/FreeTier through FeatureGate.allowsStyle, the rule Auto's draw and the stage's swipe use),
//      never the one on screen while another is open. MilkDrop only when a .milk file exists.
//   2. On that style it chooses a look: a saved or bundled preset made for it when one exists (it
//      carries what no roll moves: the held controls, the never-rolled fields, a custom shader,
//      attack and decay), else, on MilkDrop, a random .milk file, which is MilkDrop's own preset.
//   3. roll() then rolls every Customize category in panel order over that look, each from its
//      own seed, so every category moves, not only the one a page has open. Kept controls (a key
//      or a whole "#CATEGORY" in the lock set) hold the value they had before the gesture, through
//      the preset as well as the roll: Keep means this never touches them.
// Safety and reduced motion are not here: the resolver's safety::apply clamps whatever this gives.
namespace geode::viz::randomize_all {

struct Pick {
    std::string style;  // empty when no style is open
    int look = -1;      // index into the looks the shell listed, or -1 for none
    int milk = -1;      // index into the shell's .milk files, or -1 (only on MilkDrop with no look)
};

// lookStyles[i] is the style id the shell's i-th look (preset) was made for, in its own order.
// milkCount is how many .milk files the shell can load; 0 leaves MilkDrop out of the draw.
Pick pick(uint64_t seed, const std::string& current, const std::set<std::string>& lockedStyles,
          const std::vector<std::string>& lookStyles, int milkCount);

// base (the look on screen once the pick is applied) with every control the lock set keeps put back
// to its value in before (the look before the gesture), then every Customize category's controls
// that reach styleId and are not kept rolled, category by category. locked is the panel's lock set
// as it stands: control keys and "#CATEGORY" markers, plus "#PRO" for a user without Pro: the dice's
// free rule (customize::kProLock), so the roll moves the free Basics only and every control below a
// Basics keeps the new preset's value (it is not put back: #PRO is not Keep). With rollCategories
// false only the kept controls are put back: the style and preset still change.
SceneParams roll(const SceneParams& before, const SceneParams& base, uint64_t seed, const std::string& styleId,
                 const std::set<std::string>& locked, bool rollCategories = true);

}  // namespace geode::viz::randomize_all
