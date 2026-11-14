#include "viz/present/FieldGeometry.hpp"

#include <algorithm>
#include <cmath>

#include "viz/CameraRig.hpp"

namespace geode::viz::present {

namespace {

constexpr float kTau = 6.2831853f;
constexpr float kPi = 3.14159265f;
// The torus trace for a finger: plenty for one ray a frame, never a pixel's budget.
constexpr int kTorusSteps = 128;

using Vec3 = std::array<float, 3>;

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
float wrap01(float v) { return std::isfinite(v) ? v - std::floor(v) : 0.0f; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }

Vec3 normalized(const Vec3& v, const Vec3& fallback) {
    const float l = std::sqrt(dot(v, v));
    return l > 1e-9f && std::isfinite(l) ? scale(v, 1.0f / l) : fallback;
}

// lib_camera's camAround turn about +y.
Vec3 turnY(const Vec3& v, float a) {
    const float c = std::cos(a);
    const float s = std::sin(a);
    return {c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]};
}

// lib_field3d's fieldRise(): the object seen from kObjectRise above the rig's horizon.
Vec3 rise(const Vec3& v) {
    const float c = std::cos(kObjectRise);
    const float s = std::sin(kObjectRise);
    return {v[0], c * v[1] - s * v[2], s * v[1] + c * v[2]};
}

float longitude(const Vec3& p) {
    const bool axis = std::fabs(p[0]) + std::fabs(p[2]) < 1e-12f;
    return axis ? 0.5f : std::atan2(p[2], p[0]) / kTau + 0.5f;
}

}  // namespace

Geometry geometryFor(Surface surface, float depth, int longTexels, int shortTexels, float aspect) {
    const float d = std::clamp(finiteOr(depth, 1.0f), 0.1f, 3.0f);
    const float lo = static_cast<float>(std::max(std::min(longTexels, shortTexels), 1));
    const float hi = static_cast<float>(std::max(std::max(longTexels, shortTexels), 1));
    const float ratio = hi / lo;
    const float a = std::clamp(finiteOr(aspect, 1.0f), 0.1f, 10.0f);
    Geometry g;
    g.surface = surface;
    switch (surface) {
        case Surface::Torus: {
            g.radius = 1.0f;
            // Square texels want the tube's girth to the ring's as the short side to the long; a square
            // field would make a horn torus, so it is held to a ring with a hole.
            g.tube = std::clamp(1.0f / ratio, 0.34f, 0.6f);
            g.tileS = kTau * g.radius;
            g.tileT = kTau * g.tube;
            g.relief = kTorusRelief * d;
            break;
        }
        case Surface::Sphere: {
            g.radius = 1.0f;
            g.tileS = kTau * g.radius;  // the equator
            g.tileT = kPi * g.radius;   // pole to pole
            g.relief = kSphereRelief * d;
            break;
        }
        case Surface::Terrain:
        default: {
            g.surface = Surface::Terrain;
            g.tileT = kTerrainShort;
            // A whole number of tiles in the rig's travel wrap, so the wrap lands on a tile edge.
            const float n = std::max(std::round(kTravelWrap / (kTerrainShort * ratio)), 1.0f);
            g.tileS = kTravelWrap / n;
            g.relief = kTerrainRelief * d;
            break;
        }
    }
    g.top = g.relief * kReliefCeiling;
    if (g.surface == Surface::Terrain) {
        // fieldEye() keeps kTerrainBob of a wander held to kTerrainWander, so the eye never comes nearer
        // the highest relief than the clearance.
        g.eye = g.top + kTerrainClearance + kTerrainBob * kTerrainWander;
    } else {
        // Framed on the screen's short side at the FOV dial's default: view() spans 1 up and aspect across,
        // so the silhouette's half angle (outer / sqrt(eye^2 - outer^2)) is the short side over the focal,
        // shrunk by kObjectFrame.
        const float outer = g.radius + g.tube + g.top;
        const float k = kObjectFrame * kBaseFocal / std::min(a, 1.0f);
        g.eye = outer * std::sqrt(1.0f + k * k);
    }
    return g;
}

