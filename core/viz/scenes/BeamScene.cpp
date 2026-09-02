#include "viz/scenes/BeamScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"
#include "viz/scenes/SceneCommon.hpp"

namespace geode::viz {

void BeamScene::init() {
    program_ = 0;
    vao_ = 0;
    waveTex_ = 0;
    uniforms_ = UniformCache(0);
    programOk_ = false;
    std::string error;
    program_ = loader_.build("beam_vert.glsl", "beam_frag.glsl", &error);
    if (program_ == 0) {
        host_.onShaderError("Beam unavailable on this GPU: " + error);
        return;
    }
    programOk_ = true;
    uniforms_ = UniformCache(program_);
    glGenVertexArrays(1, &vao_);
    glGenTextures(1, &waveTex_);
    glBindTexture(GL_TEXTURE_2D, waveTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kSamples, 1, 0, GL_RED, GL_FLOAT, nullptr);
}

void BeamScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
}

void BeamScene::acceptPcm(const float* samples, int count) {
    const int n = std::min(count, static_cast<int>(pcm_.size()));
    if (n <= 0) return;
    std::copy(samples + (count - n), samples + count, pcm_.begin());
    pcmCount_ = n;
}

void BeamScene::update(const GeodeFeatureFrame& features, float dt) {
    if (pcmCount_ > 0) {
        fillPcmRow(samples_.data(), kSamples, pcm_.data(), pcmCount_);
        pcmCount_ = 0;
    } else {
        fillPcmRow(samples_.data(), kSamples, features.waveform, GEODE_WAVEFORM_POINTS);
    }
    float peak = 0.0f;
    for (float s : samples_) peak = std::max(peak, std::fabs(s));
    const float target = peak > 0.02f ? std::clamp(0.85f / peak, 0.5f, 6.0f) : autoGain_;
    autoGain_ += (target - autoGain_) * (target < autoGain_ ? 0.06f : 0.02f);
    beatPulse_ = std::clamp(std::max(live::hit(features), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
}

void BeamScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    const SceneParams& p = params_;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, waveTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSamples, 1, GL_RED, GL_FLOAT, samples_.data());

    glUseProgram(program_);
    glUniform1i(loc("uWave"), 0);
    glUniform1i(loc("uCount"), kSamples - 1);
    glUniform1f(loc("uMode"), p.beamXy ? 1.0f : 0.0f);
    glUniform1i(loc("uPhaseOffset"), kQuadrature);
    glUniform1f(loc("uAspect"), static_cast<float>(width_) / static_cast<float>(height_));
    glUniform1f(loc("uSigma"), kBaseSigma * std::clamp(p.beamWidth, 0.2f, 4.0f));
    glUniform1f(loc("uGain"), kBaseGain * std::clamp(p.audioDrive, 0.0f, 4.0f) * autoGain_);
    glUniform1f(loc("uTail"), std::clamp(p.beamTail, 0.0f, 1.0f));
    glUniform1f(loc("uIntensity"), std::clamp(p.beamIntensity, 0.0f, 3.0f) * (1.0f + beatPulse_ * std::clamp(p.beatResponse, 0.0f, 2.0f) * 0.4f));
    hue::rgb(hue::base(p.paletteBase()), 1.0f, beamRgb_.data());
    glUniform3f(loc("uColor"), beamRgb_[0], beamRgb_[1], beamRgb_[2]);
    uploadSceneTouch(uniforms_, touch_);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, (kSamples - 1) * 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

float BeamScene::trailRetention(const SceneParams& params) const {
    return std::clamp(0.55f + 0.44f * params.trailLength, 0.0f, 0.99f);
}

void BeamScene::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (waveTex_ != 0) glDeleteTextures(1, &waveTex_);
    program_ = 0;
    vao_ = 0;
    waveTex_ = 0;
    programOk_ = false;
    uniforms_ = UniformCache(0);
}

}  // namespace geode::viz
