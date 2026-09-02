#pragma once
#include <GLES3/gl3.h>

#include <memory>
#include <string>

#include "viz/GlProfile.hpp"
#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/compute/SimPass.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of SilkScene.kt: an advected strand field stepped by the compute layer and shown by silk_show.
class SilkScene : public Scene {
public:
    SilkScene(const styles::SilkStyle& style, ProgramLoader loader, const GlProfile* profile, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), profile_(profile), host_(std::move(host)) {}
    ~SilkScene() override { release(); }

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Fluid; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeFeatureFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void acceptPcm(const float* samples, int count) override { pcmPulse_.accept(samples, count); }
    void setTouchField(const TouchField* field) override { touch_ = field; }

private:
    static constexpr int kSimRes = 320;
    static constexpr float kByteStateScale = 8.0f;
    static constexpr float kSeedEpochSeconds = 9.0f;
    static constexpr float kEnvRisePerSec = 8.0f;
    static constexpr float kEnvFallPerSec = 2.2f;
    static constexpr float kRingSpeed = 2.6f;
    static constexpr float kRingMax = 3.4f;
    static constexpr float kBeatThreshold = 0.28f;

    void applySimSize();
    void bindStep(sim::SimUniforms& u);

    std::string id_;
    styles::SilkStyle style_;
    ProgramLoader loader_;
    const GlProfile* profile_;
    SceneHost host_;
    SceneParams params_;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    std::unique_ptr<sim::SimPass> sim_;
    GLuint showProgram_ = 0;
    UniformCache showLocs_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float envBass_ = 0.0f;
    float envMid_ = 0.0f;
    float envTreble_ = 0.0f;
    float beatPulse_ = 0.0f;
    float ringRadius_ = -1.0f;
    float slabTurn_ = 0.0f;
    float foldPhase_ = 0.0f;
    float drift_ = 0.0f;
    float stepB_ = 0.0f;
    float stepAdvect_ = 0.0f;
    float stepDecay_ = 0.0f;
    float stepDrive_ = 0.0f;
    float stepSeedEpoch_ = 0.0f;
    const TouchField* touch_ = nullptr;
};

}  // namespace geode::viz
