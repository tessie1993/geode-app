#pragma once
#include <algorithm>
#include <array>

namespace geode::viz::fluid::quality {

// Port of FluidQuality.kt, since grown into the engine's whole cost ladder: a tier says how big the
// grids are and which of the expensive refinements it can afford, so a low-end phone gets the same
// solver with the refinements off rather than a different one.
//
// Ultra and High were raised when the splats went into one batched pass: up to sixteen splats used
// to be sixteen full-grid passes on the dye AND on the velocity, and that budget now pays for finer
// grids and for MacCormack's second dye pass instead.
struct Tier {
    const char* label;
    int simRes;          // velocity / pressure / temperature grid, short side
    int dyeRes;          // dye grid, short side
    int particleSide;    // the fluid styles' tracer state texture side (count = side^2)
    int iterations;      // default pressure budget, in full-grid passes
    bool macCormack;     // second-order advection: one extra pass per advected field
    float maxStretch;    // tracer motion-blur streak ceiling; 0 draws plain points
    int curlParticleSide;  // Curl Flow's population, which is ALL that style draws
};

constexpr std::array<Tier, 5> kTiers = {{
    {"Ultra", 288, 1152, 1024, 28, true, 4.0f, 640},
    {"High", 224, 896, 768, 24, true, 3.0f, 512},
    {"Medium", 128, 512, 512, 20, true, 2.5f, 320},
    {"Low", 96, 384, 320, 16, false, 0.0f, 224},
    {"Min", 64, 256, 160, 12, false, 0.0f, 160},
}};

constexpr int kTierCount = static_cast<int>(kTiers.size());

inline const Tier& tier(int index) { return kTiers[static_cast<size_t>(std::clamp(index, 0, kTierCount - 1))]; }

inline int effectiveIndex(int userIndex, int autoDowngradeSteps) {
    return std::min(std::clamp(userIndex, 0, kTierCount - 1) + std::max(autoDowngradeSteps, 0), kTierCount - 1);
}

}  // namespace geode::viz::fluid::quality
