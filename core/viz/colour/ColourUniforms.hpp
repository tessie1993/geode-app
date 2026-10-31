#pragma once
#include "viz/Program.hpp"
#include "viz/colour/ColourEngine.hpp"

namespace geode::viz::colour {

// The colour engine's frame as the uniforms lib_palette.glsl declares (uColourOn, uColourRamp,
// uColourAway, uColourDrift, uColourHue, uColourClash, uColourClashAmt, uColourHighlight, uColourGrade).
// The program `locs` belongs to must be bound. A null frame, or an inactive one, uploads uColourOn = 0,
// which every reader checks first; a name the program never reads costs one dropped call.
void upload(UniformCache& locs, const ColourFrame* frame);

}  // namespace geode::viz::colour
