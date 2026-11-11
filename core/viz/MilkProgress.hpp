#pragma once

#include <algorithm>

// A .milk preset's `progress`: projectM hands per-frame code and shaders the preset's age over the preset
// duration it was started with, held at 1 from then on (TimeKeeper::PresetProgressA), locked or not: its lock
// only stops the switch request. So with Auto's timer running the duration is the wait the timer drew for
// this look, and a preset nothing is timed to replace (Auto off, an export) runs over projectM's own default
// and then holds at 1. (MilkDrop itself keeps a locked preset at 0; presets written to move on `progress`
// then never do, so projectM's rule is the one kept.) The duration only feeds `progress`: the scene keeps
// projectM's lock on either way.
namespace geode::viz::milk {

// projectM's default preset duration: what a held preset's progress runs over.
inline constexpr double kHeldSeconds = 30.0;
// projectM's own floor for a preset's duration.
inline constexpr double kMinSeconds = 1.0;

// holdS: auto_timer::liveHoldSeconds(), 0 when no timer runs. An export draws the look as it was pinned,
// whatever the live timer does meanwhile, so its presets are held ones.
inline double progressSeconds(double holdS, bool offscreen) {
    if (offscreen || !(holdS > 0.0)) return kHeldSeconds;
    return std::max(holdS, kMinSeconds);
}

// projectM's rule, for the host test: ageS on the clock the engine is stepped by.
inline double progressAt(double ageS, double durationS) {
    return std::clamp(ageS / std::max(durationS, kMinSeconds), 0.0, 1.0);
}

}  // namespace geode::viz::milk
