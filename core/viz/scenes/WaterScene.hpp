#pragma once
#include <array>
#include <mutex>
#include <random>
#include <vector>

#include "viz/Quad.hpp"
#include "viz/fluid/FluidEmitters.hpp"
#include "viz/fluid/RippleSim.hpp"
#include "viz/scenes/FluidSceneBase.hpp"

namespace geode::viz {

// Port of WaterScene.kt: the ripple height field with an ink layer, fed by the emitters and the fingers.
class WaterScene : public FluidSceneBase {
public:
    WaterScene(ProgramLoader loader, SceneHost host) : FluidSceneBase("water", kTimeWrapSeconds, loader, std::move(host)), sim_(loader_) {
        sim_.inkEnabled = true;
        emitters_.choreography = &choreography_;
    }
    ~WaterScene() override { release(); }

    void init() override;
    void resize(int width, int height) override { sim_.resize(width, height); }
    void draw(float timeSeconds) override;
    void release() override;
    bool isWater() const override { return true; }
    void queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength) override;

protected:
    GeodeFeatureFrame idleFeatures(float dt) override;
    void onApplyQualityTier(int index, bool userChanged) override;

private:
    static constexpr float kTouchRadius = 0.11f;
    static constexpr int kMaxTouchBacklog = 24;
    static constexpr float kInkGain = 0.8f;
    static constexpr float kTimeWrapSeconds = 628.31853f;

    static int gridResFor(int tierIndex);
    void queueIdleRain(float dt);
    void drainTouchStrokes(float rippleStrength, float baseHue);
    float nextFloat() { return uniform_(rng_); }

    fluid::RippleSim sim_;
    fluid::Emitters emitters_;
    std::vector<fluid::Splat> splats_;
    UniformCache display_{0};
    bool displayOk_ = false;
    FullscreenTriangle quad_;
    float idlePhase_ = 0.0f;
    float rainAccum_ = 0.0f;
    std::mt19937 rng_{std::random_device{}()};
    std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
    std::mutex strokeLock_;
    std::vector<std::array<float, 6>> touchStrokes_;
    std::vector<std::array<float, 6>> drainedStrokes_;
    std::array<float, 3> rgb_{};
};

}  // namespace geode::viz
