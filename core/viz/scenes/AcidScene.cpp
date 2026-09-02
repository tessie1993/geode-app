#include "viz/scenes/AcidScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

void AcidScene::init() {
    step_ = UniformCache(0);
    show_ = UniformCache(0);
    vao_ = 0;
    programOk_ = false;
    state_.reset();
    std::string error;
    const std::string quad = loader_.source("quad_vert.glsl", &error);
    const GLuint step = loader_.buildSource(quad, loader_.source("acid_step_frag.glsl", &error), &error);
    if (step == 0) {
        host_.onShaderError("Acid unavailable on this GPU: " + error);
        return;
    }
    step_ = UniformCache(step);
    const GLuint show = loader_.buildSource(quad, loader_.source("acid_show_frag.glsl", &error), &error);
    if (show == 0) {
        host_.onShaderError("Acid unavailable on this GPU: " + error);
        return;
    }
    show_ = UniformCache(show);
    glGenVertexArrays(1, &vao_);
    programOk_ = true;
}

void AcidScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (state_) state_->release();
    state_.reset();
}

void AcidScene::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
}

fluid::DoubleFbo* AcidScene::ensureState() {
    if (state_) return &*state_;
    const auto [w, h] = fluid::resolution(kSimRes, width_, height_);
    state_.emplace(w, h, fluid::kRgba8, true);
    state_->create();
    if (state_->ok()) return &*state_;
    state_->release();
    state_.reset();
    return nullptr;
}

void AcidScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    fluid::DoubleFbo* loop = ensureState();
    if (!loop) return;
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeFeatureFrame f = hasPending_ ? pending_ : GeodeFeatureFrame{};
    hasPending_ = false;

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    envBass_ = slewEnvelope(envBass_, clampedBand(f.bass), dt, kEnvRisePerSec, kEnvFallPerSec);
    envMid_ = slewEnvelope(envMid_, clampedBand(f.mid), dt, kEnvRisePerSec, kEnvFallPerSec);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(f.treble), dt, kEnvRisePerSec, kEnvFallPerSec);
    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * std::clamp(p.beatResponse, 0.0f, 2.0f), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
    if (hit * p.beatResponse > kGlitchThreshold) {
        glitch_ = 1.0f;
        glitchEpoch_ = std::fmod(glitchEpoch_ + 1.0f, 1024.0f);
    }
    glitch_ = std::max(glitch_ - dt * kGlitchDecay, 0.0f);
    fillSpokes(f.bands);

    const float frames = dt * 60.0f;
    const float feedback = std::pow(std::min(style_.feedback, kFeedbackCap), frames);
    const float zoom = std::pow(style_.zoom, frames);
    const float rotate = style_.rotate * frames * speed;
    const float hueShift = style_.hueRate * dt * speed;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);

    glUseProgram(step_.program());
    glBindFramebuffer(GL_FRAMEBUFFER, loop->write().fbo());
    glViewport(0, 0, loop->width(), loop->height());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, loop->read().tex());
    glUniform1i(step_.loc("uPrev"), 0);
    glUniform2f(step_.loc("uRes"), static_cast<float>(loop->width()), static_cast<float>(loop->height()));
    glUniform1i(step_.loc("uStyle"), style_.mode);
    glUniform1i(step_.loc("uSource"), style_.source);
    glUniform1f(step_.loc("uZoom"), zoom);
    glUniform1f(step_.loc("uRotate"), rotate);
    glUniform1f(step_.loc("uHueShift"), hueShift);
    glUniform1f(step_.loc("uFeedback"), feedback);
    glUniform1f(step_.loc("uModulate"), style_.modulate);
    glUniform1f(step_.loc("uGlitch"), glitch_ * style_.glitch);
    glUniform1f(step_.loc("uEpoch"), glitchEpoch_);
    glUniform1f(step_.loc("uTime"), time_);
    glUniform1f(step_.loc("uBass"), envBass_);
    glUniform1f(step_.loc("uMid"), envMid_);
    glUniform1f(step_.loc("uTreble"), envTreble_);
    glUniform1f(step_.loc("uBeat"), beatPulse_);
    glUniform1f(step_.loc("uStrike"), std::clamp(pcmStrike_, 0.0f, 1.5f));
    glUniform1f(step_.loc("uDrive"), safeAudioDrive(p.audioDrive));
    glUniform1fv(step_.loc("uSpokes"), kSpokes, spokes_.data());
    glUniform1f(step_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(step_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(step_.loc("uLiquid"), style_.liquid + std::clamp(p.turbulence, 0.0f, 1.0f) * 0.6f);
    uploadSceneTouch(step_, touch_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    loop->swap();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(show_.program());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, loop->read().tex());
    glUniform1i(show_.loc("uState"), 0);
    glUniform2f(show_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1f(show_.loc("uScanline"), style_.scanline);
    glUniform1f(show_.loc("uCurve"), style_.curve);
    glUniform1f(show_.loc("uSat"), style_.saturation);
    glUniform1f(show_.loc("uFloorHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(show_.loc("uOverdrive"), style_.overdrive);
    glUniform1f(show_.loc("uHit"), std::clamp(pcmStrike_ + 0.5f * beatPulse_, 0.0f, 1.0f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void AcidScene::fillSpokes(const float* bands) {
    for (int i = 0; i < kSpokes; ++i) {
        const int from = i * GEODE_BAND_COUNT / kSpokes;
        const int to = std::max(std::min((i + 1) * GEODE_BAND_COUNT / kSpokes, GEODE_BAND_COUNT), from + 1);
        float acc = 0.0f;
        for (int b = from; b < to; ++b) acc += bands[b];
        spokes_[static_cast<size_t>(i)] = std::clamp(acc / static_cast<float>(to - from), 0.0f, 1.0f);
    }
}

void AcidScene::release() {
    for (UniformCache* c : {&step_, &show_}) {
        if (c->program() != 0) glDeleteProgram(c->program());
        *c = UniformCache(0);
    }
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (state_) state_->release();
    state_.reset();
    vao_ = 0;
    programOk_ = false;
}

}  // namespace geode::viz
