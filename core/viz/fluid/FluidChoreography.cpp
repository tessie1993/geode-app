#include "viz/fluid/FluidChoreography.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid {

namespace {
constexpr float kPi = 3.1415927f;
}

float Choreography::sceneSpeed(float speed) { return std::clamp(speed, 0.05f, 4.0f); }

void Choreography::Anchor::follow(float dt) {
    const float k = 1.0f - std::exp(-dt * kFollowRate);
    float dx = (targetX - x) * k;
    float dy = (targetY - y) * k;
    const float step = std::sqrt(dx * dx + dy * dy);
    const float maxStep = kMaxSpeed * dt;
    if (step > maxStep && step > 1e-6f) {
        const float s = maxStep / step;
        dx *= s;
        dy *= s;
    }
    x += dx;
    y += dy;
}

void Choreography::Anchor::snap() {
    x = targetX;
    y = targetY;
}

void Choreography::tick(const GeodeFeatureFrame& f, float dt, float aspect) {
    time_ = std::fmod(time_ + dt * (0.4f + 0.6f * speed), kTimeWrapSeconds);
    if (hitEdge_.step(f)) hitCount_++;
    beatEnv_ = std::max(live::hit(f), beatEnv_ * std::exp(-dt / 0.35f));
    const float bassTarget = std::clamp(f.bass * 1.2f, 0.0f, 1.0f);
    bassEnv_ += (bassTarget - bassEnv_) * std::min(bassTarget > bassEnv_ ? dt / 0.03f : dt / 0.45f, 1.0f);

    traverse_.step(f, dt);
    if (traverse_.sectionCount() != lastSection_) {
        lastSection_ = traverse_.sectionCount();
        sectionPhase_ = static_cast<float>(traverse_.sectionCount()) * kGoldenAngle;
    }

    const float progress = traverse_.position() * std::clamp(progressionAmount, 0.0f, 1.0f);
    const float ax = std::min(aspect, 1.6f) * kDomainMargin;

    const int nS = std::clamp(spawnCount, 1, kMaxSpawn);
    for (int i = 0; i < nS; ++i) {
        const auto [tx, ty] = spawnTarget(i, nS, progress, ax);
        auto& s = spawns_[static_cast<size_t>(i)];
        s.targetX = std::clamp(tx, -ax, ax);
        s.targetY = std::clamp(ty, -kDomainMargin, kDomainMargin);
        const float bandE = std::clamp(f.bands[std::clamp(i * GEODE_BAND_COUNT / nS, 0, GEODE_BAND_COUNT - 1)], 0.0f, 1.0f);
        s.energy = std::clamp(0.5f * beatEnv_ + 0.5f * bandE, 0.0f, 1.0f);
    }
    const int nC = std::clamp(catchCount, 0, kMaxCatch);
    for (int i = 0; i < nC; ++i) {
        const auto [tx, ty] = catchTarget(i, nC, progress, ax);
        auto& c = catches_[static_cast<size_t>(i)];
        c.targetX = std::clamp(tx, -ax, ax);
        c.targetY = std::clamp(ty, -kDomainMargin, kDomainMargin);
        c.energy = bassEnv_;
    }

    if (!initialized_) {
        initialized_ = true;
        for (auto& s : spawns_) s.snap();
        for (auto& c : catches_) c.snap();
    } else {
        for (auto& s : spawns_) s.follow(dt);
        for (auto& c : catches_) c.follow(dt);
    }
}

void Choreography::reset() {
    initialized_ = false;
    time_ = 0.0f;
    hitCount_ = 0;
    lastSection_ = -1;
    sectionPhase_ = 0.0f;
    beatEnv_ = 0.0f;
    bassEnv_ = 0.0f;
    hitEdge_.reset();
    traverse_.reset();
}

std::pair<float, float> Choreography::spawnTarget(int i, int n, float progress, float ax) const {
    const float frac = static_cast<float>(i) / static_cast<float>(n);
    const float precession = sectionPhase_ + progress * kPi + time_ * 0.13f;
    const float journeyR = 0.35f + 0.4f * std::sin(progress * kPi);
    const float cy = (progress - 0.5f) * 0.7f;
    switch (path) {
        case kPathOrbit: {
            const float a = frac * 2.0f * kPi + precession;
            return {std::cos(a) * journeyR * ax, cy + std::sin(a) * journeyR};
        }
        case kPathRose: {
            const float k = 2.0f + 3.0f * progress;
            const float theta = frac * 2.0f * kPi + precession;
            const float r = journeyR * (0.35f + 0.65f * std::fabs(std::cos(k * theta)));
            return {std::cos(theta) * r * ax, cy + std::sin(theta) * r};
        }
        case kPathBloom: {
            const float idx = static_cast<float>(hitCount_ + i);
            const float a = idx * kGoldenAngle + sectionPhase_;
            const float r = journeyR * std::sqrt((std::fmod(idx, 24.0f) + 1.0f) / 24.0f);
            return {std::cos(a) * r * ax, cy + std::sin(a) * r};
        }
        case kPathDrift: {
            const float s = static_cast<float>(i) * 3.7f + sectionPhase_;
            const float t = time_ * 0.31f + progress * 5.0f;
            const float x = 0.7f * std::sin(t * 0.83f + s) * std::sin(t * 0.19f + s * 1.7f);
            const float y = 0.7f * std::sin(t * 0.67f + s * 2.3f) * std::sin(t * 0.23f + s);
            return {x * ax, cy * 0.5f + y * 0.8f};
        }
        default: {
            const float a = 3.0f + 2.0f * progress;
            const float b = 2.0f + 2.0f * progress;
            const float ph = frac * 2.0f * kPi + precession;
            return {std::sin(a * time_ * 0.21f + ph) * journeyR * ax, cy + std::sin(b * time_ * 0.21f + ph * 1.5f + 1.1f) * journeyR};
        }
    }
}

std::pair<float, float> Choreography::catchTarget(int i, int n, float progress, float ax) const {
    const float frac = static_cast<float>(i) / static_cast<float>(n);
    const float a = frac * 2.0f * kPi + sectionPhase_ + kPi / static_cast<float>(n) - time_ * 0.09f;
    const float r = std::max(0.62f - 0.4f * progress, 0.12f);
    const float cy = (0.5f - progress) * 0.5f;
    return {std::cos(a) * r * ax, cy + std::sin(a) * r * 0.85f};
}

void Choreography::packSpawns(float* out) const {
    const int n = std::clamp(spawnCount, 1, kMaxSpawn);
    for (int i = 0; i < kMaxSpawn; ++i) {
        float* o = out + i * 4;
        if (i < n) {
            const auto& s = spawns_[static_cast<size_t>(i)];
            o[0] = s.x;
            o[1] = s.y;
            o[2] = 0.4f + 0.6f * s.energy;
            o[3] = 0.05f + 0.10f * s.energy;
        } else {
            o[0] = o[1] = o[2] = o[3] = 0.0f;
        }
    }
}

void Choreography::packCatches(float* out, float pull, float captureRadius) const {
    const int n = std::clamp(catchCount, 0, kMaxCatch);
    for (int i = 0; i < kMaxCatch; ++i) {
        float* o = out + i * 4;
        if (i < n) {
            const auto& c = catches_[static_cast<size_t>(i)];
            o[0] = c.x;
            o[1] = c.y;
            o[2] = pull * (0.5f + 0.9f * c.energy);
            o[3] = captureRadius;
        } else {
            o[0] = o[1] = o[2] = o[3] = 0.0f;
        }
    }
}

}  // namespace geode::viz::fluid
