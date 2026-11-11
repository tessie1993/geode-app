#pragma once

#include <array>
#include <cstdint>

#include "sound/SoundFrame.h"
#include "viz/CameraRig.hpp"
#include "viz/Config.hpp"
#include "viz/Params.hpp"
#include "viz/Reactions.hpp"

namespace geode::viz {

// Customize › Macros: six global knobs laid over every style, the way Synesthesia's meta controls sit
// over every scene. They never write the user's SceneParams; each frame the renderer reads the
// process-wide knobs (setGlobal) and folds them into the params it draws with, the sound engine's
// tuning, the camera rig's music moves and a handful of routes of their own. Because the state is
// process-wide, the screen, exports, the wallpaper and the dream all see the same knobs without each
// carrying a copy.
//
// Every knob runs -1..1. 0 is "as styled": nothing changes. Up pushes each driven field toward the top
// of its range (or the bottom, for an inverted one) by that fraction of the distance left, down toward
// the other end, so the result stays in range on any style and the centre is exact.
//
// The knobs' count, range and centre are the macro rows of viz/ConfigTable.inc (#423), one row per knob
// in this enum's order, the same rows the store reads.
enum class Macro { Energy, Speed, Complexity, Mood, Space, Chaos };
constexpr int kMacroCount =
    static_cast<int>(config::Slot::macro_chaos) - static_cast<int>(config::Slot::macro_energy) + 1;
static_assert(kMacroCount == static_cast<int>(Macro::Chaos) + 1, "one macro row per knob");
// The row of knob i (0..kMacroCount-1).
constexpr config::Slot knobSlot(int i) { return static_cast<config::Slot>(static_cast<int>(config::Slot::macro_energy) + i); }
using MacroKnobs = std::array<float, kMacroCount>;

namespace macros {

// The macros' own routes ride in a second reaction engine, so they never take one of the 12 slots the
// style DNA, Events and the user share.
constexpr int kRoutes = 6;

MacroKnobs clamped(const float* values, int count);

void setGlobal(const MacroKnobs& knobs);
MacroKnobs global();

SceneParams apply(const SceneParams& p, const MacroKnobs& k);
// Energy scales Settings › Audio's Reactivity and Speed its clock speed, on top of the user's own.
GeodeSoundTuning tuning(const GeodeSoundTuning& base, const MacroKnobs& k);
CameraRig::Moves camera(const MacroKnobs& k);
// Each knob's routes at a full turn up, scaled by the knob; off at or below centre.
std::array<ReactRoute, ReactionEngine::kRoutes> routes(const MacroKnobs& k);
// Surprise: every knob not in lockMask (bit i = knob i) to a new spot within most of its reach.
// Seeded, so the same seed draws the same knobs.
MacroKnobs surprise(const MacroKnobs& k, unsigned lockMask, uint64_t seed);

struct Starter {
    const char* name;
    MacroKnobs knobs;
};
constexpr int kStarterCount = 7;
const std::array<Starter, kStarterCount>& starters();

}  // namespace macros
}  // namespace geode::viz
