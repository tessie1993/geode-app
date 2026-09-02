#pragma once
#include <array>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "viz/LiveSignal.hpp"
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

// Port of FluidEmitters.kt: the beat, stirrer, suction, sparkle and pump splats for one frame.
class Emitters {
public:
    static constexpr int kPatternCenter = 0;
    static constexpr int kPatternRing = 1;
    static constexpr int kPatternRandom = 2;
    static constexpr int kPatternSpectrumArc = 3;
    static constexpr float kBaseSpeed = 6.0f;
    static constexpr float kBeatResponseGate = 0.05f;

    const Choreography* choreography = nullptr;
    int beatPattern = kPatternRing;
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
    float beatResponse = 1.0f;

    float beatEnv() const { return beatEnv_; }
    float bassEnv() const { return bassEnv_; }

    void applyParams(const SceneParams& p);
    void tick(const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan, std::vector<Splat>& out);

private:
    static constexpr int kMaxSplatsPerFrame = 16;
    static constexpr float kPhaseWrapSeconds = 628.31853f;
    static constexpr float kMaxRadiusSwell = 2.0f;

    void anchor(int i, float aspect);
    void stirrerSplats(std::vector<Splat>& out, const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan, float radius);
    void beatSplatsFor(std::vector<Splat>& out, const GeodeFeatureFrame& f, float aspect, float baseHue, float hueSpan, float radius, float speed);
    void suctionSplats(std::vector<Splat>& out, float radius);
    void sparkleSplats(std::vector<Splat>& out, float aspect, float baseHue, float hueSpan, float radius);
    void pumpSplats(std::vector<Splat>& out, float baseHue, float hueSpan, float radius);
    float nextFloat() { return uniform_(rng_); }
    int nextInt(int bound) { return static_cast<int>(uniform_(rng_) * static_cast<float>(bound)) % bound; }

    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
    float beatEnvRaw_ = 0.0f;
    float beatEnv_ = 0.0f;
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
    live::Edge hitEdge_;
    float anchorX_ = 0.0f;
    float anchorY_ = 0.0f;
    std::array<float, 3> rgb_{};
};

}  // namespace geode::viz::fluid
