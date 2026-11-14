#pragma once
#include <algorithm>
#include <array>

namespace geode::viz::fluid::quality {

// The fluid engine's whole cost ladder: a tier says how big the grids are and which of the
// expensive refinements it can afford, so a low-end phone gets the same solver with the
// refinements off rather than a different one.
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

// "Lower the quality when the phone struggles": the rungs a fluid scene has stepped down. It used to
// only ever step down, so one hot spell or one janky moment cost the scene its quality for as long
// as it stayed resident, which in a long session is hours. Now a long run of kept-up frames steps
// back up one rung; a step up that a deficit undoes
// within the probe doubles the next wait, so a style too heavy for the phone settles instead of flapping.
struct AutoDowngrade {
    static constexpr float kRelaxSeconds = 45.0f;
    static constexpr float kProbeSeconds = 15.0f;
    static constexpr float kRelaxMaxSeconds = 720.0f;

    int steps = 0;
    float keptUpSeconds = 0.0f;
    float relaxSeconds = kRelaxSeconds;
    float sinceRelaxSeconds = kProbeSeconds;  // at kProbeSeconds: no probe in flight

    void reset() { *this = AutoDowngrade{}; }

    // One frame: `relief` rungs the monitor asks for (0 = none), `keepingUp` whether its window holds the
    // target. True when the rung changed, so the caller restarts its monitor.
    bool onFrame(int relief, bool keepingUp, float dt) {
        if (sinceRelaxSeconds < kProbeSeconds) {
            sinceRelaxSeconds += dt;
            if (sinceRelaxSeconds >= kProbeSeconds) relaxSeconds = kRelaxSeconds;
        }
        if (relief > 0) {
            if (sinceRelaxSeconds < kProbeSeconds) {
                relaxSeconds = std::min(relaxSeconds * 2.0f, kRelaxMaxSeconds);
                sinceRelaxSeconds = kProbeSeconds;
            }
            keptUpSeconds = 0.0f;
            steps = std::min(steps + relief, kTierCount - 1);
            return true;
        }
        if (steps == 0 || !keepingUp) {
            keptUpSeconds = 0.0f;
            return false;
        }
        keptUpSeconds += dt;
        if (keptUpSeconds < relaxSeconds) return false;
        keptUpSeconds = 0.0f;
        steps--;
        sinceRelaxSeconds = 0.0f;
        return true;
    }
};

}  // namespace geode::viz::fluid::quality
