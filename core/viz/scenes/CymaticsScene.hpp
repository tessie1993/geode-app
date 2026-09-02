#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/scenes/CymaticsMath.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of CymaticsScene.kt: a resonant plate driven by the spectrum, drawn by cymatics_field_frag.
class CymaticsScene : public Scene {
public:
    CymaticsScene(const styles::CymaticsStyle& style, ProgramLoader loader, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), host_(std::move(host)) {}
    ~CymaticsScene() override { release(); }

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
    static constexpr float kDriveGain = 1.5f;
    static constexpr float kIdleRms = 0.015f;
    static constexpr float kIdleFadeSeconds = 1.2f;
    static constexpr float kIdleSweepHz = 0.035f;
    static constexpr float kMinColorAmplitude = 0.12f;
    static constexpr float kExposure = 1.6f;
    static constexpr float kDriftWrap = 2.0f;
    static constexpr float kTravelOmega = 1.1f;
    static constexpr float kDriftRate = 0.05f;
    static constexpr int kStyleFaraday = 4;
    static constexpr float kToneTauSeconds = 2.5f;
    static constexpr float kToneHueSpan = 0.05f;
    static constexpr float kPcmStrikeGain = 0.6f;
    static constexpr float kMinTouchK = 5.0f;
    static constexpr float kMaxTouchK = 26.0f;

    const float* driveSpectrum(const GeodeFeatureFrame& f, float dt);
    GLint loc(const char* name) { return uniforms_.loc(name); }

    std::string id_;
    styles::CymaticsStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    cymatics::Plate plate_;
    cymatics::Drops drops_;
    std::array<float, cymatics::kMaxRenderedModes * 4> modes_{};
    int modeCount_ = 0;
    SceneParams params_;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    int width_ = 1;
    int height_ = 1;
    GLuint program_ = 0;
    UniformCache uniforms_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    float beatPulse_ = 0.0f;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float swirlPhase_ = 0.0f;
    float travelPhase_ = 0.0f;
    float driftShift_ = 0.0f;
    float toneHue_ = 0.0f;
    float idleBlend_ = 0.0f;
    float idlePhase_ = 0.0f;
    const TouchField* touch_ = nullptr;
    float touchPhase_ = 0.0f;
    std::array<float, GEODE_BAND_COUNT> idleBands_{};
    std::array<float, GEODE_BAND_COUNT> driveBands_{};
};

}  // namespace geode::viz
