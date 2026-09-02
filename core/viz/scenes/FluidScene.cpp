#include "viz/scenes/FluidScene.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"
#include "viz/fluid/FluidQuality.hpp"

namespace geode::viz {

void FluidScene::init() {
    sim_.onShaderError = [this](const std::string& m) { host_.onShaderError(m); };
    sim_.create();
    choreography_.reset();
    if (sim_.available()) {
        look_.create(sim_.texFormats());
        appliedTier_ = -1;
        appliedParticleSide_ = 0;
        applyQualityTier();
    } else {
        host_.onShaderError("Fluid style unavailable: this GPU can't render half-float buffers");
    }
}

void FluidScene::resize(int width, int height) {
    if (sim_.resize(width, height)) particles_.invalidateSeed();
    look_.resize(width, height);
}

void FluidScene::onApplyQualityTier(int index, bool userChanged) {
    const auto& tier = fluid::quality::tier(index);
    sim_.applyResolution(tier.simRes, tier.dyeRes);
    const bool recreateParticles = appliedParticleSide_ == 0 || (userChanged && appliedParticleSide_ != tier.particleSide);
    if (recreateParticles) {
        appliedParticleSide_ = tier.particleSide;
        particles_.create(tier.particleSide * tier.particleSide, sim_.texFormats());
    }
}

GeodeFeatureFrame FluidScene::idleFeatures(float dt) {
    idlePhase_ = std::fmod(idlePhase_ + dt, kIdleWrapSeconds);
    const float t = idlePhase_;
    const float bass = 0.18f + 0.12f * std::sin(t * 0.7f);
    const float mid = 0.15f + 0.10f * std::sin(t * 1.1f + 1.7f);
    const float treble = 0.05f + 0.04f * std::sin(t * 1.9f + 3.1f);
    fillIdleBands(t, 0.08f);
    return idleAudioFeatures(bass, mid, treble, 0.2f);
}

void FluidScene::draw(float timeSeconds) {
    if (!sim_.available()) return;
    resetFrameState();
    const SceneParams& p = params_;
    const GeodeFeatureFrame f = scaledFeatures();

    saveGlState();
    autoQualityTick();

    const float energy = std::clamp(f.rms, 0.0f, 1.0f);
    const float pcmKick = std::clamp(pcmStrike_, 0.0f, 1.0f);
    sim_.pressureIterations = std::clamp(p.fluidIterations, 8, 40);
    sim_.pressureDamp = std::clamp(p.fluidPressure, 0.0f, 1.0f);
    sim_.velocityDissipation = std::clamp(p.fluidVelocityDissipation, 0.0f, 4.0f);
    sim_.curlStrength = std::clamp(p.fluidCurl, 0.0f, 50.0f) * (1.0f + p.fluidCurlAudio * f.mid + pcmKick * 0.5f);
    sim_.densityDissipation = std::clamp(p.fluidDensityDissipation, 0.0f, 4.0f) * (1.0f + p.fluidFadeAudio * (1.0f - energy));
    sim_.chromaticAging = std::clamp(p.fluidChromaticAging, 0.0f, 1.0f);
    sim_.audioBass = f.bass;
    sim_.audioMid = f.mid;
    sim_.audioTreble = f.treble;
    sim_.audioEnergy = energy;
    sim_.audioBeat = live::hit(f);
    sim_.timeSeconds = time_;

    configureChoreography();
    emitters_.applyParams(p);
    emitters_.paletteCycleSpeed = hue::paletteCycleSpeed(p.fluidPaletteCycleSpeed);
    emitters_.forceScale = std::clamp(p.fluidSplatForce, 0.0f, 3.0f) * (1.0f + pcmKick * 0.5f);
    const float simDt = std::clamp(lastDt_, 0.0f, 1.0f / 30.0f);
    const float hueBase = hue::base(p.paletteBase());
    const float hueSpan = hue::span(p.hueRange, p.paletteRange());
    choreography_.tick(f, simDt, sim_.aspect());
    emitters_.tick(f, simDt, sim_.aspect(), hueBase, hueSpan, splats_);
    for (const auto& s : splats_) sim_.queueSplat(s);
    sim_.step(simDt);
    if (particles_.available() && p.fluidParticlesEnabled) {
        applyChoreographyTo(particles_);
        particles_.step(simDt, sim_.velocityTex(), sim_.aspect(), sim_.flowScale(), time_);
    }
    look_.bloomIntensity = std::clamp(p.fluidBloomIntensity, 0.1f, 2.0f) * (0.6f + p.fluidBloomAudio * energy);
    look_.bloomThreshold = std::clamp(p.fluidBloomThreshold, 0.0f, 1.0f);
    look_.sunraysWeight = std::clamp(p.fluidSunraysWeight, 0.3f, 1.0f);
    if (p.fluidDyeEnabled) look_.process(sim_.dyeTex(), p.fluidBloom, p.fluidSunrays);
    hasPending_ = false;

    restoreFramebufferAndViewport();
    if (p.fluidDyeEnabled) {
        if (look_.available()) {
            look_.drawDisplay(sim_.dyeTex(), p.fluidShading, p.fluidBloom, p.fluidSunrays, std::max(savedViewportWidth(), 1),
                              std::max(savedViewportHeight(), 1));
        } else {
            sim_.drawDisplay();
        }
    }
    if (particles_.available() && p.fluidParticlesEnabled) {
        particles_.draw(sim_.aspect(), (1.5f * std::clamp(p.particleSize, 0.2f, 3.0f)) * viewportDpiScale(), hueBase, hueSpan,
                        0.55f * std::clamp(p.fluidParticleBrightness, 0.0f, 2.0f) * (0.3f + std::clamp(p.density, 0.0f, 1.5f)),
                        static_cast<float>(p.particleShape), particle_look::glow(p.bloom), timeSeconds);
    }
    restoreBlend();
}

void FluidScene::release() {
    particles_.release();
    look_.release();
    sim_.release();
    appliedTier_ = -1;
    appliedParticleSide_ = 0;
}

}  // namespace geode::viz
