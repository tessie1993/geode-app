#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// Port of BeamScene.kt: the oscilloscope beam, drawn as additive segments from a waveform texture.
class BeamScene : public Scene {
public:
    static constexpr int kSamples = 512;

    BeamScene(ProgramLoader loader, SceneHost host) : loader_(loader), host_(std::move(host)), pcm_(kSamples * 8, 0.0f) {}
    ~BeamScene() override { release(); }

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Fluid; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeFeatureFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void acceptPcm(const float* samples, int count) override;
    void setTouchField(const TouchField* field) override { touch_ = field; }
    float trailRetention(const SceneParams& params) const override;

private:
    static constexpr float kBaseSigma = 0.006f;
    static constexpr int kQuadrature = kSamples / 4;
    static constexpr float kBaseGain = 0.8f;

    GLint loc(const char* name) { return uniforms_.loc(name); }

    std::string id_ = "beam";
    ProgramLoader loader_;
    SceneHost host_;
    GLuint program_ = 0;
    UniformCache uniforms_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    GLuint waveTex_ = 0;
    SceneParams params_;
    int width_ = 1;
    int height_ = 1;
    std::array<float, kSamples> samples_{};
    std::vector<float> pcm_;
    int pcmCount_ = 0;
    std::array<float, 3> beamRgb_{};
    float autoGain_ = 1.0f;
    float beatPulse_ = 0.0f;
    const TouchField* touch_ = nullptr;
};

}  // namespace geode::viz
