#include "viz/Layers.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

namespace {

float onePole(float x, float target, float dt, float seconds) {
    return x + (target - x) * (1.0f - std::exp(-dt / std::max(seconds, 1e-3f)));
}

}  // namespace

bool Layers::Uniforms::any() const {
    if (fallMix > 0.001f || fractal > 0.001f) return true;
    for (const auto& s : spawn) {
        if (s[2] < 1.0f) return true;
    }
    return false;
}

void Layers::reset() {
    u_ = Uniforms{};
    for (auto& s : u_.spawn) s = {0.5f, 0.5f, 1.0f, 0.0f};
    fallLevel_ = 0.0f;
    growth_ = 0.0f;
    turnPhase_ = 0.0f;
    spawnHigh_ = false;
    sinceSpawn_ = kSpawnGapSeconds;
    nextSlot_ = 0;
    seed_ = 0x6C8E9CF5u;
}

// 0..1, deterministic, so an export spawns in the same places as the screen.
float Layers::nextRandom() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return static_cast<float>(seed_ >> 8) / 16777216.0f;
}

void Layers::step(const SceneParams& p, float dt, bool reducedMotion) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    const auto finite = [](float v) { return std::isfinite(v) ? v : 0.0f; };

    // Fall through: the stack moves at the signed rate, so a route can push it in and another pull it
    // out at once; the layer fades in with |fall| and lingers half a second after it stops.
    const float fall = std::clamp(finite(p.fall), -1.0f, 1.0f);
    const float rate = fall * kFallOctavesPerSecond * (reducedMotion ? 0.5f : 1.0f);
    u_.fallPhase = std::fmod(u_.fallPhase + rate * dt + 1.0f, 1.0f);
    fallLevel_ = onePole(fallLevel_, std::fabs(fall), dt, std::fabs(fall) > fallLevel_ ? 0.15f : 0.5f);
    u_.fallMix = std::clamp(fallLevel_ * 4.0f, 0.0f, 1.0f);

    // Fractal growth: generations grow in over a third of a second and wither over a second, so a hit
    // reads as a burst outward and a sustained level as a standing fractal. Each generation turns a
    // little further the longer it stands.
    const float target = std::clamp(finite(p.fractal), 0.0f, 1.0f) * 4.0f;
    growth_ = onePole(growth_, target, dt, target > growth_ ? 0.35f : 1.0f);
    turnPhase_ = std::fmod(turnPhase_ + dt * 0.15f * growth_, 6.2831853f);
    u_.fractal = growth_;
    u_.fractalTurn = 0.35f + 0.25f * std::sin(turnPhase_);

    // Spawn: each rising edge past 0.5 (re-armed below 0.3) starts the oldest slot over at a new place.
    for (auto& s : u_.spawn) s[2] = std::min(s[2] + dt / kSpawnLifeSeconds, 1.0f);
    sinceSpawn_ += dt;
    const float sp = finite(p.spawn);
    const bool edge = sp >= 0.5f && !spawnHigh_;
    spawnHigh_ = sp >= 0.3f && (sp >= 0.5f || spawnHigh_);
    if (edge && sinceSpawn_ >= kSpawnGapSeconds) {
        sinceSpawn_ = 0.0f;
        auto& s = u_.spawn[static_cast<size_t>(nextSlot_)];
        s = {0.15f + 0.7f * nextRandom(), 0.15f + 0.7f * nextRandom(), 0.0f, nextRandom()};
        nextSlot_ = (nextSlot_ + 1) % kSpawns;
    }
}

}  // namespace geode::viz
