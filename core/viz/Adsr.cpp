#include "viz/Adsr.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

static_assert(config::spec(config::Slot::adsr_band).max == static_cast<float>(EnvBand::Width), "adsr band row");
static_assert(config::indexInGroup(config::Slot::adsr_moment) == GEODE_ADSR_CONFIG_FLOATS - 1,
              "one adsr row per wire float before the targets");

namespace {
bool hasLiveTarget(const AdsrConfig& c) {
    for (const LfoTarget t : c.targets) {
        if (t != LfoTarget::None) return true;
    }
    return false;
}
}  // namespace

const std::array<float, AdsrEngine::kCount>& AdsrEngine::tick(float dt, const GeodeSoundFrame& sound, std::uint32_t moments) {
    for (int i = 0; i < kCount; i++) {
        const AdsrConfig& c = configs[i];
        if (!c.enabled || !hasLiveTarget(c)) {
            level_[i] = 0.0f;
            stage_[i] = 0;
            peak_[i] = 1.0f;
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
        if (hit > 0.0f && (c.retrigger || stage_[i] == 0 || stage_[i] == 4)) {
            const bool wasAttacking = stage_[i] == 1;
            stage_[i] = 1;
            peak_[i] = wasAttacking ? std::max(peak_[i], hit) : std::max(hit, level_[i]);
        }
        const float ceiling = std::clamp(peak_[i], 0.0f, 1.0f);
        const float sustainTarget =
            (c.sustainTrack ? c.sustain * std::clamp(energy / std::max(c.gateThreshold, kMinGateThreshold), 0.0f, 1.0f) : c.sustain) * ceiling;
        switch (stage_[i]) {
            case 1:
                level_[i] += dt / std::max(c.attack, kMinStageSeconds) * ceiling;
                if (level_[i] >= ceiling) {
                    level_[i] = ceiling;
                    stage_[i] = 2;
                }
                break;
            case 2:
                level_[i] -= dt / std::max(c.decay, kMinStageSeconds) * std::max(1.0f - sustainTarget, 0.05f);
                if (level_[i] <= sustainTarget) {
                    level_[i] = sustainTarget;
                    stage_[i] = gateHolds ? 3 : 4;
                }
                break;
            case 3:
                level_[i] += (sustainTarget - level_[i]) * std::min(dt * 8.0f, 1.0f);
                if (!gateHolds) stage_[i] = 4;
                break;
            case 4:
                level_[i] -= dt / std::max(c.release, kMinStageSeconds);
                if (gateOpen && level_[i] > 0.01f) {
                    stage_[i] = 3;
                } else if (level_[i] <= 0.0f) {
                    level_[i] = 0.0f;
                    stage_[i] = 0;
                }
                break;
            default:
                break;
        }
        out_[i] = std::clamp(level_[i], 0.0f, 1.0f);
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

SceneParams AdsrEngine::apply(const SceneParams& p, const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs) {
    SceneParams r = p;
    for (int i = 0; i < kCount; i++) {
        const AdsrConfig& c = configs[i];
        if (!c.enabled || envs[i] <= 0.0f) continue;
        for (const LfoTarget t : c.targets) {
            ModChain chain{};
            if (t == LfoTarget::None || chainOf(t, chain)) continue;
            r = applyLfoTarget(r, t, envs[i] * c.amount);
        }
    }
    return r;
}


}  // namespace geode::viz
