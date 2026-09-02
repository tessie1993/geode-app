#include "viz/scenes/WaterScene.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"
#include "viz/fluid/FluidMath.hpp"

namespace geode::viz {

void WaterScene::init() {
    quad_.forget();
    sim_.onShaderError = [this](const std::string& m) { host_.onShaderError(m); };
    sim_.inkEnabled = true;
    sim_.create();
    choreography_.reset();
    appliedTier_ = -1;
    lastUserQuality_ = -1;
    autoDowngrade_ = 0;
    displayOk_ = false;
    if (!sim_.available()) {
        host_.onShaderError("Water style unavailable: this GPU can't render half-float buffers");
        return;
    }
    std::string error;
    const GLuint program = loader_.build("fluid_base_vert.glsl", "water_display_frag.glsl", &error);
    if (program == 0) {
        GEODE_LOGW("RippleSim", "water display shader rejected by driver: %s", error.c_str());
        host_.onShaderError("Water display unavailable on this GPU: " + error);
    }
    display_ = UniformCache(program);
    displayOk_ = program != 0;
    quad_.create();
    applyQualityTier();
}

int WaterScene::gridResFor(int tierIndex) {
    switch (tierIndex) {
        case 0: return 512;
        case 1: return 448;
        case 2: return 384;
        case 3: return 288;
        default: return 192;
    }
}

void WaterScene::onApplyQualityTier(int index, bool userChanged) {
    (void) userChanged;
    sim_.applyResolution(gridResFor(index));
}

GeodeFeatureFrame WaterScene::idleFeatures(float dt) {
    idlePhase_ = std::fmod(idlePhase_ + dt, kTimeWrapSeconds);
    const float t = idlePhase_;
    const float bass = 0.16f + 0.10f * std::sin(t * 0.6f);
    const float mid = 0.13f + 0.09f * std::sin(t * 1.0f + 1.7f);
    const float treble = 0.05f + 0.04f * std::sin(t * 1.8f + 3.1f);
    fillIdleBands(t, 0.07f);
    return idleAudioFeatures(bass, mid, treble, 0.18f);
}

void WaterScene::queueIdleRain(float dt) {
    rainAccum_ += dt;
    if (rainAccum_ < 0.45f) return;
    rainAccum_ = 0.0f;
    const float x = (nextFloat() * 2.0f - 1.0f) * sim_.aspect() * 0.85f;
    const float y = nextFloat() * 2.0f - 1.0f;
    hue::rgb(hue::base(params_.paletteBase()) + 0.12f * nextFloat(), 0.5f, rgb_.data());
    sim_.queueDrop(x, y * 0.85f, 0.05f, 0.28f * std::clamp(params_.waterRippleStrength, 0.0f, 2.0f), rgb_[0], rgb_[1], rgb_[2]);
}

void WaterScene::queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength) {
    std::lock_guard<std::mutex> lock(strokeLock_);
    if (touchStrokes_.size() >= static_cast<size_t>(kMaxTouchBacklog)) return;
    touchStrokes_.push_back({nx, ny, ndx, ndy, dt, strength});
}

void WaterScene::drainTouchStrokes(float rippleStrength, float baseHue) {
    drainedStrokes_.clear();
    {
        std::lock_guard<std::mutex> lock(strokeLock_);
        drainedStrokes_.swap(touchStrokes_);
    }
    for (const auto& st : drainedStrokes_) {
        hue::rgb(baseHue + 0.5f * hue::range(params_.hueRange), 1.0f, rgb_.data());
        sim_.queueStroke(st[0] * sim_.aspect(), st[1], st[2] * sim_.aspect(), st[3], st[4], kTouchRadius, st[5] * std::max(rippleStrength, 0.2f),
                         rgb_[0], rgb_[1], rgb_[2]);
    }
}

void WaterScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!sim_.available() || !displayOk_) return;
    resetFrameState();
    const SceneParams& p = params_;
    const GeodeFeatureFrame f = scaledFeatures();
    const bool idle = isIdle();

    saveGlState();
    autoQualityTick();

    sim_.waveSpeed = 1.2f * std::clamp(p.waterWaveSpeed, 0.2f, 2.0f);
    sim_.damping = std::clamp(p.waterDamping, 0.9f, 0.999f);
    sim_.inkFlow = std::clamp(p.waterLiquidFlow, 0.0f, 4.0f);
    sim_.inkDissipation = std::clamp(p.waterLiquidFade, 0.0f, 2.0f);

    configureChoreography();
    emitters_.applyParams(p);
    emitters_.forceScale = std::clamp(p.fluidSplatForce, 0.0f, 3.0f);

    const float simDt = std::clamp(lastDt_, 0.0f, 1.0f / 30.0f);
    choreography_.tick(f, simDt, sim_.aspect());
    const float pcmKick = std::clamp(pcmStrike_, 0.0f, 1.0f);
    const float rippleStrength = std::clamp(p.waterRippleStrength, 0.0f, 2.0f);
    const float catchRadius = fluid::water::catchWellRadius(p.fluidCatchRadius);
    const float baseHue = hue::base(p.paletteBase());
    emitters_.tick(f, simDt, sim_.aspect(), baseHue, hue::range(p.hueRange), splats_);
    for (const auto& s : splats_) {
        const float speed = std::sqrt(s.velX * s.velX + s.velY * s.velY) / fluid::Emitters::kBaseSpeed;
        if (fluid::water::isCatchWell(s.r, s.g, s.b)) {
            const float well = fluid::water::catchWellAmplitude(speed, catchRadius, rippleStrength);
            if (std::fabs(well) > 1e-4f) sim_.queueDrop(s.curX, s.curY, catchRadius, well);
            continue;
        }
        const float amp = (0.06f + 0.5f * std::min(speed, 2.0f)) * rippleStrength * (1.0f + pcmKick * 0.6f);
        if (amp > 1e-4f) sim_.queueDrop(s.curX, s.curY, s.radius * 0.6f, amp, s.r * kInkGain, s.g * kInkGain, s.b * kInkGain);
    }
    if (idle) queueIdleRain(lastDt_);
    drainTouchStrokes(rippleStrength, baseHue);
    sim_.step(simDt);
    hasPending_ = false;

    restoreFramebufferAndViewport();
    glDisable(GL_BLEND);
    glUseProgram(display_.program());
    glUniform2f(display_.loc("uInvRes"), sim_.texelW(), sim_.texelH());
    glUniform1f(display_.loc("uAspect"), sim_.aspect());
    glUniform1f(display_.loc("uTime"), time_);
    glUniform1f(display_.loc("uBaseHue"), baseHue);
    glUniform1f(display_.loc("uHueSpan"), hue::span(p.hueRange, p.paletteRange()));
    glUniform1f(display_.loc("uDepth"), std::clamp(p.waterDepth, 0.0f, 1.0f));
    glUniform1f(display_.loc("uSpecular"), std::clamp(p.waterSpecular, 0.0f, 1.0f));
    glUniform1f(display_.loc("uFlowDrift"), std::clamp(p.waterFlow, 0.0f, 1.0f));
    glUniform1f(display_.loc("uRefract"), 0.9f);
    glUniform1f(display_.loc("uTreble"), std::clamp(f.treble + pcmKick * 0.5f, 0.0f, 2.0f));
    glUniform1f(display_.loc("uBrightness"), fluid::water::kDisplayBrightness);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sim_.heightTex());
    glUniform1i(display_.loc("uHeight"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sim_.inkAvailable() ? sim_.inkTex() : sim_.heightTex());
    glUniform1i(display_.loc("uInk"), 1);
    glUniform1f(display_.loc("uInkAmount"), sim_.inkAvailable() ? std::clamp(p.waterLiquid, 0.0f, 1.0f) : 0.0f);
    quad_.draw();
    glActiveTexture(GL_TEXTURE0);

    restoreBlend();
}

void WaterScene::release() {
    sim_.release();
    if (display_.program() != 0) glDeleteProgram(display_.program());
    display_ = UniformCache(0);
    displayOk_ = false;
    quad_.release();
    appliedTier_ = -1;
}

}  // namespace geode::viz
