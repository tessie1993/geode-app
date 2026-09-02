#include "viz/fluid/FluidMath.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid {

namespace math {

namespace {

float fract(float x) { return x - std::floor(x); }

float hash3(float x0, float y0, float z0) {
    const float x = fract(x0 * 0.3183099f + 0.1f) * 17.0f;
    const float y = fract(y0 * 0.3183099f + 0.2f) * 17.0f;
    const float z = fract(z0 * 0.3183099f + 0.3f) * 17.0f;
    return fract(x * y * z * (x + y + z));
}

float mix(float a, float b, float t) { return a + (b - a) * t; }

float vnoise3(float px, float py, float pz) {
    const float ix = std::floor(px);
    const float iy = std::floor(py);
    const float iz = std::floor(pz);
    float fx = px - ix;
    float fy = py - iy;
    float fz = pz - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const auto n = [&](float dx, float dy, float dz) { return hash3(ix + dx, iy + dy, iz + dz); };
    return mix(mix(mix(n(0, 0, 0), n(1, 0, 0), fx), mix(n(0, 1, 0), n(1, 1, 0), fx), fy),
               mix(mix(n(0, 0, 1), n(1, 0, 1), fx), mix(n(0, 1, 1), n(1, 1, 1), fx), fy), fz);
}

float psi(float x, float y, float time, float freq, float detail) {
    float v = vnoise3(x * freq, y * freq, time) * 0.625f;
    v += vnoise3(x * freq * 2.02f + 11.3f, y * freq * 2.02f + 11.3f, time * 2.02f + 11.3f) * 0.25f;
    v += vnoise3(x * freq * 4.05f + 29.7f, y * freq * 4.05f + 29.7f, time * 4.05f + 29.7f) * 0.125f * detail;
    return v;
}

}  // namespace

float driven(float value, float audioDrive) {
    const float d = std::clamp(audioDrive, kMinAudioDrive, kMaxAudioDrive);
    return std::clamp(value * d, 0.0f, std::max(value, kDriveCeiling));
}

std::pair<float, float> curlVelocity(float x, float y, float time, float freq, float detail) {
    const float e = 0.02f;
    const float dpdx = psi(x + e, y, time, freq, detail) - psi(x - e, y, time, freq, detail);
    const float dpdy = psi(x, y + e, time, freq, detail) - psi(x, y - e, time, freq, detail);
    return {dpdy / (2.0f * e), -dpdx / (2.0f * e)};
}

float confinementDeltaV(float curlStrength, float dx, float velDiff, float dt) {
    const float omega = (0.5f / dx) * velDiff;
    return curlStrength * dx * omega * dt;
}

std::pair<float, float> softLimitFlow(float x, float y) {
    const float len = std::sqrt(x * x + y * y);
    const float k = 6.0f / (6.0f + len);
    return {x * k, y * k};
}

std::pair<float, float> terminalSpeedCap(float x, float y) {
    const float sp = std::sqrt(x * x + y * y);
    const float k = 12.0f / std::max(12.0f, sp);
    return {x * k, y * k};
}

int stateSide(int count) { return std::max(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))), 2); }

float attractorForce(float pull, float dist2) {
    const float f = pull / (dist2 + 0.05f);
    return f * 6.0f / (6.0f + f);
}

bool isCaptured(float px, float py, float cx, float cy, float captureRadius) {
    const float dx = cx - px;
    const float dy = cy - py;
    return dx * dx + dy * dy < captureRadius * captureRadius;
}

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

float bloomPrefilterScale(float br, float threshold, float softKnee) {
    const float knee = threshold * softKnee + 1e-4f;
    const float cx = threshold - knee;
    const float cy = knee * 2.0f;
    const float cz = 0.25f / knee;
    float rq = std::clamp(br - cx, 0.0f, cy);
    rq = cz * rq * rq;
    return std::max(rq, br - threshold) / std::max(br, 1e-4f);
}

}  // namespace math

GeodeFeatureFrame scaledFeatures(const GeodeFeatureFrame& features, float audioDrive) {
    const float d = std::clamp(audioDrive, math::kMinAudioDrive, math::kMaxAudioDrive);
    if (d == 1.0f) return features;
    GeodeFeatureFrame out = features;
    for (float& band : out.bands) band = math::driven(band, d);
    out.rms = math::driven(features.rms, d);
    out.bass = math::driven(features.bass, d);
    out.mid = math::driven(features.mid, d);
    out.treble = math::driven(features.treble, d);
    return out;
}

namespace water {

bool isCatchWell(float r, float g, float b) { return std::max({r, g, b}) <= 0.0f; }

float catchWellRadius(float catchRadius) { return std::clamp(catchRadius, kMinCatchRadius, kMaxCatchRadius); }

float catchWellAmplitude(float speed, float catchRadius, float rippleStrength) {
    const float r = catchWellRadius(catchRadius);
    const float spread = std::clamp(kRefCatchRadius / r, 0.4f, 2.5f);
    return -(0.06f + 0.5f * std::clamp(speed, 0.0f, 2.0f)) * spread * std::clamp(rippleStrength, 0.0f, 2.0f);
}

}  // namespace water

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
