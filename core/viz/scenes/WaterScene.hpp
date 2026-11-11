#pragma once
#include <array>
#include <random>
#include <vector>

#include "viz/Quad.hpp"
#include "viz/fluid/FluidEmitters.hpp"
#include "viz/fluid/RippleSim.hpp"
#include "viz/scenes/FluidSceneBase.hpp"

namespace geode::viz {

// The water surface with an ink layer, its drops from the emitters and the fingers.
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
    void queueRipple(float x, float y, float amplitude) override;

protected:
    GeodeSoundFrame idleFeatures(float dt) override;
    void onApplyQualityTier(int index, bool userChanged) override;

private:
    static constexpr float kInkGain = 0.8f;
    static constexpr float kTimeWrapSeconds = 628.31853f;

    static int gridResFor(int tierIndex);
    void queueIdleRain(float dt);
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
    std::array<float, 3> rgb_{};
};

}  // namespace geode::viz
