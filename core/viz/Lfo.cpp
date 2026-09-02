#include "viz/Lfo.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

bool chainOf(LfoTarget target, ModChain& out) {
    switch (target) {
        case LfoTarget::Lfo1Rate: out = {0, ModChainField::Rate}; return true;
        case LfoTarget::Lfo1Depth: out = {0, ModChainField::Depth}; return true;
        case LfoTarget::Lfo2Rate: out = {1, ModChainField::Rate}; return true;
        case LfoTarget::Lfo2Depth: out = {1, ModChainField::Depth}; return true;
        case LfoTarget::Lfo3Rate: out = {2, ModChainField::Rate}; return true;
        case LfoTarget::Lfo3Depth: out = {2, ModChainField::Depth}; return true;
        default: return false;
    }
}

bool isLuminanceTarget(LfoTarget target) {
    switch (target) {
        case LfoTarget::Brightness: case LfoTarget::Intensity: case LfoTarget::Saturation: case LfoTarget::Bloom:
        case LfoTarget::Glitch: case LfoTarget::Vignette: case LfoTarget::ColorShift: case LfoTarget::PaletteMix:
        case LfoTarget::Temperature:
            return true;
        default:
            return false;
    }
}

ModPolarity naturalPolarity(ModSource source) {
    switch (source) {
        case ModSource::Lfo: case ModSource::StereoPan: return ModPolarity::Bipolar;
        default: return ModPolarity::Positive;
    }
}

float polarized(float raw, ModPolarity polarity) {
    switch (polarity) {
        case ModPolarity::Bipolar: return raw;
        case ModPolarity::Positive: return (raw + 1.0f) * 0.5f;
        case ModPolarity::Negative: return -(raw + 1.0f) * 0.5f;
    }
    return raw;
}

float shape(float value, ModCurve curve) {
    const float m = std::clamp(std::fabs(value), 0.0f, 1.0f);
    float shaped = m;
    switch (curve) {
        case ModCurve::Linear: shaped = m; break;
        case ModCurve::Exponential: shaped = m * m; break;
        case ModCurve::Logarithmic: shaped = std::sqrt(m); break;
        case ModCurve::Smooth: shaped = m * m * (3.0f - 2.0f * m); break;
    }
    return value < 0.0f ? -shaped : shaped;
}

