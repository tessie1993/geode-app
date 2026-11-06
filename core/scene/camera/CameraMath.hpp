#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// The scene camera's maths: 3-vectors and unit quaternions, header-only and pure. Textbook forms only
// (Shoemake's slerp, Shepperd's matrix-to-quaternion, Rodrigues), written for this camera.
//
// Conventions, matching viz/CameraRig: a camera's local axes are X = right, Y = up, Z = forward, a
// right-handed frame (right × up = forward). An orientation q maps local to world, so a camera's world
// forward is rotate(q, {0, 0, 1}). Turning about one of the camera's own axes multiplies on the right:
// q * axisAngle(localAxis, a). Turning about a world axis multiplies on the left.

namespace geode::scene::camera {

using Vec3 = std::array<float, 3>;

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;

inline Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 scale(const Vec3& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }
inline float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a, const Vec3& fallback = {0.0f, 0.0f, 1.0f}) {
    const float l = length(a);
    return l > 1e-6f ? scale(a, 1.0f / l) : fallback;
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return add(a, scale(sub(b, a), t)); }
inline bool finite(const Vec3& a) { return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]); }

// Wraps an angle into [-pi, pi).
inline float wrapAngle(float a) {
    if (!std::isfinite(a)) return 0.0f;
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f) a += kTwoPi;
    return a - kPi;
}

struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline float qdot(const Quat& a, const Quat& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
inline Quat qnormalize(const Quat& q) {
    const float l = std::sqrt(qdot(q, q));
    if (!(l > 1e-8f) || !std::isfinite(l)) return Quat{};
    return {q.w / l, q.x / l, q.y / l, q.z / l};
}
// A right-handed turn of `angle` radians about the unit axis k.
inline Quat axisAngle(const Vec3& k, float angle) {
    const float h = 0.5f * angle;
    const float s = std::sin(h);
    return {std::cos(h), k[0] * s, k[1] * s, k[2] * s};
}
inline Vec3 rotate(const Quat& q, const Vec3& v) {
    // v + 2w(u × v) + 2u × (u × v), u the vector part.
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = scale(cross(u, v), 2.0f);
    return add(add(v, scale(t, q.w)), cross(u, t));
}
inline Vec3 rightOf(const Quat& q) { return rotate(q, {1.0f, 0.0f, 0.0f}); }
inline Vec3 upOf(const Quat& q) { return rotate(q, {0.0f, 1.0f, 0.0f}); }
inline Vec3 fwdOf(const Quat& q) { return rotate(q, {0.0f, 0.0f, 1.0f}); }

// The orientation whose local X, Y, Z are these world axes (an orthonormal right-handed basis).
// Shepperd's method: take the largest of the four diagonal combinations, so it never divides by ~0.
inline Quat fromBasis(const Vec3& r, const Vec3& u, const Vec3& f) {
    // Rotation matrix columns are r, u, f: m[row][col].
    const float m00 = r[0], m10 = r[1], m20 = r[2];
    const float m01 = u[0], m11 = u[1], m21 = u[2];
    const float m02 = f[0], m12 = f[1], m22 = f[2];
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const float s = 2.0f * std::sqrt(trace + 1.0f);
        q = {0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = 2.0f * std::sqrt(std::max(1.0f + m00 - m11 - m22, 1e-12f));
        q = {(m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const float s = 2.0f * std::sqrt(std::max(1.0f + m11 - m00 - m22, 1e-12f));
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s};
    } else {
        const float s = 2.0f * std::sqrt(std::max(1.0f + m22 - m00 - m11, 1e-12f));
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s};
    }
    return qnormalize(q);
}

// The angle of the turn from a to b, 0..pi (q and -q are the same turn).
// The atan2 form of the relative turn, exact for tiny angles where acos(dot) loses them in rounding.
inline float angleBetween(const Quat& a, const Quat& b) {
    const Quat d = mul(conj(a), b);
    const float s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return 2.0f * std::atan2(s, std::fabs(d.w));
}

// Shoemake's slerp, always the short way round; t outside 0..1 is clamped.
inline Quat slerp(const Quat& a, Quat b, float t) {
    t = std::clamp(std::isfinite(t) ? t : 0.0f, 0.0f, 1.0f);
    float d = qdot(a, b);
    if (d < 0.0f) {
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995f) {
        // Nearly the same turn: a normalised lerp is exact to float precision and never divides by ~0.
        return qnormalize({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    }
    const float th = std::acos(d);
    const float s = std::sin(th);
    const float ka = std::sin((1.0f - t) * th) / s;
    const float kb = std::sin(t * th) / s;
    return qnormalize({ka * a.w + kb * b.w, ka * a.x + kb * b.x, ka * a.y + kb * b.y, ka * a.z + kb * b.z});
}

// Moves from a toward b by at most maxAngle radians.
inline Quat stepToward(const Quat& a, const Quat& b, float maxAngle) {
    const float angle = angleBetween(a, b);
    if (angle <= maxAngle || angle < 1e-7f) return b;
    return slerp(a, b, std::max(maxAngle, 0.0f) / angle);
}

// The look offsets viz/CameraRig applies to its path frame, in its order: yaw about the camera's up,
// then pitch about its right (positive looks up), then roll about its forward:
//   q = path * axisAngle(Y, yaw) * axisAngle(X, -pitch) * axisAngle(Z, roll).
struct Look {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
};

inline Quat fromLook(const Look& l) {
    return mul(mul(axisAngle({0.0f, 1.0f, 0.0f}, l.yaw), axisAngle({1.0f, 0.0f, 0.0f}, -l.pitch)),
               axisAngle({0.0f, 0.0f, 1.0f}, l.roll));
}

// fromLook's inverse for a relative turn q: yaw in (-pi, pi], pitch in [-pi/2, pi/2], roll in (-pi, pi].
// For M = Ry(a) Rx(b) Rz(c): M12 = -sin b, M02 / M22 = tan a, M10 / M11 = tan c; pitch = -b.
inline Look toLook(const Quat& q) {
    const Vec3 r = rightOf(q);
    const Vec3 u = upOf(q);
    const Vec3 f = fwdOf(q);
    Look l;
    l.pitch = std::asin(std::clamp(f[1], -1.0f, 1.0f));
    l.yaw = std::atan2(f[0], f[2]);
    l.roll = std::atan2(r[1], u[1]);
    return l;
}

// The camera's roll away from level: the signed turn about its forward that takes the level up (world
// up with the forward part removed) to its own up. Undefined looking straight up or down: nothing there.
inline bool rollFromLevel(const Quat& q, float& out) {
    const Vec3 f = fwdOf(q);
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    const Vec3 level = sub(worldUp, scale(f, dot(f, worldUp)));
    const float l = length(level);
    if (l < 0.03f) return false;
    const Vec3 lu = scale(level, 1.0f / l);
    const Vec3 u = upOf(q);
    out = std::atan2(dot(cross(lu, u), f), dot(lu, u));
    return true;
}

// The critically damped spring viz/CameraRig uses: exact for a constant target, stable for any dt.
struct Spring {
    float x = 0.0f;
    float v = 0.0f;
    void reset(float to) {
        x = to;
        v = 0.0f;
    }
    void step(float target, float dt, float seconds) {
        const float omega = 2.0f / std::max(seconds, 1e-3f);
        const float e = std::exp(-omega * dt);
        const float d = x - target;
        const float t = (v + omega * d) * dt;
        x = target + (d + t) * e;
        v = (v - omega * t) * e;
    }
};

}  // namespace geode::scene::camera
