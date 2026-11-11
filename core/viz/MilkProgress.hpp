#pragma once

#include <algorithm>

// A .milk preset's `progress`: projectM hands per-frame code and shaders the preset's age over the preset
// duration it was started with, held at 1 from then on (TimeKeeper::PresetProgressA). MilkDrop's is the age
// over the time until its next preset, and 0 for as long as the preset is locked (plugin.cpp restarts the
// preset's clock every frame under Scroll Lock or a menu). So the duration is the time Auto's timer expects
// the preset to show, and a preset nothing is timed to replace (Auto off, an export) is MilkDrop's locked
// one. The scene keeps projectM's own lock on either way: the duration only feeds `progress`.
namespace geode::viz::milk {

// Long enough that a held preset's progress never leaves 0: a day reads as 0.09.
inline constexpr double kHeldSeconds = 999999.0;
// projectM's own floor for a preset's duration.
inline constexpr double kMinSeconds = 1.0;

// holdS: auto_timer::liveHoldSeconds(), 0 when no timer runs. An export draws the look as it was pinned,
// whatever the live timer does meanwhile, so its presets are held.
inline double progressSeconds(double holdS, bool offscreen) {
    if (offscreen || !(holdS > 0.0)) return kHeldSeconds;
    return std::max(holdS, kMinSeconds);
}

}  // namespace geode::viz::milk