SceneParams applyLfoTarget(const SceneParams& r, LfoTarget target, float v) {
    SceneParams o = r;
    switch (target) {
        case LfoTarget::Speed: o.speed = std::clamp(r.speed + v, 0.05f, 4.0f); break;
        case LfoTarget::Zoom: o.zoom = std::clamp(r.zoom + v, 0.3f, 3.0f); break;
        case LfoTarget::Rotation: o.rotation = std::clamp(r.rotation + v * 1.5f, -3.0f, 3.0f); break;
        case LfoTarget::Sway: o.sway = std::clamp(r.sway + v, 0.0f, 1.0f); break;
        case LfoTarget::Pulse: o.pulse = std::clamp(r.pulse + v, 0.0f, 1.0f); break;
        case LfoTarget::DriftX: o.driftX = std::clamp(r.driftX + v, -1.0f, 1.0f); break;
        case LfoTarget::DriftY: o.driftY = std::clamp(r.driftY + v, -1.0f, 1.0f); break;
        case LfoTarget::Warp: o.warp = std::clamp(r.warp + v, 0.0f, 1.0f); break;
        case LfoTarget::Ripple: o.ripple = std::clamp(r.ripple + v, 0.0f, 1.0f); break;
        case LfoTarget::Morph: o.morph = std::clamp(r.morph + v, 0.0f, 1.0f); break;
        case LfoTarget::Twist: o.twist = std::clamp(r.twist + v, -1.0f, 1.0f); break;
        case LfoTarget::Tile: o.tile = std::clamp(r.tile + v * 2.0f, 1.0f, 6.0f); break;
        case LfoTarget::Pixelate: o.pixelate = std::clamp(r.pixelate + v, 0.0f, 1.0f); break;
        case LfoTarget::Posterize: o.posterize = std::clamp(r.posterize + v, 0.0f, 1.0f); break;
        case LfoTarget::ColorShift: o.colorShift = r.colorShift + v * 0.5f; break;
        case LfoTarget::PaletteMix: o.paletteMix = std::clamp(r.paletteMix + v, 0.0f, 1.0f); break;
        case LfoTarget::Saturation: o.saturation = std::clamp(r.saturation + v, 0.0f, 1.5f); break;
        case LfoTarget::Brightness: o.brightness = std::clamp(r.brightness + v, 0.2f, 2.0f); break;
        case LfoTarget::Intensity: o.intensity = std::clamp(r.intensity + v, 0.2f, 2.0f); break;
        case LfoTarget::Bloom: o.bloom = std::clamp(r.bloom + v, 0.0f, 1.0f); break;
        case LfoTarget::Temperature: o.temperature = std::clamp(r.temperature + v, -1.0f, 1.0f); break;
        case LfoTarget::Turbulence: o.turbulence = std::clamp(r.turbulence + v, 0.0f, 1.5f); break;
        case LfoTarget::ChromaAb: o.chromaAb = std::clamp(r.chromaAb + v, 0.0f, 1.0f); break;
        case LfoTarget::Vignette: o.vignette = std::clamp(r.vignette + v, 0.0f, 1.0f); break;
        case LfoTarget::Glitch: o.glitch = std::clamp(r.glitch + v, 0.0f, 1.0f); break;
        case LfoTarget::Fisheye: o.fisheye = std::clamp(r.fisheye + v, -1.0f, 1.0f); break;
        case LfoTarget::ParticleSize: o.particleSize = std::clamp(r.particleSize + v, 0.3f, 2.5f); break;
        case LfoTarget::TrailLength: o.trailLength = std::clamp(r.trailLength + v, 0.05f, 0.98f); break;
        case LfoTarget::FluidCurl: o.fluidCurl = std::clamp(r.fluidCurl + v * 25.0f, 0.0f, 50.0f); break;
        case LfoTarget::FluidRadius: o.fluidSplatRadius = std::clamp(r.fluidSplatRadius + v * 0.15f, 0.02f, 0.4f); break;
        case LfoTarget::FluidForce: o.fluidSplatForce = std::clamp(r.fluidSplatForce + v * 1.5f, 0.0f, 3.0f); break;
        case LfoTarget::FluidGlow: o.fluidBloomIntensity = std::clamp(r.fluidBloomIntensity + v, 0.1f, 2.0f); break;
        case LfoTarget::FluidFade: o.fluidDensityDissipation = std::clamp(r.fluidDensityDissipation + v * 1.5f, 0.0f, 4.0f); break;
        case LfoTarget::FluidCatchPull: o.fluidCatchPull = std::clamp(r.fluidCatchPull + v * 1.5f, 0.0f, 3.0f); break;
        case LfoTarget::FluidCatchRadius: o.fluidCatchRadius = std::clamp(r.fluidCatchRadius + v * 0.12f, 0.03f, 0.3f); break;
        case LfoTarget::FlowStrength: o.flowStrength = std::clamp(r.flowStrength + v, 0.0f, 1.0f); break;
        case LfoTarget::WaterRipple: o.waterRippleStrength = std::clamp(r.waterRippleStrength + v, 0.0f, 2.0f); break;
        case LfoTarget::RippleOverlay: o.rippleOverlayStrength = std::clamp(r.rippleOverlayStrength + v, 0.0f, 1.0f); break;
        case LfoTarget::None: case LfoTarget::Lfo1Rate: case LfoTarget::Lfo1Depth: case LfoTarget::Lfo2Rate:
        case LfoTarget::Lfo2Depth: case LfoTarget::Lfo3Rate: case LfoTarget::Lfo3Depth:
            break;
    }
    return o;
}

