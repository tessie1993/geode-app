#include "viz/physics/PhysicsPlan.hpp"

#include <algorithm>
#include <cmath>

#include "viz/fluid/FluidQuality.hpp"

namespace geode::viz::physics {

namespace {

// The fluid's grids: FluidQuality's Low rung. The layer is a backdrop the picture paints, seen through the style's
// darks or screened over it, so it never needs the fluid styles' own grids.
constexpr int kGrid = 3;  // quality::kTiers "Low": 96 / 384

// The particle state's side: 36 864 particles.
constexpr int kSide = 192;

// How fast the dye moves toward the picture's colour, per second.
constexpr float kDyeRate = 1.6f;
// The picture's push along its contours at force 1 (see swirlGain), the fluid's curl and the particles' drift.
constexpr float kSwirl = 6.0f;
constexpr float kCurl = 18.0f;
constexpr float kDrift = 0.25f;

float unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

}  // namespace

Plan planFor(const SceneParams& p) {
    Plan plan;
    plan.fluidMix = unit(p.simFluid);
    plan.particleMix = unit(p.simParticles);
    plan.order = unit(p.simLayerOrder);
    plan.force = std::isfinite(p.simForce) ? std::clamp(p.simForce, 0.0f, 2.0f) : 0.0f;
    plan.fluid = plan.fluidMix >= kOn;
    plan.particles = plan.particleMix >= kOn;
    if (plan.fluid) {
        const fluid::quality::Tier& grid = fluid::quality::tier(kGrid);
        plan.simRes = grid.simRes;
        plan.dyeRes = grid.dyeRes;
        plan.pressureIterations = grid.iterations;
    }
    if (plan.particles) plan.particleSide = kSide;
    return plan;
}

float dyeWeight(float dt) {
    if (!(dt > 0.0f)) return 0.0f;
    return 1.0f - std::exp(-kDyeRate * dt);
}

float swirlGain(float force) { return kSwirl * std::max(force, 0.0f); }

float curlStrength(float force) { return kCurl * std::max(force, 0.0f); }

float driftSpeed(float force) { return kDrift * std::max(force, 0.0f); }

std::array<float, kSpawnPoints * 4> spawns(float aspect) {
    const float a = std::isfinite(aspect) && aspect > 0.0f ? aspect : 1.0f;
    std::array<float, kSpawnPoints * 4> out{};
    // A 4 x 2 grid of cell centres, each jittered over most of its cell.
    for (int i = 0; i < kSpawnPoints; ++i) {
        const int column = i % 4;
        const int row = i / 4;
        out[static_cast<size_t>(i * 4 + 0)] = a * (-0.75f + 0.5f * static_cast<float>(column));
        out[static_cast<size_t>(i * 4 + 1)] = -0.5f + 1.0f * static_cast<float>(row);
        out[static_cast<size_t>(i * 4 + 2)] = 1.0f;
        out[static_cast<size_t>(i * 4 + 3)] = 0.45f * std::max(a * 0.5f, 1.0f);
    }
    return out;
}

void Clock::step(float dt) {
    if (!(dt > 0.0f)) return;
    // Wrapped where fluid::Particles wraps its own copy, so the phase keeps its precision over a long play.
    stirTime = std::fmod(stirTime + kStirRate * dt, 6.2831853f * 64.0f);
}

bool Keep::step(bool on) {
    if (on) {
        idle = 0;
        return true;
    }
    if (idle <= kIdleFrames) ++idle;
    return idle <= kIdleFrames;
}

float luma(const Rgb& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

namespace {

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Exact at both ends, so a mix of 0 is its first value and 1 its second, bit for bit.
float mixf(float a, float b, float t) { return a * (1.0f - t) + b * t; }

}  // namespace

Rgb layer(const Rgb& style, const Rgb& dye, float fluidMix, float order) {
    const float m = unit(fluidMix);
    if (m <= 0.0f) return style;
    const float o = unit(order);
    // Behind: the dye shows where the style is dark and fades out where it is lit.
    const float under = 1.0f - smoothstep(0.0f, 0.45f, luma(style));
    const auto one = [&](float s, float d) {
        const float ink = std::clamp(d, 0.0f, 1.0f);
        const float behind = std::min(s + ink * under, 1.0f);
        // The screen, 1 - (1 - s)(1 - ink), written so it never reads darker than s.
        const float over = s + ink * (1.0f - s);
        return std::clamp(mixf(s, mixf(behind, over, o), m), 0.0f, 1.0f);
    };
    return {one(style.r, dye.r), one(style.g, dye.g), one(style.b, dye.b)};
}

Rgb front(const Rgb& base, const Rgb& mote) {
    const auto one = [](float b, float m) {
        const float s = std::clamp(m, 0.0f, 1.0f);
        return std::min(s + b * (1.0f - s), 1.0f);
    };
    return {one(base.r, mote.r), one(base.g, mote.g), one(base.b, mote.b)};
}

}  // namespace geode::viz::physics