Ray eyeRay(const Geometry& g, const CameraPose& pose, float x, float y) {
    const float focal = std::clamp(finiteOr(pose.focal, kBaseFocal), 0.1f, 10.0f);
    Ray r;
    if (g.surface == Surface::Terrain) {
        const float v = y - kTerrainTip;
        const Vec3 d = add(add(scale(pose.right, x), scale(pose.up, v)), scale(pose.fwd, focal));
        r.dir = normalized(d, pose.fwd);
        r.origin = pose.pos;
        r.origin[1] = g.eye + kTerrainBob * std::clamp(finiteOr(pose.pos[1], 0.0f), -kTerrainWander, kTerrainWander);
        return r;
    }
    const float turn = finiteOr(pose.travel, 0.0f) * kObjectSpin;
    const Vec3 b0 = turnY(pose.right, turn);
    const Vec3 b1 = turnY(pose.up, turn);
    const Vec3 b2 = turnY(pose.fwd, turn);
    const float dolly = std::clamp(finiteOr(pose.dolly, 0.0f), -1.0f, 1.0f);
    const Vec3 o = scale(b2, -g.eye * (1.0f - 0.35f * dolly));
    const Vec3 d = normalized(add(add(scale(b0, x), scale(b1, y)), scale(b2, focal)), b2);
    r.origin = rise(o);
    r.dir = rise(d);
    return r;
}

std::optional<std::array<float, 2>> fieldPoint(const Geometry& g, const Ray& ray) {
    const Vec3& o = ray.origin;
    const Vec3& d = ray.dir;
    for (float v : o) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    for (float v : d) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    const float rest = 0.5f * g.relief;
    switch (g.surface) {
        case Surface::Terrain: {
            if (d[1] > -1e-4f) return std::nullopt;
            const float t = (o[1] - rest) / -d[1];
            if (t < 0.0f || t > kTerrainFar) return std::nullopt;
            const Vec3 p = add(o, scale(d, t));
            return std::array<float, 2>{wrap01(p[2] / g.tileS), wrap01(p[0] / g.tileT)};
        }
        case Surface::Sphere: {
            const float r = g.radius + rest;
            const float b = dot(o, d);
            const float c = dot(o, o) - r * r;
            const float disc = b * b - c;
            if (disc < 0.0f) return std::nullopt;
            const float t = -b - std::sqrt(disc);
            if (t < 0.0f) return std::nullopt;
            const Vec3 p = add(o, scale(d, t));
            const float len = std::max(std::sqrt(dot(p, p)), 1e-6f);
            const float colat = std::acos(std::clamp(p[1] / len, -1.0f, 1.0f));
            return std::array<float, 2>{wrap01(longitude(p)), wrap01(colat / kPi)};
        }
        case Surface::Torus: {
            const float tube = g.tube + rest;
            const float far = g.eye + 2.0f * (g.radius + g.tube + g.top);
            float t = 0.0f;
            for (int i = 0; i < kTorusSteps && t < far; ++i) {
                const Vec3 p = add(o, scale(d, t));
                const float ring = std::sqrt(p[0] * p[0] + p[2] * p[2]) - g.radius;
                const float dist = std::sqrt(ring * ring + p[1] * p[1]) - tube;
                if (dist < 1e-4f) {
                    const float minor = std::fabs(ring) + std::fabs(p[1]) < 1e-12f ? 0.5f : std::atan2(p[1], ring) / kTau + 0.5f;
                    return std::array<float, 2>{wrap01(longitude(p)), wrap01(minor)};
                }
                t += dist;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::array<float, 2> stateUv(float s, float t, int stateW, int stateH, float fracX, float fracY) {
    const float w = static_cast<float>(std::max(stateW, 1));
    const float h = static_cast<float>(std::max(stateH, 1));
    const bool tall = stateH > stateW;
    const float u = tall ? t : s;
    const float v = tall ? s : t;
    return {wrap01(u - finiteOr(fracX, 0.0f) / w), wrap01(v - finiteOr(fracY, 0.0f) / h)};
}

}  // namespace geode::viz::present
