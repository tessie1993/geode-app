#pragma once
#include <GLES3/gl3.h>

#include <optional>
#include <string>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of MycoScene.kt: physarum agents depositing into a blurred trail field.
class MycoScene : public Scene {
public:
    MycoScene(const styles::MycoStyle& style, ProgramLoader loader, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), host_(std::move(host)) {}
    ~MycoScene() override { release(); }

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
    static constexpr int kTrailRes = 384;
    static constexpr float kBeatThreshold = 0.3f;
    static constexpr float kEnvRisePerSec = 9.0f;
    static constexpr float kEnvFallPerSec = 2.4f;
    static constexpr float kByteFallbackDeposit = 0.125f;
    static constexpr float kTouchBirthPerSecond = 0.35f;
    static constexpr float kTouchBirthPerFrameCap = 0.02f;

    bool ensureBuffers();

    std::string id_;
    styles::MycoStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    SceneParams params_;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    UniformCache agent_{0};
    UniformCache deposit_{0};
    UniformCache blur_{0};
    UniformCache show_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    std::optional<fluid::Formats> formats_;
    std::optional<fluid::DoubleFbo> agents_;
    std::optional<fluid::DoubleFbo> trail_;
    bool byteTrail_ = false;
    bool agentsSeeded_ = false;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float envBass_ = 0.0f;
    float envTreble_ = 0.0f;
    float beatPulse_ = 0.0f;
    float reaim_ = 0.0f;
    const TouchField* touch_ = nullptr;
};

}  // namespace geode::viz
