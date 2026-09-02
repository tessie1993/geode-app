#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>
#include <string>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of AcidScene.kt: a video-feedback loop on an RGBA8 ping-pong.
class AcidScene : public Scene {
public:
    AcidScene(const styles::AcidStyle& style, ProgramLoader loader, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), host_(std::move(host)) {}
    ~AcidScene() override { release(); }

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
    static constexpr int kSimRes = 540;
    static constexpr float kFeedbackCap = 0.975f;
    static constexpr float kGlitchThreshold = 0.32f;
    static constexpr float kGlitchDecay = 2.4f;
    static constexpr float kEnvRisePerSec = 9.0f;
    static constexpr float kEnvFallPerSec = 2.4f;
    static constexpr int kSpokes = 12;

    fluid::DoubleFbo* ensureState();
    void fillSpokes(const float* bands);

    std::string id_;
    styles::AcidStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    SceneParams params_;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    UniformCache step_{0};
    UniformCache show_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    std::optional<fluid::DoubleFbo> state_;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float envBass_ = 0.0f;
    float envMid_ = 0.0f;
    float envTreble_ = 0.0f;
    float beatPulse_ = 0.0f;
    float glitch_ = 0.0f;
    float glitchEpoch_ = 0.0f;
    const TouchField* touch_ = nullptr;
    std::array<float, kSpokes> spokes_{};
};

}  // namespace geode::viz
