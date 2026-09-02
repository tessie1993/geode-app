#pragma once
#include <vector>

#include "viz/fluid/FluidEmitters.hpp"
#include "viz/fluid/FluidLook.hpp"
#include "viz/fluid/FluidParticles.hpp"
#include "viz/fluid/FluidSim.hpp"
#include "viz/scenes/FluidSceneBase.hpp"

namespace geode::viz {

// Port of FluidScene.kt: dye, particles and the look pass over one FluidSim.
class FluidScene : public FluidSceneBase {
public:
    FluidScene(ProgramLoader loader, SceneHost host)
        : FluidSceneBase("fluid", kTimeWrapSeconds, loader, std::move(host)), sim_(loader_), look_(loader_), particles_(loader_) {
        emitters_.choreography = &choreography_;
    }
    ~FluidScene() override { release(); }

    void init() override;
    void resize(int width, int height) override;
    void draw(float timeSeconds) override;
    void release() override;
    GLuint velocityTexture() const override { return sim_.available() ? sim_.velocityTex() : 0; }
    bool isFluid() const override { return true; }
    void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) override { sim_.setInjectionShaders(forceSrc, dyeSrc); }

protected:
    GeodeFeatureFrame idleFeatures(float dt) override;
    bool tierApplied() const override { return appliedParticleSide_ != 0; }
    void onApplyQualityTier(int index, bool userChanged) override;

private:
    static constexpr float kTimeWrapSeconds = 7100.0f;
    static constexpr float kIdleWrapSeconds = 628.31853f;

    fluid::FluidSim sim_;
    fluid::Look look_;
    fluid::Particles particles_;
    fluid::Emitters emitters_;
    std::vector<fluid::Splat> splats_;
    int appliedParticleSide_ = 0;
    float idlePhase_ = 0.0f;
};

}  // namespace geode::viz
