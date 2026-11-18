#pragma once
#include "viz/Params.hpp"

// The look's own palette inside its scene params: the two hue overrides (paletteBaseOverride, paletteRangeOverride)
// and your colours' stops (paletteStop0..4). Customize › Look's palette chips, your-colours editor, the artwork
// palette and a saved palette all set it through these, so the rules (what clearing leaves, how many stops a look
// holds, the hue range) live once. Pure; host-tested in core/tests/look_files_test.cpp.
namespace geode::viz::palette_params {

// True when the look draws its own palette rather than built-in `palette`: a hue override set or a first stop set.
// The look's saved palette id means something only while this holds (the shell drops it otherwise).
bool usesCustomPalette(const SceneParams& p);

// The built-in palette again: both overrides unset and every stop unset.
SceneParams withoutCustomPalette(SceneParams p);

// Your colours: the overrides and stops cleared, then the first `count` of `stops` (at most five) from paletteStop0.
SceneParams withStops(SceneParams p, const int* stops, int count);

// A hue gradient: every stop unset, base and span held to 0..1 (NaN as 0) as the two overrides.
SceneParams withGradient(SceneParams p, float base, float span);

// How many stops are set from the first, written to out[0..count-1] (out may be null).
int stopsOf(const SceneParams& p, int* out);

}  // namespace geode::viz::palette_params
