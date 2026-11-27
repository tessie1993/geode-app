#include "viz/modulation/Adsr.hpp"

#include <algorithm>
#include <cmath>

#include "viz/modulation/Slew.hpp"

namespace geode::viz {

static_assert(config::spec(config::Slot::adsr_band).max == static_cast<float>(EnvBand::Width), "adsr band row");
static_assert(config::indexInGroup(config::Slot::adsr_moment) == GEODE_ADSR_CONFIG_FLOATS - 1,
              "one adsr row per wire float before the targets");

namespace {
// A tracking sustain follows its target at 8 a second: a time constant of an eighth of a second.
constexpr float kSustainSeconds = 1.0f / 8.0f;

bool hasLiveTarget(const AdsrConfig& c) {
    for (const LfoTarget t : c.targets) {
        if (t != LfoTarget::None) return true;
    }
    return false;
}
}  // namespace

float AdsrVoice::step(const AdsrShape& c, float dt, float hit, float energy, bool gateOpen, bool gateHolds) {
    if (hit > 0.0f && (c.retrigger || stage == AdsrStage::Idle || stage == AdsrStage::Release)) {
        const bool wasAttacking = stage == AdsrStage::Attack;
        stage = AdsrStage::Attack;
        peak = wasAttacking ? std::max(peak, hit) : std::max(hit, level);
    }
    const float ceiling = std::clamp(peak, 0.0f, 1.0f);
    const float sustainTarget =
        (c.sustainTrack ? c.sustain * std::clamp(energy / std::max(c.gateThreshold, AdsrEngine::kMinGateThreshold), 0.0f, 1.0f)
                        : c.sustain) *
        ceiling;
    // Each stage runs its linear rate for as much of dt as it needs; the rest of the frame goes on into the next
    // stage, so a stage's length in seconds does not depend on the frame it ends in.
    float left = std::max(dt, 0.0f);
    if (stage == AdsrStage::Attack) {
        const float rate = ceiling / std::max(c.attack, AdsrEngine::kMinStageSeconds);
        if (rate > 0.0f && level + rate * left < ceiling) {
            level += rate * left;
            left = 0.0f;
        } else {
            if (rate > 0.0f) left -= std::max(ceiling - level, 0.0f) / rate;
            level = ceiling;
            stage = AdsrStage::Decay;
        }
    }
    if (stage == AdsrStage::Decay) {
        const float rate = std::max(1.0f - sustainTarget, 0.05f) / std::max(c.decay, AdsrEngine::kMinStageSeconds);
        if (level - rate * left > sustainTarget) {
            level -= rate * left;
            left = 0.0f;
        } else {
            left -= std::max(level - sustainTarget, 0.0f) / rate;
            level = sustainTarget;
            stage = gateHolds ? AdsrStage::Sustain : AdsrStage::Release;
        }
    }
    if (stage == AdsrStage::Sustain) {
        if (gateHolds) {
            level = slew::step(level, sustainTarget, left, kSustainSeconds);
            return std::clamp(level, 0.0f, 1.0f);
        }
        stage = AdsrStage::Release;
    }
    if (stage == AdsrStage::Release) {
        level -= left / std::max(c.release, AdsrEngine::kMinStageSeconds);
        if (gateOpen && level > 0.01f) {
            stage = AdsrStage::Sustain;
        } else if (level <= 0.0f) {
            level = 0.0f;
            stage = AdsrStage::Idle;
        }
    }    return std::clamp(level, 0.0f, 1.0f);
}

const std::array<float, AdsrEngine::kCount>& AdsrEngine::tick(float dt, const GeodeSoundFrame& sound, std::uint32_t moments) {
    for (int i = 0; i < kCount; i++) {
        const AdsrConfig& c = configs[i];
        if (!c.enabled || !hasLiveTarget(c)) {
            voice_[i].reset();
            out_[i] = 0.0f;
            continue;
        }
        float energy = 0.0f;
        switch (c.band) {
            // The sound engine's levels on the old 0..1.5 scale the gate threshold is set in:
            // the bands' att envelopes halved, the shaped level, the centroid and the width.
            case EnvBand::Bass: energy = 0.5f * sound.bassAtt; break;
            case EnvBand::Mid: energy = 0.5f * sound.midAtt; break;
            case EnvBand::Treble: energy = 0.5f * sound.trebleAtt; break;
            case EnvBand::Rms: energy = sound.level; break;
            case EnvBand::Brightness: energy = sound.centroid; break;
            case EnvBand::Width: energy = sound.stereoWidth; break;
        }
        energy = std::isfinite(energy) ? std::clamp(energy, 0.0f, 1.5f) : 0.0f;
        const bool gateOpen = energy >= c.gateThreshold;
        const bool gateHolds = energy >= c.gateThreshold * 0.85f;
        // A note start (the engine's flux-peak onset, one frame) triggers the envelope, or the moment the user picked.
        const bool onMoment = c.moment >= 0 && c.moment < 32;
        const float onset = std::isfinite(sound.onset) ? std::clamp(sound.onset, 0.0f, 1.0f) : 0.0f;
        const float hit = onMoment ? (((moments >> c.moment) & 1u) != 0 ? 1.0f : 0.0f) : onset;
        const AdsrShape shape{c.attack, c.decay, c.sustain, c.release, c.sustainTrack, c.retrigger, c.gateThreshold};
        out_[i] = voice_[i].step(shape, dt, hit, energy, gateOpen, gateHolds);
    }
    return out_;
}

void AdsrEngine::lfoOffsets(const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs,
                            std::array<float, LfoEngine::kSlots>& rate, std::array<float, LfoEngine::kSlots>& depth) {
    rate.fill(0.0f);
    depth.fill(0.0f);
    for (int i = 0; i < kCount; i++) {
        const AdsrConfig& c = configs[i];
        if (!c.enabled || envs[i] <= 0.0f) continue;
        const float v = envs[i] * c.amount;
        for (const LfoTarget t : c.targets) {
            ModChain chain{};
            if (!chainOf(t, chain)) continue;
            if (chain.field == ModChainField::Rate) rate[chain.slot] += v * kChainRateHz; else depth[chain.slot] += v;
        }
    }
}

void AdsrEngine::apply(SceneParams& p, const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs) {
    for (int i = 0; i < kCount; i++) {
        const AdsrConfig& c = configs[i];
        if (!c.enabled || envs[i] <= 0.0f) continue;
        for (const LfoTarget t : c.targets) {
            ModChain chain{};
            if (t == LfoTarget::None || chainOf(t, chain)) continue;
            applyLfoTargetInPlace(p, t, envs[i] * c.amount);
        }
    }
}


}  // namespace geode::viz
