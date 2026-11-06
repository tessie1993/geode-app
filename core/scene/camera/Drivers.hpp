#pragma once

#include <array>
#include <cstddef>

#include "scene/camera/CameraMath.hpp"

// The scene camera's drivers. Each produces a pose and a weight; SceneCamera blends them top-down,
// Studio keyframes over touch/gyro takeover over the base flight (viz::CameraRig, wrapped in
// SceneCamera.cpp). All pure, fixed-size, allocation-free per frame.

namespace geode::scene::camera {

// A camera pose: a position, a unit orientation quaternion (local X right, Y up, Z forward; see
// CameraMath.hpp) and a focal length (1 / tan(fov / 2) for a unit-height screen). The quaternion has no
// gimbal lock, so the camera can flip, loop and turn round.
struct Pose {
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Quat orient{};
    float focal = 1.6f;
};

// Blends b over a by weight w (0 keeps a, 1 is b): position and focal lerp, orientation slerps.
Pose blend(const Pose& a, const Pose& b, float w);

// One key of a Studio pose track, on programme (timeline) time.
struct PoseKey {
    double time = 0.0;  // seconds of programme time
    Pose pose{};
    // A cut key: the track holds the previous key until this one's time, then jumps (no tween).
    bool cut = false;
};

// Studio keyframes (the timeline origin): a sorted pose track sampled at programme time. Before the
// first key the track holds it, after the last it holds that one. Between keys the pose eases
// (smoothstep) from one to the next, position lerped, orientation slerped the short way.
class KeyframeTrack {
public:
    static constexpr std::size_t kMaxKeys = 256;

    // Copies up to kMaxKeys keys and sorts them by time. Non-finite keys are dropped. count 0 clears.
    void set(const PoseKey* keys, std::size_t count);
    void clear() { count_ = 0; }
    bool empty() const { return count_ == 0; }
    std::size_t size() const { return count_; }

    // The pose at programme time t.
    Pose sample(double t) const;
    // True when a cut key's time lies in (from, to]: the track jumped between those two frames.
    bool cutBetween(double from, double to) const;

private:
    std::array<PoseKey, kMaxKeys> keys_{};
    std::size_t count_ = 0;
};

// Touch or gyro takeover. While a finger is down (or the gyro is on) the view follows the input's
// look; on release the takeover's weight glides back to 0 on a critically damped spring, so the view
// returns to the flight below without a jump or an overshoot, at any frame rate.
class Takeover {
public:
    struct Input {
        float yaw = 0.0f;    // -1..1
        float pitch = 0.0f;  // -1..1
        bool active = false;
    };

    void reset();
    // reach: radians at full deflection; follow: seconds to follow the input; glide: seconds to glide back.
    void step(const Input& in, float dt, float reach, float follow, float glide);

    // The weight this layer is blended with, 0..1.
    float weight() const { return std::clamp(weight_.x, 0.0f, 1.0f); }
    // The turn this layer lays over the base, in the base's own frame (right-multiplied).
    Quat offset() const { return fromLook({yaw_.x, pitch_.x, 0.0f}); }
    bool active() const { return active_; }

private:
    Spring yaw_, pitch_, weight_;
    bool active_ = false;
};

}  // namespace geode::scene::camera
