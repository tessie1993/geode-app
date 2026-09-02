#include "viz/fluid/FluidEmitters.hpp"

#include <algorithm>
#include <cmath>

#include "viz/scenes/SceneCommon.hpp"

namespace geode::viz::fluid {

namespace {

constexpr float kPi = 3.1415927f;

Splat capsule(float px, float py, float cx, float cy, float radius, float vx, float vy, float r, float g, float b, float dyeGain) {
    return {px, py, cx, cy, radius, vx, vy, r * dyeGain, g * dyeGain, b * dyeGain};
}

}  // namespace

void Emitters::applyParams(const SceneParams& p) {
    beatPattern = std::clamp(p.fluidBeatPattern, 0, 3);
    beatSplats = std::clamp(p.fluidBeatSplats, 0, 8);
    stirrers = std::clamp(p.fluidStirrers, 0, 4);
    stirrerSpeed = std::clamp(p.fluidStirrerSpeed, 0.0f, 2.0f) * Choreography::sceneSpeed(p.speed);
    bassPump = p.fluidBassPump;
    sparkle = p.fluidSparkle;
    splatRadius = std::clamp(p.fluidSplatRadius, 0.02f, 0.4f);
    radiusPulse = std::clamp(p.fluidRadiusPulse, 0.0f, 1.0f);
    catchSuction = std::clamp(p.fluidCatchPull, 0.0f, 3.0f);
    beatResponse = p.beatResponse;
}

void Emitters::tick(const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan, std::vector<Splat>& out) {
    out.clear();
    beatEnvRaw_ = std::max(live::hit(f), beatEnvRaw_ * std::exp(-dt / 0.3f));
    beatEnv_ = beatEnvRaw_ * std::clamp(beatResponse, 0.0f, 2.0f);
    const float bassTarget = std::clamp(f.bass * 1.2f, 0.0f, 1.0f);
    bassEnv_ += (bassTarget - bassEnv_) * std::min(bassTarget > bassEnv_ ? dt / 0.03f : dt / 0.4f, 1.0f);
    trebleMean_ += (f.treble - trebleMean_) * std::min(dt / 0.32f, 1.0f);
    palettePhase_ = std::fmod(palettePhase_ + dt * paletteCycleSpeed * 0.05f, 1.0f);
    suctionPhase_ = std::fmod(suctionPhase_ + dt, kPhaseWrapSeconds);

    const float radius = splatRadius * std::min(1.0f + radiusPulse * beatEnv_, kMaxRadiusSwell);
    const float speed = kBaseSpeed * forceScale * (0.4f + 1.6f * f.bass) * (0.3f + 0.7f * beatEnv_);
    const bool beatEdge = hitEdge_.step(f);

    if (beatEdge && beatSplats > 0 && beatResponse > kBeatResponseGate) beatSplatsFor(out, f, aspect, baseHue, hueSpan, radius, speed);
    stirrerSplats(out, f, dt, aspect, baseHue, hueSpan, radius);
    suctionSplats(out, radius);
    if (sparkle && f.treble > trebleMean_ * 1.6f && f.treble > 0.08f) sparkleSplats(out, aspect, baseHue, hueSpan, radius);
    if (bassPump && bassEnv_ > 0.15f) pumpSplats(out, baseHue, hueSpan, radius);
    if (out.size() > static_cast<size_t>(kMaxSplatsPerFrame)) out.resize(static_cast<size_t>(kMaxSplatsPerFrame));
}

void Emitters::anchor(int i, float aspect) {
    if (choreography) {
        const int n = std::clamp(choreography->spawnCount, 1, Choreography::kMaxSpawn);
        const auto& a = choreography->spawns()[static_cast<size_t>(i % n)];
        anchorX_ = a.x;
        anchorY_ = a.y;
        return;
    }
    const float ang = suctionPhase_ * 0.4f + static_cast<float>(i) * 2.1f;
    anchorX_ = std::cos(ang) * 0.45f * std::min(aspect, 1.4f);
    anchorY_ = std::sin(ang) * 0.45f;
}

void Emitters::stirrerSplats(std::vector<Splat>& out, const GeodeFeatureFrame& f, float dt, float aspect, float baseHue, float hueSpan,
                             float radius) {
    const int n = std::clamp(stirrers, 0, 4);
    if (n != activeStirrers_) {
        stirrerHasPrev_.fill(false);
        activeStirrers_ = n;
    }
    if (n == 0) return;
    const std::array<float, 4> bands = {f.bass, f.mid, f.treble, f.rms};
    for (int i = 0; i < n; ++i) {
        const size_t s = static_cast<size_t>(i);
        const float band = bands[s % bands.size()];
        anchor(i, aspect);
        const float cxA = anchorX_;
        const float cyA = anchorY_;
        const float orbitR = 0.14f + 0.10f * static_cast<float>(i % 3);
        stirrerAngle_[s] = std::fmod(stirrerAngle_[s] + dt * stirrerSpeed * (0.3f + band * 1.7f) * (i % 2 == 0 ? 1.0f : -1.0f), kTwoPi);
        const float x = cxA + std::cos(stirrerAngle_[s]) * orbitR;
        const float y = cyA + std::sin(stirrerAngle_[s]) * orbitR;
        if (stirrerHasPrev_[s]) {
            const float px = stirrerPrevX_[s];
            const float py = stirrerPrevY_[s];
            const float invDt = 1.0f / std::max(dt, 1e-3f);
            hue::hsv(std::fmod(baseHue + palettePhase_ + static_cast<float>(i) * hueSpan / 4.0f, 1.0f), 0.85f, 1.0f, rgb_.data());
            const float amp = 0.1f + 0.55f * band;
            out.push_back({px, py, x, y, radius, (x - px) * invDt * 0.22f * forceScale, (y - py) * invDt * 0.22f * forceScale,
                           rgb_[0] * amp, rgb_[1] * amp, rgb_[2] * amp});
        }
        stirrerPrevX_[s] = x;
        stirrerPrevY_[s] = y;
        stirrerHasPrev_[s] = true;
    }
}

void Emitters::beatSplatsFor(std::vector<Splat>& out, const GeodeFeatureFrame& f, float aspect, float baseHue, float hueSpan, float radius,
                             float speed) {
    const int n = std::clamp(beatSplats, 1, 8);
    const float dyeGain = 1.5f * (0.15f + 0.85f * beatEnv_);
    for (int i = 0; i < n; ++i) {
        const float frac = static_cast<float>(i) / static_cast<float>(n);
        hue::hsv(std::fmod(baseHue + palettePhase_ + frac * hueSpan, 1.0f), 0.9f, 1.0f, rgb_.data());
        const float cr = rgb_[0];
        const float cg = rgb_[1];
        const float cb = rgb_[2];
        anchor(i, aspect);
        const float ax = anchorX_;
        const float ay = anchorY_;
        switch (beatPattern) {
            case kPatternCenter: {
                const float a = frac * 2.0f * kPi + palettePhase_ * 6.0f;
                out.push_back(capsule(ax, ay, ax + std::cos(a) * 0.06f, ay + std::sin(a) * 0.06f, radius, std::cos(a) * speed,
                                      std::sin(a) * speed, cr, cg, cb, dyeGain));
                break;
            }
            case kPatternRandom: {
                const float x = ax + (nextFloat() * 2.0f - 1.0f) * 0.25f;
                const float y = ay + (nextFloat() * 2.0f - 1.0f) * 0.25f;
                const float a = nextFloat() * 2.0f * kPi;
                out.push_back(capsule(x, y, x + std::cos(a) * 0.05f, y + std::sin(a) * 0.05f, radius, std::cos(a) * speed,
                                      std::sin(a) * speed, cr, cg, cb, dyeGain));
                break;
            }
            case kPatternSpectrumArc: {
                const int bandIdx = std::clamp(static_cast<int>(frac * (GEODE_BAND_COUNT - 1)), 0, GEODE_BAND_COUNT - 1);
                const float bandE = std::clamp(f.bands[bandIdx], 0.0f, 1.5f);
                const float x = (frac * 2.0f - 1.0f) * 0.7f * aspect;
                const float y = std::clamp(ay * 0.35f - 0.6f, -0.9f, -0.35f);
                const float v = speed * (0.4f + 1.6f * bandE) / std::max(0.4f + 1.6f * f.bass, 0.4f);
                out.push_back(capsule(x, y, x, y + 0.06f, radius, 0.0f, v, cr, cg, cb, dyeGain * (0.4f + bandE)));
                break;
            }
            default: {
                const float a = frac * 2.0f * kPi + palettePhase_ * 6.0f;
                const float ringR = 0.16f;
                const float x = ax + std::cos(a) * ringR;
                const float y = ay + std::sin(a) * ringR;
                const float tx = -std::sin(a);
                const float ty = std::cos(a);
                out.push_back(capsule(x - tx * 0.04f, y - ty * 0.04f, x + tx * 0.04f, y + ty * 0.04f, radius, tx * speed, ty * speed, cr,
                                      cg, cb, dyeGain));
                break;
            }
        }
    }
}

void Emitters::suctionSplats(std::vector<Splat>& out, float radius) {
    if (!choreography || catchSuction <= 0.0f) return;
    const int n = std::clamp(choreography->catchCount, 0, Choreography::kMaxCatch);
    if (n == 0) return;
    suctionIndex_ = (suctionIndex_ + 1) % n;
    const auto& a = choreography->catches()[static_cast<size_t>(suctionIndex_)];
    const float strength = kBaseSpeed * 0.7f * catchSuction * (0.35f + 0.65f * bassEnv_);
    const float ang = suctionPhase_ * 2.7f + static_cast<float>(suctionIndex_) * 2.1f;
    const float ox = std::cos(ang) * 0.18f;
    const float oy = std::sin(ang) * 0.18f;
    const float len = std::max(std::sqrt(ox * ox + oy * oy), 1e-4f);
    out.push_back({a.x + ox, a.y + oy, a.x, a.y, radius * 0.8f, -ox / len * strength, -oy / len * strength, 0.0f, 0.0f, 0.0f});
}

void Emitters::sparkleSplats(std::vector<Splat>& out, float aspect, float baseHue, float hueSpan, float radius) {
    const int count = 1 + nextInt(2);
    for (int k = 0; k < count; ++k) {
        anchor(nextInt(8), aspect);
        const float x = anchorX_ + (nextFloat() * 2.0f - 1.0f) * 0.3f;
        const float y = anchorY_ + (nextFloat() * 2.0f - 1.0f) * 0.3f;
        const float a = nextFloat() * 2.0f * kPi;
        hue::hsv(std::fmod(baseHue + palettePhase_ + hueSpan * 0.5f, 1.0f), 0.35f, 1.0f, rgb_.data());
        out.push_back(capsule(x, y, x + std::cos(a) * 0.02f, y + std::sin(a) * 0.02f, radius * 0.35f, std::cos(a) * kBaseSpeed * 0.5f,
                              std::sin(a) * kBaseSpeed * 0.5f, rgb_[0], rgb_[1], rgb_[2], 0.9f));
    }
}

void Emitters::pumpSplats(std::vector<Splat>& out, float baseHue, float hueSpan, float radius) {
    const float v = kBaseSpeed * forceScale * bassEnv_;
    anchor(0, 1.0f);
    const float ax = anchorX_;
    const float ay = anchorY_;
    for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) / 6.0f * 2.0f * kPi;
        hue::hsv(std::fmod(baseHue + palettePhase_ + static_cast<float>(i) * hueSpan / 6.0f, 1.0f), 0.95f, 1.0f, rgb_.data());
        out.push_back(capsule(ax + std::cos(a) * 0.06f, ay + std::sin(a) * 0.06f, ax + std::cos(a) * 0.14f, ay + std::sin(a) * 0.14f, radius,
                              std::cos(a) * v, std::sin(a) * v, rgb_[0], rgb_[1], rgb_[2], 0.3f + 0.9f * bassEnv_));
    }
}

}  // namespace geode::viz::fluid