const std::array<float, LfoEngine::kSlots>& LfoEngine::tick(float dt, const GeodeFeatureFrame& features, const float* extRateAdd,
                                                             const float* extDepthAdd) {
    for (int i = 0; i < kSlots; i++) {
        out_[i] = 0.0f;
        rateAdd_[i] = extRateAdd ? extRateAdd[i] : 0.0f;
        depthAdd_[i] = extDepthAdd ? extDepthAdd[i] : 0.0f;
    }
    for (int i = 0; i < kSlots; i++) {
        const LfoConfig& c = configs[i];
        if (!c.enabled || c.target == LfoTarget::None) continue;
        const float depth = std::clamp(c.depth + depthAdd_[i], 0.0f, 2.0f);
        float raw = 0.0f;
        switch (c.source) {
            case ModSource::Lfo: raw = oscillator(i, c, dt); break;
            case ModSource::Bass: raw = follow(i, features.bass, dt); break;
            case ModSource::Mid: raw = follow(i, features.mid, dt); break;
            case ModSource::Treble: raw = follow(i, features.treble, dt); break;
            case ModSource::Level: raw = follow(i, live::level(features), dt); break;
            case ModSource::Brightness: raw = follow(i, live::brightness(features), dt); break;
            case ModSource::Transient: raw = follow(i, live::hit(features), dt); break;
            case ModSource::StereoWidth: raw = follow(i, live::width(features), dt); break;
            case ModSource::StereoPan: raw = followBipolar(i, live::pan(features), dt); break;
        }
        const float v = shape(polarized(raw, c.polarity), c.curve) * depth;
        out_[i] = v;
        ModChain chain{};
        if (!chainOf(c.target, chain)) continue;
        // A slot may only steer a LATER slot, or one of them is always a frame behind the other.
        if (i >= chain.slot) continue;
        if (chain.field == ModChainField::Rate) rateAdd_[chain.slot] += v * kChainRateHz; else depthAdd_[chain.slot] += v;
    }
    return out_;
}

SceneParams LfoEngine::apply(const SceneParams& p, const std::array<float, kSlots>& values) const {
    SceneParams r = p;
    for (int i = 0; i < kSlots; i++) {
        if (!configs[i].enabled) continue;
        r = applyLfoTarget(r, configs[i].target, values[i]);
    }
    return r;
}

float LfoEngine::oscillator(int i, const LfoConfig& c, float dt) {
    const float period = std::clamp(c.rateSeconds, LfoConfig::kMinRateSeconds, LfoConfig::kMaxRateSeconds);
    const float rate = safety::limitLfoRate(std::clamp(1.0f / period + rateAdd_[i], kMinRateHz, kMaxRateHz), c.target);
    phases_[i] = std::fmod(phases_[i] + rate * dt, 1.0f);
    totalPhase_[i] = std::fmod(totalPhase_[i] + rate * dt, kShPhaseWrap);
    const float ph = phases_[i];
    switch (c.wave) {
        case LfoWave::Sine: return std::sin(ph * kTau);
        case LfoWave::Triangle: return 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        case LfoWave::Saw: return ph * 2.0f - 1.0f;
        case LfoWave::Square: return ph < 0.5f ? 1.0f : -1.0f;
        case LfoWave::Random: {
            const int cycle = static_cast<int>(std::floor(totalPhase_[i]));
            if (cycle != lastCycle_[i]) {
                lastCycle_[i] = cycle;
                std::uniform_real_distribution<float> unit(0.0f, 1.0f);
                sampleHold_[i] = unit(rng_) * 2.0f - 1.0f;
            }
            return sampleHold_[i];
        }
    }
    return 0.0f;
}

float LfoEngine::follow(int i, float value, float dt) {
    return followBipolar(i, std::clamp(value, 0.0f, 1.0f) * 2.0f - 1.0f, dt);
}

float LfoEngine::followBipolar(int i, float value, float dt) {
    const float target = std::clamp(value, -1.0f, 1.0f);
    const float tau = target > followed_[i] ? kFollowRiseSeconds : kFollowFallSeconds;
    followed_[i] += (target - followed_[i]) * std::clamp(dt / tau, 0.0f, 1.0f);
    return followed_[i];
}

}  // namespace geode::viz
