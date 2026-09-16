#pragma once
#include <array>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "viz/MotionField.hpp"
#include "viz/Params.hpp"
#include "viz/fluid/FluidChoreography.hpp"

namespace geode::viz::fluid {

struct Splat {
    float prevX;
    float prevY;
    float curX;
    float curY;
    float radius;
    float velX;
    float velY;
    float r;
    float g;
    float b;
};

// Port of FluidEmitters.kt: the stirrer, orbit, suction, sparkle and pump splats for one frame.
// Wave three: every emitter here runs continuously - see Emitters::tick.
class Emitters {
public:
    static constexpr int kPatternCenter = 0;
    static constexpr int kPatternRing = 1;
    static constexpr int kPatternRandom = 2;
    static constexpr int kPatternSpectrumArc = 3;
    static constexpr float kBaseSpeed = 6.0f;

    const Choreography* choreography = nullptr;
    int beatPattern = kPatternRing;
    // beatSplats: now the count of continuously-orbiting emitters (see tick());
    // radiusPulse: read from params but inert - it used to swell the splat
    // radius with the deleted beat envelope and now has nothing to multiply.
    // Both are candidates for R08 to retire from the Reactivity tab.
    int beatSplats = 3;
    int stirrers = 2;
    float stirrerSpeed = 1.0f;
    bool bassPump = false;
    bool sparkle = true;
    float splatRadius = 0.12f;
    float radiusPulse = 0.4f;
    float paletteCycleSpeed = 0.5f;
    float forceScale = 1.0f;
    float catchSuction = 1.0f;

    float bassEnv() const { return bassEnv_; }

    void applyParams(const SceneParams& p);
    // motion is defaulted so callers outside wave three's scope (WaterScene,
    // FlowField) keep compiling unchanged, reading it as the neutral
    // "typical" state (see MotionField::State's defaults).
    void tick(const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan, std::vector<Splat>& out,
              const MotionField::State& motion = MotionField::State{});

private:
    static constexpr int kMaxSplatsPerFrame = 16;
    static constexpr float kPhaseWrapSeconds = 628.31853f;
    static constexpr int kMaxOrbitEmitters = 8;

    void anchor(int i, float aspect);
    void stirrerSplats(std::vector<Splat>& out, const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan, float radius);
    // motion: uBarPhase -> orbit angle, uEnergyRel -> orbit radius.
    void orbitSplat(std::vector<Splat>& out, const GeodeFeatureFrame& f, int i, int n, float aspect, float baseHue, float hueSpan,
                    float radius, float speed, float energyRel);
    void suctionSplats(std::vector<Splat>& out, float radius);
    void sparkleSplats(std::vector<Splat>& out, float aspect, float baseHue, float hueSpan, float radius);
    void pumpSplats(std::vector<Splat>& out, float baseHue, float hueSpan, float radius);
    float nextFloat() { return uniform_(rng_); }
    int nextInt(int bound) { return static_cast<int>(uniform_(rng_) * static_cast<float>(bound)) % bound; }

    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
    float bassEnv_ = 0.0f;
    std::array<float, 4> stirrerAngle_{0.0f, 1.7f, 3.4f, 5.1f};
    std::array<float, 4> stirrerPrevX_{};
    std::array<float, 4> stirrerPrevY_{};
    std::array<bool, 4> stirrerHasPrev_{};
    int activeStirrers_ = 0;
    float trebleMean_ = 0.05f;
    float palettePhase_ = 0.0f;
    float suctionPhase_ = 0.0f;
    int suctionIndex_ = 0;
    // Per-emitter phase accumulators driving the continuous orbit splats
    // (rate from bassRel) and the fine treble-sparkle emitter (rate from
    // trebRel), replacing the transient-edge trigger.
    std::array<float, kMaxOrbitEmitters> emitPhase_{};
    float fineEmitPhase_ = 0.0f;
    float anchorX_ = 0.0f;
    float anchorY_ = 0.0f;
    std::array<float, 3> rgb_{};
};

}  // namespace geode::viz::fluid
