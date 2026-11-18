#pragma once
#include <string_view>

#include "viz/Director.hpp"

// Which style Auto steps to at a director moment (owner: "make it morph and progress"): never a random jump.
// A returning section brings back a style of its own family, stepped on by its returns; a new section or a
// long stretch steps to the next style along the catalogue's order (family by family, as the stage swipes);
// a drop that opens a new section leaps to the next family. With Styles progress off, a return brings back
// its own style and anything else leaves the style on screen.
//
// The renderer whose moments the shell takes publishes its director's intent as it hands the moments out
// (Renderer::takeDirectorStyleMoments), and Auto's pick (geode_auto_pick_among) reads it once. Pure but for
// that one process-wide slot; host-tested in core/tests/director_style_test.cpp.
namespace geode::viz::director {

// The pick did not apply: no intent waits (Auto draws at random, as for a timer tick).
constexpr int kNoIntent = -2;
// Keep the style on screen.
constexpr int kKeep = -1;

// The intent of the moments just taken; replaces any not yet read.
void publishStyleIntent(const StyleIntent& intent);

// An index into `ids` (the shell's pool, one style id a line, in its order; the first entry of an id
// stands for it), kKeep, or kNoIntent when no intent waits. Reads the waiting intent once.
int pickStyle(std::string_view ids, int onScreen);

// The same rule for a given intent, without the slot: what pickStyle applies.
int pickFor(const StyleIntent& intent, std::string_view ids, int onScreen);

}  // namespace geode::viz::director
