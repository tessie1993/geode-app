#include "viz/Lfo.hpp"

#include <algorithm>
#include <cmath>

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
        case LfoTarget::Vignette: case LfoTarget::ColorShift: case LfoTarget::PaletteMix:
        case LfoTarget::Temperature:
            return true;
        default:
            return false;
    }
}

int luminanceIndex(LfoTarget target) {
    switch (target) {
        case LfoTarget::Brightness: return 0;
        case LfoTarget::Intensity: return 1;
        case LfoTarget::Saturation: return 2;
        case LfoTarget::Bloom: return 3;
        case LfoTarget::Vignette: return 4;
        case LfoTarget::ColorShift: return 5;
        case LfoTarget::PaletteMix: return 6;
        case LfoTarget::Temperature: return 7;
        default: return -1;
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

namespace {

// What a target writes: one float field, and how far one unit of modulation moves it. The clamp is
// the field's own range in viz/ParamTable.inc, so an LFO, an envelope or a route can never push a
// field past what its control offers, and a Circular field (the hue) adds unclamped.
struct Drive {
    float SceneParams::*member;
    ParamSlot slot;
    float scale;
};

bool driveOf(LfoTarget target, Drive& out) {
#define GEODE_DRIVE(field, scale)                              \
    out = Drive{&SceneParams::field, ParamSlot::field, scale}; \
    return true
    switch (target) {
        case LfoTarget::Speed: GEODE_DRIVE(speed, 1.0f);
        case LfoTarget::Zoom: GEODE_DRIVE(zoom, 1.0f);
        case LfoTarget::Rotation: GEODE_DRIVE(rotation, 1.5f);
        case LfoTarget::Sway: GEODE_DRIVE(sway, 1.0f);
        case LfoTarget::Pulse: GEODE_DRIVE(pulse, 1.0f);
        case LfoTarget::DriftX: GEODE_DRIVE(driftX, 1.0f);
        case LfoTarget::DriftY: GEODE_DRIVE(driftY, 1.0f);
        case LfoTarget::Warp: GEODE_DRIVE(warp, 1.0f);
        case LfoTarget::Ripple: GEODE_DRIVE(ripple, 1.0f);
        case LfoTarget::Morph: GEODE_DRIVE(morph, 1.0f);
        case LfoTarget::Twist: GEODE_DRIVE(twist, 1.0f);
        case LfoTarget::Tile: GEODE_DRIVE(tile, 2.0f);
        case LfoTarget::Pixelate: GEODE_DRIVE(pixelate, 1.0f);
        case LfoTarget::Posterize: GEODE_DRIVE(posterize, 1.0f);
        case LfoTarget::ColorShift: GEODE_DRIVE(colorShift, 0.5f);
        case LfoTarget::PaletteMix: GEODE_DRIVE(paletteMix, 1.0f);
        case LfoTarget::Saturation: GEODE_DRIVE(saturation, 1.0f);
        case LfoTarget::Brightness: GEODE_DRIVE(brightness, 1.0f);
        case LfoTarget::Intensity: GEODE_DRIVE(intensity, 1.0f);
        case LfoTarget::Bloom: GEODE_DRIVE(bloom, 1.0f);
        case LfoTarget::Temperature: GEODE_DRIVE(temperature, 1.0f);
        case LfoTarget::Turbulence: GEODE_DRIVE(turbulence, 1.0f);
        case LfoTarget::ChromaAb: GEODE_DRIVE(chromaAb, 1.0f);
        case LfoTarget::Vignette: GEODE_DRIVE(vignette, 1.0f);
        case LfoTarget::Fisheye: GEODE_DRIVE(fisheye, 1.0f);
        case LfoTarget::ParticleSize: GEODE_DRIVE(particleSize, 1.0f);
        case LfoTarget::TrailLength: GEODE_DRIVE(trailLength, 1.0f);
        case LfoTarget::FluidCurl: GEODE_DRIVE(fluidCurl, 25.0f);
        case LfoTarget::FluidRadius: GEODE_DRIVE(fluidSplatRadius, 0.15f);
        case LfoTarget::FluidForce: GEODE_DRIVE(fluidSplatForce, 1.5f);
        case LfoTarget::FluidGlow: GEODE_DRIVE(fluidBloomIntensity, 1.0f);
        case LfoTarget::FluidFade: GEODE_DRIVE(fluidDensityDissipation, 1.5f);
        case LfoTarget::FluidCatchPull: GEODE_DRIVE(fluidCatchPull, 1.5f);
        case LfoTarget::FluidCatchRadius: GEODE_DRIVE(fluidCatchRadius, 0.12f);
        case LfoTarget::FlowStrength: GEODE_DRIVE(flowStrength, 1.0f);
        case LfoTarget::WaterRipple: GEODE_DRIVE(waterRippleStrength, 1.0f);
        case LfoTarget::RippleOverlay: GEODE_DRIVE(rippleOverlayStrength, 1.0f);
        case LfoTarget::CamFly: GEODE_DRIVE(camFly, 1.0f);
        case LfoTarget::CamOrbit: GEODE_DRIVE(camOrbit, 1.0f);
        case LfoTarget::CamPush: GEODE_DRIVE(camPush, 1.0f);
        case LfoTarget::CamYaw: GEODE_DRIVE(camYaw, 1.0f);
        case LfoTarget::CamPitch: GEODE_DRIVE(camPitch, 1.0f);
        case LfoTarget::CamRoll: GEODE_DRIVE(camRoll, 1.0f);
        case LfoTarget::CamFov: GEODE_DRIVE(camFov, 1.0f);
        case LfoTarget::CamShake: GEODE_DRIVE(camShake, 1.0f);
        case LfoTarget::FlowCurl: GEODE_DRIVE(flowCurl, 25.0f);
        case LfoTarget::FlowForce: GEODE_DRIVE(flowForce, 1.5f);
        case LfoTarget::EndlessZoomSpeed: GEODE_DRIVE(endlessZoomSpeed, 0.5f);
        // None, the retired Glitch slot and the chained targets write no scene parameter.
        default: return false;
    }
#undef GEODE_DRIVE
}

}  // namespace

SceneParams applyLfoTarget(const SceneParams& r, LfoTarget target, float v) {
    SceneParams o = r;
    Drive drive{};
    if (!driveOf(target, drive)) return o;
    const ParamSpec& spec = SceneParams::spec(drive.slot);
    const float moved = r.*drive.member + v * drive.scale;
    o.*drive.member = spec.kind == ParamKind::Circular ? moved : std::clamp(moved, spec.min, spec.max);
    return o;
}

const std::array<float, LfoEngine::kSlots>& LfoEngine::tick(float dt, const GeodeSoundFrame& sound, const float* extRateAdd,
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
            // The sound engine's frame: the bands' fast att envelopes halved (typical 0.5, a hit 1),
            // the shaped level, the centroid, the note-start envelope and the stereo field.
            case ModSource::Bass: raw = follow(i, 0.5f * sound.bassAtt, dt); break;
            case ModSource::Mid: raw = follow(i, 0.5f * sound.midAtt, dt); break;
            case ModSource::Treble: raw = follow(i, 0.5f * sound.trebleAtt, dt); break;
            case ModSource::Level: raw = follow(i, sound.level, dt); break;
            case ModSource::Brightness: raw = follow(i, sound.centroid, dt); break;
            case ModSource::Transient: raw = follow(i, sound.onsetEnv, dt); break;
            case ModSource::StereoWidth: raw = follow(i, sound.stereoWidth, dt); break;
            case ModSource::StereoPan: raw = followBipolar(i, sound.stereoPan, dt); break;
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

void LfoEngine::addLuminanceDrive(const std::array<float, kSlots>& values, LuminanceDrive& out) const {
    for (int i = 0; i < kSlots; i++) {
        if (!configs[i].enabled) continue;
        const int lum = luminanceIndex(configs[i].target);
        if (lum >= 0) out[static_cast<size_t>(lum)] += values[i];
    }
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
