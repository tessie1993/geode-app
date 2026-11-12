#pragma once
#include <array>

#include "audio/dsp/Equalizer.hpp"

namespace geode::audio {

// The built-in equalizer curves, one level per band in millibels, low band first. The settings store a
// curve by its place in this table, so a new curve goes at the end and none moves or leaves. The shell
// names them in the same order.
inline constexpr int kEqPresets = 6;

inline constexpr std::array<std::array<int, Equalizer::kBands>, kEqPresets> kEqPresetLevels{{
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                           // flat
    {600, 500, 350, 150, 0, 0, 0, 0, 0, 0},                   // bass
    {0, 0, 0, 0, 0, 100, 250, 400, 500, 550},                 // treble
    {-200, -150, -50, 100, 300, 350, 300, 150, 0, -100},      // vocal
    {450, 350, 150, -50, -150, -50, 150, 300, 400, 450},      // rock
    {500, 400, 100, 0, -150, 0, 150, 300, 450, 500},          // electronic
}};

}  // namespace geode::audio
