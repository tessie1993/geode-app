#pragma once

#include <array>

// Life's births (ParamTable.inc life): things born at spawn points of their own, which drift away and then
// collapse in on themselves. Three births at a time, each a copy of the style's own world grown from nothing at
// a seeded point off the centre; each drifts (translation only, nothing turns), then dies by one of three
// kinds that rotate from birth to birth:
//
//   Implode   it shrinks into its own centre.
//   Fold back its far half folds over onto the near half across a plane that sweeps through it, until
//             nothing is left.
//   Cave in   a hollow opens on one side and eats inward until the whole thing has caved in.
//
// This decides when and where; the shaders (lib_grow's births, composite_frag's for the kinds without a
// field) only draw what it hands them. The clock is the scene's own, already Speed-integrated (ShaderScene's
// shaderTime_, the renderer's lifeClock_), and nothing else goes in: no sound, no tempo, no beat. Births are
// an engine rhythm of their own, not a reaction, so the reaction matrix still owns everything the music
// moves. Life itself is a param, so a route or a knob the user sets can still drive it.
//
// Life scales how many are present (the first from just above 0, all three by 1, each fading in rather than
// popping) and how far they drift. At Life 0 every value is exactly 0: nothing is born and the shaders take
// their old path, bit for bit. Pure, any thread; host-tested in core/tests/births_test.cpp.
namespace geode::viz::births {

inline constexpr int kBirths = 3;

// Two vec4 per birth, uploaded as uBirths[6] (and uPostBirths for the composite):
//   [2b]     the spawn point (x, y, z) in birth units (lib_grow: 0.3 of the frame's near depth, or of the screen
//            height in 2D), and the scale (0 while unborn, up to kMaxScale grown);
//   [2b + 1] the kind (0 Implode, 1 Fold back, 2 Cave in), how far it has collapsed (0..1), and the cosine and
//            sine of the side it folds or caves from (fixed for the birth's life: nothing turns).
inline constexpr int kFloats = kBirths * 8;

enum class Kind : int { Implode = 0, FoldBack = 1, CaveIn = 2 };

// One birth's whole life, seconds of the scene clock: born, drifting, collapsed. 7100 / 800, so the scene
// clocks' wrap at 7100 s lands on a life's end and the cycle runs on unbroken.
inline constexpr float kPeriod = 7100.0f / 800.0f;

// The largest a grown birth is, as a share of the world it copies.
inline constexpr float kMaxScale = 0.35f;

// The farthest any value puts a birth's middle from where the shaders measure (birth units), with its drift:
// lib_grow's march slack and the composite's reach are built from it.
inline constexpr float kMaxReach = 2.4f;

// The births at scene clock `clock` (seconds, Speed-integrated, wrapping at 7100) and Life `life` (0..1).
std::array<float, kFloats> compute(float clock, float life);

}  // namespace geode::viz::births
