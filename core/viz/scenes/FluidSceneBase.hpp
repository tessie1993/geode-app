#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>

#include "viz/Scene.hpp"
#include "viz/ThermalGovernor.hpp"
#include "viz/fluid/FluidChoreography.hpp"
#include "viz/fluid/FluidParticles.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"

namespace geode::viz {

// Port of FluidSceneBase.kt: the shared clock, idle synthesis, quality ladder and GL state guard.
class FluidSceneBase : public Scene {
public:
    FluidSceneBase(std::string id, float timeWrapSeconds, ProgramLoader loader, SceneHost host)
        : id_(std::move(id)), timeWrapSeconds_(timeWrapSeconds), loader_(loader), host_(std::move(host)) {}

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Fluid; }
    void setParams(const SceneParams& params) override { params_ = params; }
    void acceptPcm(const float* samples, int count) override { pcmPulse_.accept(samples, count); }
    void update(const GeodeFeatureFrame& features, float dt) override;

protected:
    float tickPcm(float dt) { return pcmPulse_.tick(dt); }
    virtual GeodeFeatureFrame idleFeatures(float dt) = 0;
    GeodeFeatureFrame scaledFeatures();
    bool isIdle() const { return !hasPending_ && featuresAgeSec_ >= 0.25f; }
    void fillIdleBands(float t, float amp);
    GeodeFeatureFrame idleAudioFeatures(float bass, float mid, float treble, float rms) const;
    virtual bool tierApplied() const { return true; }
    virtual void onApplyQualityTier(int index, bool userChanged) = 0;
    void applyQualityTier();
    void autoQualityTick();
    void configureChoreography();
    void applyChoreographyTo(fluid::Particles& particles);
    void saveFramebufferAndViewport();
    void saveGlState();
    void restoreFramebufferAndViewport();
    void restoreBlend();
    int savedViewportWidth() const { return prevViewport_[2]; }
    int savedViewportHeight() const { return prevViewport_[3]; }
    float viewportDpiScale() const { return particle_look::dpiScale(savedViewportHeight()); }

    std::string id_;
    float timeWrapSeconds_;
    ProgramLoader loader_;
    SceneHost host_;
    fluid::Choreography choreography_;
    PerformanceMonitor monitor_;
    float pcmStrike_ = 0.0f;
    SceneParams params_;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    bool hasLast_ = false;
    GeodeFeatureFrame last_{};
    float featuresAgeSec_ = 0.0f;
    int autoDowngrade_ = 0;
    int lastUserQuality_ = -1;
    int appliedTier_ = -1;

private:
    PcmPulse pcmPulse_;
    GLint prevFbo_ = 0;
    std::array<GLint, 4> prevViewport_{};
    std::array<GLint, 4> prevBlendFunc_{};
    bool blendWas_ = false;
    std::array<float, fluid::Choreography::kMaxSpawn * 4> spawnPack_{};
    std::array<float, fluid::Choreography::kMaxCatch * 4> catchPack_{};
    std::array<float, GEODE_BAND_COUNT> idleBands_{};
};

}  // namespace geode::viz
