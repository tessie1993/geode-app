#include "viz/fluid/FluidMath.hpp"
#include "viz/LiveSignal.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid {

namespace math {

float driven(float value, float audioDrive) {
    const float d = std::clamp(audioDrive, kMinAudioDrive, kMaxAudioDrive);
    return std::clamp(value * d, 0.0f, std::max(value, kDriveCeiling));
}

int stateSide(int count) { return std::max(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))), 2); }

std::pair<float, float> segDist(float ax, float ay, float bx, float by, float px, float py) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float len2 = abx * abx + aby * aby;
    if (len2 < 1e-8f) {
        const float dx = px - ax;
        const float dy = py - ay;
        return {std::sqrt(dx * dx + dy * dy), 0.0f};
    }
    const float fp = std::clamp(((px - ax) * abx + (py - ay) * aby) / len2, 0.0f, 1.0f);
    const float dx = px - (ax + abx * fp);
    const float dy = py - (ay + aby * fp);
    return {std::sqrt(dx * dx + dy * dy), fp};
}

float dragStep(float v, float flow, float drag, float dt) {
    const float k = 1.0f - std::pow(1.0f - drag, dt * 60.0f);
    return v + (flow - v) * k;
}

float splatFalloff(float dist, float radius, float along) {
    const float r = std::max(radius, 1e-4f);
    if (dist > r * kSplatCutoffRadii) return 0.0f;
    return std::exp(-dist / r) * (1.0f - along * 0.6f);
}

float macCormack(float forward, float source, float backward, float lo, float hi) {
    return std::clamp(forward + 0.5f * (source - backward), lo, hi);
}

std::vector<std::pair<int, int>> multigridLevels(int w, int h) {
    std::vector<std::pair<int, int>> out{{std::max(w, 1), std::max(h, 1)}};
    while (static_cast<int>(out.size()) < kMaxLevels) {
        const auto [cw, ch] = out.back();
        const int nw = (cw + 1) / 2;
        const int nh = (ch + 1) / 2;
        if (std::min(nw, nh) < kMinCoarseSide) break;
        out.emplace_back(nw, nh);
    }
    return out;
}

SolvePlan planPressureSolve(int budget, int w, int h) {
    budget = std::max(budget, 1);
    const auto levels = multigridLevels(w, h);
    SolvePlan plan;
    plan.levels = static_cast<int>(levels.size());
    if (plan.levels < 2) {
        // A grid too small to coarsen: the budget is all fine passes.
        plan.extraPasses = budget;
        plan.cost = static_cast<float>(budget);
        plan.draws = budget;
        return plan;
    }
    const float fine = static_cast<float>(levels[0].first) * static_cast<float>(levels[0].second);
    const auto area = [&](int l) {
        return static_cast<float>(levels[static_cast<size_t>(l)].first) * static_cast<float>(levels[static_cast<size_t>(l)].second) / fine;
    };
    // Larger budgets smooth harder before they add cycles: a pass on a big grid is worth more than
    // a render-target switch, a cycle is ~6 switches per level.
    plan.smooth = budget >= 32 ? 3 : (budget >= 20 ? 2 : 1);
    plan.coarseIterations = 6;
    float cycleCost = 0.0f;
    int cycleDraws = 0;
    for (int l = 0; l + 1 < plan.levels; ++l) {
        cycleCost += area(l) * static_cast<float>(2 * plan.smooth + 1) + area(l + 1);  // smooth, prolong; restrict at l+1
        cycleDraws += 2 * plan.smooth + 2;
    }
    cycleCost += area(plan.levels - 1) * static_cast<float>(plan.coarseIterations);
    cycleDraws += plan.coarseIterations + 1;  // + the clear of the coarsest correction
    plan.cycles = std::clamp(static_cast<int>(std::floor(static_cast<float>(budget) / cycleCost)), 1, kMaxCycles);
    const float left = static_cast<float>(budget) - static_cast<float>(plan.cycles) * cycleCost;
    plan.extraPasses = std::max(static_cast<int>(std::floor(left)), 0);
    plan.cost = static_cast<float>(plan.cycles) * cycleCost + static_cast<float>(plan.extraPasses);
    // Coarse corrections other than the coarsest are cleared too: one clear per coarse level.
    plan.draws = plan.cycles * (cycleDraws + plan.levels - 2) + plan.extraPasses;
    return plan;
}

