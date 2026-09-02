#include "viz/scenes/LifeScene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
constexpr int kCensusProbes[5][2] = {{1, 2}, {3, 2}, {2, 1}, {2, 3}, {2, 2}};
}

void LifeScene::init() {
    stepProgram_ = 0;
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    formats_.reset();
    state_.reset();
    std::string error;
    const std::string quad = loader_.source("quad_vert.glsl", &error);
    stepProgram_ = loader_.buildSource(quad, loader_.source("life_step_frag.glsl", &error), &error);
    if (stepProgram_ == 0) {
        host_.onShaderError("Life unavailable on this GPU: " + error);
        return;
    }
    showProgram_ = loader_.buildSource(quad, loader_.source("life_show_frag.glsl", &error), &error);
    if (showProgram_ == 0) {
        host_.onShaderError("Life unavailable on this GPU: " + error);
        return;
    }
    stepLocs_ = UniformCache(stepProgram_);
    showLocs_ = UniformCache(showProgram_);
    glGenVertexArrays(1, &vao_);
    programOk_ = true;
    seedRemain_ = kSeedSeconds;
}

void LifeScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (state_) state_->release();
    state_.reset();
    seedRemain_ = kSeedSeconds;
}

void LifeScene::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
}

fluid::DoubleFbo* LifeScene::ensureState() {
    if (state_) return &*state_;
    if (!formats_) formats_ = fluid::probeFormats();
    byteState_ = !formats_->ok;
    const fluid::TexFormat texFmt = byteState_ ? fluid::kRgba8 : formats_->rgba;
    const auto [w, h] = fluid::resolution(kSimRes, width_, height_);
    state_.emplace(w, h, texFmt, true);
    state_->create();
    if (state_->ok()) return &*state_;
    state_->release();
    state_.reset();
    return nullptr;
}

void LifeScene::census(fluid::DoubleFbo& field, GLint restoreFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, field.read().fbo());
    float maxA = 0.0f;
    float maxV = 0.0f;
    float minLive = std::numeric_limits<float>::max();
    bool sane = true;
    for (const auto& probe : kCensusProbes) {
        const int x = field.width() * probe[0] / 4;
        const int y = field.height() * probe[1] / 4;
        float a = 0.0f;
        float v = 0.0f;
        if (byteState_) {
            unsigned char bytes[4] = {0, 0, 0, 0};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
            a = static_cast<float>(bytes[0]) / 255.0f;
            v = static_cast<float>(bytes[1]) / 255.0f;
        } else {
            float texel[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, texel);
            a = texel[0];
            v = texel[1];
        }
        sane = sane && std::isfinite(a) && std::isfinite(v);
        maxA = std::max(maxA, a);
        maxV = std::max(maxV, v);
        minLive = std::min(minLive, style_.rule == 0 ? a : v);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(restoreFbo));
    if (!sane) return;
    const float maxLive = style_.rule == 0 ? maxA : maxV;
    const bool starving = maxLive < kStarved && (style_.rule == 1 ? maxA > 0.9f : true);
    if (starving || minLive > kOvergrown) seedRemain_ = kSeedSeconds;
}

void LifeScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    fluid::DoubleFbo* field = ensureState();
    if (!field) return;
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeFeatureFrame f = hasPending_ ? pending_ : GeodeFeatureFrame{};
    hasPending_ = false;

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float drive = safeAudioDrive(p.audioDrive);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(f.treble), dt, kEnvRisePerSec, kEnvFallPerSec);
    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * std::clamp(p.beatResponse, 0.0f, 2.0f), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
    kick_ = std::max(kick_ - dt * 5.0f, 0.0f);
    if (hit * p.beatResponse > kBeatThreshold) {
        kick_ = (0.4f + 0.6f * std::clamp(hit, 0.0f, 1.5f)) * drive;
        kickAngle_ += kGoldenAngle;
        kickX_ = 0.5f + 0.32f * std::cos(kickAngle_);
        kickY_ = 0.5f + 0.32f * std::sin(kickAngle_);
    }
    seedRemain_ = std::max(seedRemain_ - dt, 0.0f);
    censusAge_ += dt;
    if (censusAge_ >= kCensusSeconds) {
        censusAge_ = 0.0f;
        census(*field, prevFbo);
    }

    const int substeps = std::clamp(static_cast<int>(std::lround(style_.substeps * speed)), 1, 8);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);

    glUseProgram(stepProgram_);
    glViewport(0, 0, field->width(), field->height());
    glUniform2f(stepLocs_.loc("uRes"), static_cast<float>(field->width()), static_cast<float>(field->height()));
    glUniform1i(stepLocs_.loc("uRule"), style_.rule);
    glUniform1f(stepLocs_.loc("uDt"), style_.dt);
    glUniform1i(stepLocs_.loc("uCore"), style_.core);
    glUniform1i(stepLocs_.loc("uGrowth"), style_.growth);
    glUniform1f(stepLocs_.loc("uMu"), style_.mu);
    glUniform1f(stepLocs_.loc("uSigma"), style_.sigma);
    glUniform1f(stepLocs_.loc("uRadius"), style_.radius);
    glUniform1i(stepLocs_.loc("uRings"), style_.rings);
    glUniform3f(stepLocs_.loc("uB"), style_.b1, style_.b2, style_.b3);
    glUniform1f(stepLocs_.loc("uF"), style_.feed);
    glUniform1f(stepLocs_.loc("uK"), style_.kill);
    glUniform2f(stepLocs_.loc("uDiff"), 1.0f, 0.5f);
    glUniform1f(stepLocs_.loc("uAniso"), style_.aniso);
    glUniform1f(stepLocs_.loc("uSeedJitter"), style_.seedJitter);
    glUniform1f(stepLocs_.loc("uTime"), time_);
    for (int pass = 0; pass < substeps; ++pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, field->write().fbo());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, field->read().tex());
        glUniform1i(stepLocs_.loc("uPrev"), 0);
        const bool first = pass == 0;
        glUniform1f(stepLocs_.loc("uSeed"), first ? seedRemain_ / kSeedSeconds : 0.0f);
        glUniform1f(stepLocs_.loc("uKick"), first ? kick_ : 0.0f);
        glUniform2f(stepLocs_.loc("uKickPos"), kickX_, kickY_);
        glUniform1f(stepLocs_.loc("uSprinkle"), first ? (envTreble_ + pcmStrike_ * 0.5f) * drive : 0.0f);
        // Injections land once per frame, not once per substep.
        uploadSceneTouch(stepLocs_, touch_, first);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        field->swap();
    }
    kick_ = 0.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(showProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, field->read().tex());
    glUniform1i(showLocs_.loc("uState"), 0);
    glUniform2f(showLocs_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform2f(showLocs_.loc("uSimRes"), static_cast<float>(field->width()), static_cast<float>(field->height()));
    glUniform1i(showLocs_.loc("uLook"), style_.look);
    glUniform1f(showLocs_.loc("uShowV"), style_.rule == 1 ? 1.0f : 0.0f);
    glUniform1f(showLocs_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(showLocs_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(showLocs_.loc("uEnergy"), clampedBand(f.rms));
    glUniform1f(showLocs_.loc("uBeat"), beatPulse_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void LifeScene::release() {
    if (stepProgram_ != 0) glDeleteProgram(stepProgram_);
    if (showProgram_ != 0) glDeleteProgram(showProgram_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (state_) state_->release();
    state_.reset();
    formats_.reset();
    stepProgram_ = 0;
    showProgram_ = 0;
    vao_ = 0;
    programOk_ = false;
    stepLocs_ = UniformCache(0);
    showLocs_ = UniformCache(0);
}

}  // namespace geode::viz
