#include "viz/Lfo.hpp"

#include <algorithm>
#include <cmath>

#include "viz/ParamMatrix.hpp"
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

bool isLuminanceTarget(LfoTarget target) { return matrix::target(target).luminance; }

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

// Every target's scale and range is a row of the parameter matrix (viz/ParamMatrix).
SceneParams applyLfoTarget(const SceneParams& r, LfoTarget target, float v) {
    const matrix::TargetRow& row = matrix::target(target);
    if (row.field == nullptr) return r;
    SceneParams o = r;
    const float x = r.*row.field + v * row.scale;
    o.*row.field = row.wraps ? x : std::clamp(x, row.min, row.max);
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
