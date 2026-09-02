#include "viz/fluid/RippleMath.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid::ripple {

namespace {
constexpr float kStrokeReferenceSpeed = 1.6f;
constexpr float kGoldenAngle = 2.3999631f;
constexpr double kGoldenFract = 0.6180339887;
}  // namespace

float heightDecayPerSubstep(float damping, float subDt) {
    const float per60 = 1.0f - (1.0f - std::clamp(damping, 0.0f, 1.0f)) * kHeightDecayRatio;
    return std::pow(per60, subDt * 60.0f);
}

float dropProfile(float dist, float radius, float amp) {
    const float r = std::max(radius, 1e-4f);
    return amp * std::exp(-(dist * dist) / (r * r));
}

float cflClampedDt(float c, float dt, float dx) {
    if (c <= 1e-6f) return dt;
    return std::min(dt, 0.7f * dx / c);
}

std::pair<float, float> refractionOffset(float hL, float hR, float hT, float hB, float strength) {
    float ox = (hR - hL) * strength;
    float oy = (hT - hB) * strength;
    const float len = std::sqrt(ox * ox + oy * oy);
    const float k = kRefractionCap / (kRefractionCap + len);
    return {ox * k, oy * k};
}

float inkDissipation(float dissipation, float dt) { return std::clamp(1.0f - std::clamp(dissipation, 0.0f, 8.0f) * dt, 0.0f, 1.0f); }

std::vector<StrokeDrop> strokeDrops(float x, float y, float dx, float dy, float dt, float radius, float strength) {
    const float step = std::sqrt(dx * dx + dy * dy);
    const float speed = dt > 1e-4f ? step / dt : 0.0f;
    const float drive = (0.25f + std::clamp(speed / kStrokeReferenceSpeed, 0.0f, 1.5f)) * std::clamp(strength, 0.0f, 2.0f);
    if (drive <= 1e-4f) return {};
    const float ux = step > 1e-5f ? dx / step : 0.0f;
    const float uy = step > 1e-5f ? dy / step : 0.0f;
    const float lead = radius * 0.6f;
    std::vector<StrokeDrop> out;
    out.push_back({x + ux * lead, y + uy * lead, radius, drive});
    if (step > 1e-5f) out.push_back({x - ux * lead, y - uy * lead, radius, -drive * 0.8f});
    return out;
}

std::pair<float, float> overlayDropPosition(int index, float aspect) {
    const int n = std::max(index, 0);
    const float angle = static_cast<float>(n) * kGoldenAngle;
    const float radius = 0.85f * std::sqrt(static_cast<float>(std::fmod(n * kGoldenFract, 1.0)));
    return {std::cos(angle) * radius * aspect, std::sin(angle) * radius};
}

void OverlayDrops::reset() {
    frame_ = 0;
    dropIndex_ = 0;
    hitEdge_.reset();
}

void OverlayDrops::tick(const GeodeFeatureFrame& features, float aspect, const Queue& queue) {
    frame_++;
    const float hit = live::hit(features);
    if (hitEdge_.step(features)) {
        const float amp = (0.22f + 0.4f * std::clamp(features.bass, 0.0f, 1.5f)) * hit;
        for (int i = 0; i < kBeatDrops; ++i) {
            const auto [x, y] = overlayDropPosition(dropIndex_++, aspect);
            queue(x, y, 0.055f, amp);
        }
    }
    if (features.treble > kSparkleThreshold && frame_ % kSparkleInterval == 0) {
        const auto [x, y] = overlayDropPosition(dropIndex_++, aspect);
        queue(x, y, 0.03f, 0.1f * std::min(features.treble, 2.0f));
    }
}

}  // namespace geode::viz::fluid::ripple
