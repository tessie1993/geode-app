#pragma once
#include <array>
#include <utility>

#include "api/geode_api.h"
#include "viz/LiveSignal.hpp"

namespace geode::viz::fluid {

// Port of FluidChoreography.kt: the spawn and catch anchors walking a path on heard energy.
class Choreography {
public:
    static constexpr int kMaxSpawn = 8;
    static constexpr int kMaxCatch = 4;
    static constexpr int kPathOrbit = 0;
    static constexpr int kPathLissajous = 1;
    static constexpr int kPathRose = 2;
    static constexpr int kPathBloom = 3;
    static constexpr int kPathDrift = 4;
    static constexpr int kPathCount = 5;
    static constexpr float kGoldenAngle = 2.399963f;

    static float sceneSpeed(float speed);

    struct Anchor {
        float x = 0.0f;
        float y = 0.0f;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float energy = 0.0f;
        void follow(float dt);
        void snap();
    };

    int path = kPathLissajous;
    int spawnCount = 3;
    int catchCount = 2;
    float progressionAmount = 1.0f;
    float speed = 1.0f;

    const std::array<Anchor, kMaxSpawn>& spawns() const { return spawns_; }
    const std::array<Anchor, kMaxCatch>& catches() const { return catches_; }
    int hitCount() const { return hitCount_; }

    void tick(const GeodeFeatureFrame& f, float dt, float aspect);
    void reset();
    void packSpawns(float* out) const;
    void packCatches(float* out, float pull, float captureRadius) const;

private:
    static constexpr float kTimeWrapSeconds = 7100.0f;
    static constexpr float kFollowRate = 2.2f;
    static constexpr float kMaxSpeed = 4.5f;
    static constexpr float kDomainMargin = 0.92f;

    std::pair<float, float> spawnTarget(int i, int n, float progress, float ax) const;
    std::pair<float, float> catchTarget(int i, int n, float progress, float ax) const;

    std::array<Anchor, kMaxSpawn> spawns_{};
    std::array<Anchor, kMaxCatch> catches_{};
    int hitCount_ = 0;
    float time_ = 0.0f;
    int lastSection_ = -1;
    float sectionPhase_ = 0.0f;
    bool initialized_ = false;
    float beatEnv_ = 0.0f;
    float bassEnv_ = 0.0f;
    live::Edge hitEdge_;
    live::Traverse traverse_;
};

}  // namespace geode::viz::fluid
