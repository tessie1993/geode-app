#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "viz/panel/CustomizeMap.hpp"

// Customize's dice (geode_panel_roll*, geode_mod_waves_roll): the lock set a roll keeps, whether the steady
// waves roll with it, and the seed of each roll. The rolls themselves are ParamRandomizer, CustomizeMap,
// RandomizeAll and WaveRandomizer. Host-tested in core/tests/param_history_test.cpp.
namespace geode::viz::panel_roll {

inline constexpr int kChanged = 1;
inline constexpr int kWaves = 2;

// The user's kept controls (one key or "#CATEGORY" a line), plus "#PRO" for the roll without Pro: every control
// below a Basics holds.
inline std::string locks(std::string_view kept, bool basicsOnly) {
    std::string out(kept);
    if (basicsOnly) {
        if (!out.empty()) out += '\n';
        out += customize::kProLock;
    }
    return out;
}

// What a roll asks for next. Only the roll of everything re-rolls the steady waves already on (the waves are
// Expert's, which has no roll of its own), and never without Pro.
inline int bits(bool changed, bool everything, bool basicsOnly) {
    return (changed ? kChanged : 0) | (everything && !basicsOnly ? kWaves : 0);
}

// A fresh seed per roll: splitmix64 over a clock-seeded state, process-wide.
inline std::uint64_t nextSeed() {
    static std::mutex lock;
    static std::uint64_t state = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::lock_guard<std::mutex> guard(lock);
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

}  // namespace geode::viz::panel_roll
