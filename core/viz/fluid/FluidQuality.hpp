#pragma once
#include <algorithm>
#include <array>

namespace geode::viz::fluid::quality {

// Port of FluidQuality.kt.
struct Tier {
    const char* label;
    int simRes;
    int dyeRes;
    int particleSide;
    int iterations;
};

constexpr std::array<Tier, 5> kTiers = {{
    {"Ultra", 256, 1024, 1024, 28},
    {"High", 192, 768, 768, 24},
    {"Medium", 128, 512, 512, 20},
    {"Low", 96, 384, 320, 16},
    {"Min", 64, 256, 160, 12},
}};

constexpr int kTierCount = static_cast<int>(kTiers.size());

inline const Tier& tier(int index) { return kTiers[static_cast<size_t>(std::clamp(index, 0, kTierCount - 1))]; }

inline int effectiveIndex(int userIndex, int autoDowngradeSteps) {
    return std::min(std::clamp(userIndex, 0, kTierCount - 1) + std::max(autoDowngradeSteps, 0), kTierCount - 1);
}

}  // namespace geode::viz::fluid::quality
