#include "viz/scenes/MycoScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

void MycoScene::init() {
    for (UniformCache* c : {&agent_, &deposit_, &blur_, &show_}) *c = UniformCache(0);
    vao_ = 0;
    programOk_ = false;
    formats_.reset();
    agents_.reset();
    trail_.reset();
    agentsSeeded_ = false;
    std::string error;
    const std::string quad = loader_.source("quad_vert.glsl", &error);
    const auto fail = [&](const char* what) { host_.onShaderError(std::string("Mycelium ") + what + " unavailable: " + error); };
    const GLuint agent = loader_.buildSource(quad, loader_.source("myco_agent_frag.glsl", &error), &error);
    if (agent == 0) fail("agents");
    const GLuint deposit = loader_.build("myco_deposit_vert.glsl", "myco_deposit_frag.glsl", &error);
    if (deposit == 0) fail("deposit");
    const GLuint blur = loader_.buildSource(quad, loader_.source("myco_blur_frag.glsl", &error), &error);
    if (blur == 0) fail("trail");
    const GLuint show = loader_.buildSource(quad, loader_.source("myco_show_frag.glsl", &error), &error);
    if (show == 0) fail("present");
    agent_ = UniformCache(agent);
    deposit_ = UniformCache(deposit);
    blur_ = UniformCache(blur);
    show_ = UniformCache(show);
    if (agent == 0 || deposit == 0 || blur == 0 || show == 0) return;
    glGenVertexArrays(1, &vao_);
    programOk_ = true;
}

void MycoScene::resize(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    if (trail_) trail_->release();
    trail_.reset();
}

void MycoScene::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, kSceneTimeWrapSeconds);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
}

bool MycoScene::ensureBuffers() {
    if (!formats_) formats_ = fluid::probeFormats();
    const fluid::Formats& fmt = *formats_;
    if (!agents_) {
        const fluid::TexFormat agentFmt = fmt.hasRgba32 ? fmt.rgba32 : (fmt.ok ? fmt.rgba : fluid::kRgba8);
        agents_.emplace(style_.agentRes, style_.agentRes, agentFmt, false);
        agents_->create();
        if (agents_->ok()) {
            agentsSeeded_ = false;
        } else {
            agents_->release();
            agents_.reset();
        }
    }
    if (agents_ && !trail_) {
        const auto [w, h] = fluid::resolution(kTrailRes, width_, height_);
        byteTrail_ = !fmt.ok;
        trail_.emplace(w, h, byteTrail_ ? fluid::kRgba8 : fmt.rg, true);
        trail_->create();
        if (!trail_->ok()) {
            trail_->release();
            trail_.reset();
        }
    }
    return agents_.has_value() && trail_.has_value();
}

void MycoScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!programOk_) return;
    resetFrameState();
    GLint prevFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    if (!ensureBuffers()) return;
    fluid::DoubleFbo& colony = *agents_;
    fluid::DoubleFbo& field = *trail_;
    const SceneParams& p = params_;
    const float dt = std::clamp(lastDt_, 0.0f, 1.0f / 15.0f);
    const GeodeFeatureFrame f = hasPending_ ? pending_ : GeodeFeatureFrame{};
    hasPending_ = false;

    const float speed = std::clamp(p.speed, 0.05f, 4.0f);
    const float drive = safeAudioDrive(p.audioDrive);
    envBass_ = slewEnvelope(envBass_, clampedBand(f.bass), dt, kEnvRisePerSec, kEnvFallPerSec);
    envTreble_ = slewEnvelope(envTreble_, clampedBand(f.treble), dt, kEnvRisePerSec, kEnvFallPerSec);
    const float hit = live::hit(f);
    beatPulse_ = std::clamp(std::max(hit * std::clamp(p.beatResponse, 0.0f, 2.0f), beatPulse_ - dt * 3.0f), 0.0f, 1.5f);
    reaim_ = hit * p.beatResponse > kBeatThreshold ? style_.reaim : 0.0f;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vao_);

    glUseProgram(agent_.program());
    glBindFramebuffer(GL_FRAMEBUFFER, colony.write().fbo());
    glViewport(0, 0, colony.width(), colony.height());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colony.read().tex());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, field.read().tex());
    glUniform1i(agent_.loc("uAgents"), 0);
    glUniform1i(agent_.loc("uTrail"), 1);
    glUniform2f(agent_.loc("uTrailRes"), static_cast<float>(field.width()), static_cast<float>(field.height()));
    glUniform1f(agent_.loc("uInit"), agentsSeeded_ ? 0.0f : 1.0f);
    glUniform1f(agent_.loc("uSpeciesMix"), style_.speciesMix);
    glUniform1f(agent_.loc("uSensorDist"), style_.sensorDist);
    glUniform1f(agent_.loc("uSensorAngle"), style_.sensorAngle);
    glUniform1f(agent_.loc("uTurnAngle"), style_.turnAngle);
    glUniform1f(agent_.loc("uMoveStep"), style_.moveStep * speed * (1.0f + 0.5f * envBass_ * drive));
    glUniform4f(agent_.loc("uMatrix"), style_.selfA, style_.crossAb, style_.crossBa, style_.selfB);
    glUniform1f(agent_.loc("uBreath"), beatPulse_ * drive);
    glUniform1f(agent_.loc("uJitter"), style_.jitter + 0.35f * envTreble_ * drive + std::clamp(p.turbulence, 0.0f, 1.0f) * 0.5f);
    glUniform1f(agent_.loc("uSnap"), style_.snap);
    glUniform1f(agent_.loc("uReaim"), reaim_);
    glUniform1f(agent_.loc("uTime"), time_);
    glUniform1f(agent_.loc("uAniso"), style_.aniso);
    // A rate integrated over dt, so a gesture buds the same at 60 and 120 Hz.
    const float birthRate = std::min(kTouchBirthPerSecond * dt, kTouchBirthPerFrameCap);
    glUniform1f(agent_.loc("uTouchBirth"), birthRate * (touch_ ? touch_->anchorStrength() : 0.0f));
    uploadSceneTouch(agent_, touch_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    colony.swap();
    agentsSeeded_ = true;

    glUseProgram(deposit_.program());
    glBindFramebuffer(GL_FRAMEBUFFER, field.read().fbo());
    glViewport(0, 0, field.width(), field.height());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colony.read().tex());
    glUniform1i(deposit_.loc("uAgents"), 0);
    glUniform2f(deposit_.loc("uAgentRes"), static_cast<float>(colony.width()), static_cast<float>(colony.height()));
    glUniform1f(deposit_.loc("uDeposit"), style_.deposit * (byteTrail_ ? kByteFallbackDeposit : 1.0f));
    glDrawArrays(GL_POINTS, 0, colony.width() * colony.height());
    glDisable(GL_BLEND);

    glUseProgram(blur_.program());
    glBindFramebuffer(GL_FRAMEBUFFER, field.write().fbo());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, field.read().tex());
    glUniform1i(blur_.loc("uTrail"), 0);
    glUniform2f(blur_.loc("uTrailRes"), static_cast<float>(field.width()), static_cast<float>(field.height()));
    glUniform1f(blur_.loc("uDecay"), std::pow(style_.decay, dt * 60.0f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    field.swap();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(show_.program());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, field.read().tex());
    glUniform1i(show_.loc("uTrail"), 0);
    glUniform2f(show_.loc("uRes"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform2f(show_.loc("uTrailRes"), static_cast<float>(field.width()), static_cast<float>(field.height()));
    glUniform1i(show_.loc("uLook"), style_.look);
    glUniform1f(show_.loc("uBaseHue"), hue::base(p.paletteBase()) + style_.hueOffset);
    glUniform1f(show_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()) * style_.hueSpan);
    glUniform1f(show_.loc("uExposure"), style_.exposure * (byteTrail_ ? 1.0f / kByteFallbackDeposit : 1.0f));
    glUniform1f(show_.loc("uEnergy"), clampedBand(f.rms));
    glUniform1f(show_.loc("uBeat"), beatPulse_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void MycoScene::release() {
    for (UniformCache* c : {&agent_, &deposit_, &blur_, &show_}) {
        if (c->program() != 0) glDeleteProgram(c->program());
        *c = UniformCache(0);
    }
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    if (agents_) agents_->release();
    if (trail_) trail_->release();
    agents_.reset();
    trail_.reset();
    formats_.reset();
    vao_ = 0;
    programOk_ = false;
}

}  // namespace geode::viz
