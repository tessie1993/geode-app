#include "scene/camera/Drivers.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::camera {

Pose blend(const Pose& a, const Pose& b, float w) {
    w = std::isfinite(w) ? std::clamp(w, 0.0f, 1.0f) : 0.0f;
    if (w <= 0.0f) return a;
    if (w >= 1.0f) return b;
    Pose o;
    o.pos = lerp(a.pos, b.pos, w);
    o.orient = slerp(a.orient, b.orient, w);
    o.focal = a.focal + (b.focal - a.focal) * w;
    return o;
}

namespace {

bool finiteKey(const PoseKey& k) {
    const Quat& q = k.pose.orient;
    return std::isfinite(k.time) && finite(k.pose.pos) && std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(k.pose.focal) && k.pose.focal > 0.0f;
}

}  // namespace

void KeyframeTrack::set(const PoseKey* keys, std::size_t count) {
    count_ = 0;
    if (keys == nullptr) return;
    for (std::size_t i = 0; i < count && count_ < kMaxKeys; ++i) {
        if (!finiteKey(keys[i])) continue;
        PoseKey k = keys[i];
        k.pose.orient = qnormalize(k.pose.orient);
        keys_[count_++] = k;
    }
    // Insertion sort: stable (two keys at one time keep their order) and allocation-free.
    for (std::size_t i = 1; i < count_; ++i) {
        const PoseKey k = keys_[i];
        std::size_t j = i;
        while (j > 0 && keys_[j - 1].time > k.time) {
            keys_[j] = keys_[j - 1];
            --j;
        }
        keys_[j] = k;
    }
}

Pose KeyframeTrack::sample(double t) const {
    if (count_ == 0) return Pose{};
    if (!(t > keys_[0].time)) return keys_[0].pose;
    if (t >= keys_[count_ - 1].time) return keys_[count_ - 1].pose;
    // The segment [lo - 1, lo) holding t: lo is the first key after t (an upper bound), so at a cut key's
    // own time the track already shows that key, matching cutBetween's (from, to].
    std::size_t lo = 1;
    std::size_t hi = count_ - 1;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (keys_[mid].time <= t) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    const PoseKey& a = keys_[lo - 1];
    const PoseKey& b = keys_[lo];
    if (b.cut) return a.pose;
    const double span = b.time - a.time;
    float u = span > 1e-9 ? static_cast<float>((t - a.time) / span) : 1.0f;
    u = u * u * (3.0f - 2.0f * u);
    return blend(a.pose, b.pose, u);
}

bool KeyframeTrack::cutBetween(double from, double to) const {
    if (!(to > from)) return false;
    for (std::size_t i = 0; i < count_; ++i) {
        if (keys_[i].cut && keys_[i].time > from && keys_[i].time <= to) return true;
    }
    return false;
}

void Takeover::reset() {
    yaw_ = pitch_ = weight_ = Spring{};
    active_ = false;
}

void Takeover::step(const Input& in, float dt, float reach, float follow, float glide) {
    const float yaw = std::isfinite(in.yaw) ? std::clamp(in.yaw, -1.0f, 1.0f) : 0.0f;
    const float pitch = std::isfinite(in.pitch) ? std::clamp(in.pitch, -1.0f, 1.0f) : 0.0f;
    active_ = in.active;
    if (active_) {
        // Follow the finger; the weight comes up as fast as the look follows.
        yaw_.step(yaw * reach, dt, follow);
        pitch_.step(pitch * reach, dt, follow);
        weight_.step(1.0f, dt, follow);
    } else {
        // Released: the look holds where the finger left it and the weight glides back to the flight.
        // A critically damped spring, so it settles without overshooting past the base.
        weight_.step(0.0f, dt, glide);
        if (weight_.x < 1e-4f) {
            weight_.reset(0.0f);
            yaw_.reset(0.0f);
            pitch_.reset(0.0f);
        }
    }
}

}  // namespace geode::scene::camera
