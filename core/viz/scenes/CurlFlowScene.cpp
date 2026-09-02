#include "viz/scenes/CurlFlowScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/fluid/FluidMath.hpp"

namespace geode::viz {

void CurlFlowScene::init() {
    release();
    formats_ = fluid::probeFormats();
    available_ = formats_.ok;
    if (!available_) {
        host_.onShaderError("Curl Flow unavailable: this GPU can't render half-float buffers");
        return;
    }
    choreography_.reset();
    quad_.create();
    std::string error;
    const GLuint program = loader_.build("fluid_base_vert.glsl", "curl_field_frag.glsl", &error);
    if (program == 0) {
        host_.onShaderError("Curl Flow unavailable on this GPU: " + error);
        release();
        return;
    }
    fieldUniforms_ = UniformCache(program);
    particles_.create(49152, formats_);
    if (!particles_.available()) {
        host_.onShaderError("Curl Flow unavailable: this GPU refused the particle state buffers");
        release();
    }
}

void CurlFlowScene::resize(int width, int height) {
    if (!available_) return;
    aspect_ = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
    if (field_) field_->release();
    const auto [fw, fh] = fluid::resolution(96, width, height);
    field_.emplace(fw, fh, formats_.rg, true);
    field_->create();
    if (!field_->ok()) {
        field_->release();
        field_.reset();
        host_.onShaderError("Curl Flow unavailable: this GPU refused the flow-field buffer");
    }
    particles_.invalidateSeed();
}

void CurlFlowScene::update(const GeodeFeatureFrame& features, float dt) {
    pending_ = features;
    hasPending_ = true;
    lastDt_ = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    pcmKick_ = std::clamp(tickPcm(dt), 0.0f, 1.0f);
}

void CurlFlowScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!available_ || !field_) return;
    const fluid::Fbo& fld = *field_;
    saveFramebufferAndViewport();

    if (hasPending_) {
        const GeodeFeatureFrame& f = pending_;
        wallTime_ = std::fmod(wallTime_ + lastDt_, kWallWrapSeconds);
        beatEnv_ = std::max(live::hit(f), beatEnv_ * std::exp(-lastDt_ / 0.35f));
        beatDrive_ = fluid::curl::beatDrive(beatEnv_, params_.beatResponse);
        noiseTime_ = std::fmod(noiseTime_ + lastDt_ * (0.15f + f.mid * 1.4f) * fluid::Choreography::sceneSpeed(params_.speed), kNoiseWrapSeconds);

        configureChoreography();
        choreography_.tick(f, lastDt_, aspect_);

        glDisable(GL_BLEND);
        quad_.bind();
        glUseProgram(fieldUniforms_.program());
        glUniform2f(fieldUniforms_.loc("uInvRes"), 1.0f / static_cast<float>(fld.width()), 1.0f / static_cast<float>(fld.height()));
        glUniform1f(fieldUniforms_.loc("uAspect"), aspect_);
        glUniform1f(fieldUniforms_.loc("uTime"), noiseTime_);
        glUniform1f(fieldUniforms_.loc("uFreq"), 1.2f * (0.5f + std::clamp(params_.turbulence, 0.1f, 2.0f)));
        glUniform1f(fieldUniforms_.loc("uDetail"), std::clamp(f.treble * 3.0f + pcmKick_ * 0.8f, 0.0f, 1.5f));
        glUniform1f(fieldUniforms_.loc("uAmp"), fluid::curl::fieldAmp(params_.audioDrive, beatDrive_) * (1.0f + pcmKick_ * 0.35f));
        glUniform2f(fieldUniforms_.loc("uPeriod"), 0.0f, 0.0f);
        glBindFramebuffer(GL_FRAMEBUFFER, fld.fbo());
        // The field is rebuilt from noise every audio frame, so nothing carries over: discard, then fill.
        fld.discardContents();
        glViewport(0, 0, fld.width(), fld.height());
        glDrawArrays(GL_TRIANGLES, 0, 3);
        quad_.unbind();

        applyChoreographyTo(particles_);
        particles_.step(lastDt_, fld.tex(), aspect_, 1.0f, wallTime_);
        hasPending_ = false;
    }

    restoreFramebufferAndViewport();
    particles_.draw(aspect_, std::clamp(params_.particleSize, 0.4f, 4.0f) * viewportDpiScale(), params_.paletteBase(),
                    hue::span(params_.hueRange, params_.paletteRange()), fluid::curl::particleBrightness(beatDrive_),
                    static_cast<float>(params_.particleShape), particle_look::glow(params_.bloom), wallTime_);
}

float CurlFlowScene::trailRetention(const SceneParams& params) const { return fluid::curl::retention(params.trailLength, params.trails); }

void CurlFlowScene::release() {
    particles_.release();
    if (field_) field_->release();
    field_.reset();
    if (fieldUniforms_.program() != 0) glDeleteProgram(fieldUniforms_.program());
    fieldUniforms_ = UniformCache(0);
    quad_.release();
    available_ = false;
}

}  // namespace geode::viz
