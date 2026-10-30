#include "viz/CameraRig.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kW = kTwoPi / CameraRig::kPathPeriod;
constexpr float kLookAhead = 0.75f;

using Vec3 = std::array<float, 3>;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(const Vec3& a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? scale(a, 1.0f / l) : Vec3{0.0f, 0.0f, 1.0f};
}
// Rodrigues rotation of v about unit axis k.
Vec3 rotate(const Vec3& v, const Vec3& k, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return add(add(scale(v, c), scale(cross(k, v), s)), scale(k, dot(k, v) * (1.0f - c)));
}

// Two incommensurate sines: a cheap, smooth handheld wobble, deterministic in time.
float wobble(float t, float a, float b) { return 0.6f * std::sin(t * a) + 0.4f * std::sin(t * b + 1.7f); }

}  // namespace

void CameraRig::Spring::step(float target, float dt, float seconds) {
    const float omega = 2.0f / std::max(seconds, 1e-3f);
    const float k = omega * dt;
    const float e = std::exp(-k);
    const float d = x - target;
    const float t = (v + omega * d) * dt;
    x = target + (d + t) * e;
    v = (v - omega * t) * e;
}

void CameraRig::reset() {
    yaw_ = pitch_ = roll_ = dolly_ = fov_ = speed_ = orbit_ = Spring{};
    travel_ = 0.0f;
    time_ = 0.0f;
    primed_ = false;
    pose_ = CameraPose{};
}

Vec3 CameraRig::pathPoint(int path, float orbit, float s) {
    const float a = 0.4f + 1.6f * std::clamp(orbit, 0.0f, 1.0f);
    switch (path) {
        case 1: {  // spiral: a corkscrew around the travel axis
            const float th = 52.0f * kW * s;
            return {a * std::cos(th), a * std::sin(th), s};
        }
        case 2: {  // orbit: circle the origin, z is not travel
            const float th = 24.0f * kW * s;
            const float r = 2.6f + 1.4f * a;
            return {r * std::sin(th), 0.35f * a * std::sin(13.0f * kW * s), -r * std::cos(th)};
        }
        case 3:  // hover: a slow figure-eight on the spot
            return {0.3f * a * std::sin(17.0f * kW * s), 0.2f * a * std::sin(34.0f * kW * s), 0.0f};
        default:  // glide: a smooth wandering spline down +z
            return {a * (std::sin(34.0f * kW * s) + 0.5f * std::sin(21.0f * kW * s + 1.0f)),
                    0.6f * a * std::sin(28.0f * kW * s + 2.0f), s};
    }
}

void CameraRig::step(const SceneParams& p, const LookInput& look, float dt, bool reducedMotion) {
    // Same frame-time rule as viz/Reactions: non-finite is no time at all.
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    const float g = reducedMotion ? safety::kReducedMotionScale : 1.0f;
    const float soft = 0.08f + 0.72f * std::clamp(p.camSmooth, 0.0f, 1.0f);
    const float lookYaw = p.camYaw * kMaxYaw + (p.camTouch || p.camGyro ? look.yaw * kTouchLook : 0.0f);
    const float lookPitch = p.camPitch * kMaxPitch + (p.camTouch || p.camGyro ? look.pitch * kTouchLook : 0.0f);
    const float fovTarget = std::clamp(p.camFov, 0.5f, 2.0f);
    const float speedTarget = std::clamp(p.camFly, 0.0f, 2.5f) * kBaseSpeed * g;
    if (!primed_) {
        yaw_.reset(lookYaw);
        pitch_.reset(lookPitch);
        roll_.reset(p.camRoll * kMaxRoll * g);
        dolly_.reset(p.camPush);
        fov_.reset(fovTarget);
        speed_.reset(speedTarget);
        orbit_.reset(p.camOrbit * g);
        primed_ = true;
    }
    yaw_.step(lookYaw, dt, soft);
    pitch_.step(lookPitch, dt, soft);
    roll_.step(p.camRoll * kMaxRoll * g, dt, soft * 1.5f);
    dolly_.step(std::clamp(p.camPush, -1.0f, 1.0f), dt, soft * 0.6f);
    fov_.step(fovTarget, dt, soft);
    // Speed eases slower than the look: a surge should feel like thrust.
    speed_.step(speedTarget, dt, soft * 2.0f);
    orbit_.step(std::clamp(p.camOrbit, 0.0f, 1.0f) * g, dt, soft * 3.0f);

    time_ = std::fmod(time_ + dt, kPathPeriod);
    travel_ = std::fmod(travel_ + std::max(speed_.x, 0.0f) * dt, kTravelWrap);
    const int path = std::clamp(p.camPath, 0, 3);

    const Vec3 here = pathPoint(path, orbit_.x, travel_);
    const Vec3 ahead = pathPoint(path, orbit_.x, travel_ + kLookAhead);
    Vec3 fwd = path == 2 ? normalize(scale(here, -1.0f)) : (path == 3 ? Vec3{0.0f, 0.0f, 1.0f} : normalize(sub(ahead, here)));
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    Vec3 right = normalize(cross(worldUp, fwd));
    Vec3 up = cross(fwd, right);

    // Bank into the curve: the path's sideways acceleration tilts the horizon.
    float bank = 0.0f;
    if (path == 0 || path == 1) {
        const Vec3 further = pathPoint(path, orbit_.x, travel_ + 2.0f * kLookAhead);
        const Vec3 turn = sub(normalize(sub(further, ahead)), fwd);
        bank = std::clamp(-dot(turn, right) * 1.2f, -0.5f, 0.5f) * g;
    }
    const float shake = std::clamp(p.camShake, 0.0f, 1.0f) * kShakeRadians * g;
    const float sYaw = shake * wobble(time_, 7.3f, 11.9f);
    const float sPitch = shake * wobble(time_, 8.9f, 13.1f);
    const float yaw = yaw_.x + sYaw;
    const float pitch = pitch_.x + sPitch;
    const float roll = roll_.x + bank;

    fwd = rotate(fwd, up, yaw);
    right = rotate(right, up, yaw);
    fwd = rotate(fwd, right, -pitch);
    up = rotate(up, right, -pitch);
    right = rotate(right, fwd, roll);
    up = rotate(up, fwd, roll);

    pose_.pos = add(here, scale(fwd, dolly_.x * kDollyReach));
    pose_.fwd = fwd;
    pose_.right = right;
    pose_.up = up;
    pose_.focal = kBaseFocal / fov_.x;
    pose_.travel = travel_;
    pose_.dolly = dolly_.x;
    pose_.yaw = yaw;
    pose_.pitch = pitch;
    pose_.roll = roll;
    // Flat projection: the path's lateral wander becomes a gentle pan, the
    // dolly and FOV become zoom, and the look tilts the image plane.
    pose_.flat = {0.05f * here[0], 0.05f * here[1], (1.0f + 0.35f * dolly_.x) / std::max(fov_.x, 0.25f), roll};
    pose_.tilt = {std::clamp(yaw, -1.0f, 1.0f) * kFlatTilt, std::clamp(pitch, -1.0f, 1.0f) * kFlatTilt};
}

}  // namespace geode::viz
