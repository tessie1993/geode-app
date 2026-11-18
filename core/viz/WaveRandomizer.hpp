#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

// Randomize for the waves (Customize › Expert › Waves): a whole set of LFO slots rolled at once. Pure:
// no GL, no JNI, so the host tests build it (core/tests/lfo_random_test.cpp).
//
// A rolled set is meant to look good, not only to be legal:
//   - each wave moves a different kind of control (motion, shape, colour, camera, the style's engine),
//     drawn from what the style reads and the slot may move (modtargets::offeredTo), so three waves
//     never pile onto one thing and at most one sits on colour;
//   - the period is log-uniform, 1-12 s for motion, shape and engine, 4-24 s for colour and camera;
//     two in five are beat-timed (2-16 beats, 4-16 for colour and camera) on the measured tempo, and
//     keep the rolled period for when the music has given none;
//   - the depth is 0.15-0.45 (0.1-0.3 on colour): visible, never the whole range;
//   - colour and camera only glide (sine, triangle, random glide); the stepping shapes are for the rest;
//   - the direction follows where the control sits: up only from its low end, down only from its high
//     end, both ways in between.
// Left out on purpose: brightness and intensity (a wave there reads as flicker), tile, pixelate and
// posterize (they jump), camera shake, the chained targets and the liquid's controls while Liquid is off.
namespace geode::viz::waves {

using Set = std::array<LfoConfig, LfoEngine::kSlots>;

enum class Roll {
    All,      // the wave editor's Randomize: every slot switched on and rolled
    Running,  // a roll of everything: only the steady waves already on; nothing is switched on
};

// The panel piece the waves are in CustomizeMap: kept (by itself or through "#EXPERT"), a roll holds them.
inline constexpr const char* kLockKey = "@waves";

// The same seed, style, params and waves give the same set. Returns current when nothing can be rolled.
Set randomize(const Set& current, const SceneParams& params, std::uint64_t seed, const std::string& styleId, Roll roll);

// The kind of control a target is (0..kKinds-1), or -1 for one the roll never picks.
constexpr int kKinds = 5;
constexpr int kCameraKind = 3;
int kindOf(LfoTarget target);

// The slots as floats (the lfo rows of viz/ConfigTable.inc), one after another.
constexpr int kWireFloats = LfoEngine::kSlots * GEODE_LFO_CONFIG_FLOATS;
Set fromWire(const float* values);
void toWire(const Set& set, float* out);

}  // namespace geode::viz::waves