namespace {
float unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
}  // namespace

Drives drives(const GeodeSoundFrame& f) {
    Drives d;
    const float trend = unit(2.0f * f.energyTrend - 1.0f);  // 0 steady or falling, 1 at twice as loud
    d.heat = 0.15f + 0.50f * live::bass(f) + 0.20f * live::band(f.subAtt) + 0.25f * unit(f.kickEnv) + 0.10f * trend;
    d.buoyancy = 0.75f + 0.30f * trend + 0.20f * unit(f.densitySlow);
    d.particleStir = 0.25f + 0.35f * live::treble(f) + 0.25f * unit(f.hatEnv) + 0.20f * unit(f.flatness);
    d.stirRate = 0.20f + 0.45f * live::mid(f) + 0.20f * unit(0.5f * f.flux) + 0.15f * unit(f.runDensity);
    d.streak = 0.70f + 0.30f * unit(f.onsetEnv) + 0.20f * unit(f.snareEnv) + 0.30f * live::level(f);
    return d;
}

float frameSimDt(float dt) {
    if (!(dt > 0.0f)) return 0.0f;
    return std::min(dt, kMaxSimStep * static_cast<float>(kMaxSubsteps));
}

Substeps substeps(float dt) {
    const float total = frameSimDt(dt);
    // A hair under 1 so a step of exactly kMaxSimStep (a 30 fps frame) stays one step after rounding.
    const int n = std::clamp(static_cast<int>(std::ceil(total / kMaxSimStep - 1e-4f)), 1, kMaxSubsteps);
    return {n, total / static_cast<float>(n)};
}

float heatInjection(float heatDrive, float heatGain, float dt) {
    return std::max(heatGain, 0.0f) * std::clamp(heatDrive, 0.0f, 2.0f) * 6.0f * std::clamp(dt, 0.0f, 1.0f / 15.0f);
}

float heatHeadroom(float temperature) { return std::clamp(1.0f - temperature / kMaxTemperature, 0.0f, 1.0f); }

float coolingDivisor(float cooling, float dt) { return 1.0f + std::max(cooling, 0.0f) * std::max(dt, 0.0f); }

float buoyancyAccel(float buoyancy, float temperature) { return buoyancy * std::clamp(temperature, 0.0f, kMaxTemperature); }

float particleExposure(int count, int refCount) {
    if (count <= 0 || refCount <= 0) return 1.0f;
    return std::pow(static_cast<float>(refCount) / static_cast<float>(count), 0.75f);
}

}  // namespace math

GeodeSoundFrame scaledFeatures(const GeodeSoundFrame& features, float audioDrive) {
    const float d = std::clamp(audioDrive, math::kMinAudioDrive, math::kMaxAudioDrive);
    if (d == 1.0f) return features;
    GeodeSoundFrame out = features;
    for (float& band : out.spectrum) band = math::driven(band, d);
    out.rms = math::driven(features.rms, d);
    live::setBands(out, math::driven(live::bass(features), d), math::driven(live::mid(features), d), math::driven(live::treble(features), d));
    return out;
}

namespace curl {

float beatDrive(float beatEnvelope, float beatResponse) { return beatEnvelope * std::clamp(beatResponse, 0.0f, 2.0f); }

float fieldAmp(float audioDrive, float beatDrive) {
    return kBaseAmp * std::clamp(audioDrive, math::kMinAudioDrive, math::kMaxAudioDrive) * (1.0f + std::clamp(beatDrive, 0.0f, 2.0f) * kBeatAmp);
}

float retention(float trailLength, bool trails) {
    if (!trails) return kOffRetention;
    return kMinRetention + (1.0f - kMinRetention) * std::clamp(trailLength, 0.0f, 1.0f);
}

float fadeAlpha(bool trails, float trailLength, float dt) { return 1.0f - std::pow(retention(trailLength, trails) * 0.97f, dt * 60.0f); }

float warpDecay(float retentionValue, float dt) { return std::pow(std::clamp(retentionValue * 0.97f + 0.02f, 0.0f, 0.99f), dt * 60.0f); }

float particleBrightness(float beatEnvelope) { return kBaseBrightness + std::clamp(beatEnvelope, 0.0f, 1.0f) * kBeatBrightness; }

}  // namespace curl

}  // namespace geode::viz::fluid
