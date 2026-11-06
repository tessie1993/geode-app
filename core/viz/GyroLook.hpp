#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::viz {

// Phone tilt as a camera look, relative to how the phone was held when the
// gyro was switched on. The shell reads the game rotation vector (no
// magnetometer, so a speaker or a metal table cannot swing the view) through
// SensorManager.getOrientation and hands over azimuth (-pi..pi) and pitch
// (-pi/2..pi/2) in radians; this turns them into the -1..1 look that
// CameraRig reads while camGyro is on (through Renderer::setLook).
//
// The first sample after a reset is the baseline: the way the phone was held
// is "straight ahead". Tilting kFullLookRadians from it is a full look to one
// side; further is clamped. The yaw delta wraps to -pi..pi, so turning across
// the sensor's +-pi seam is a small turn, not a whole circle.
//
// Pure: no GL, no allocation, no locks. Live view only; an export never sees
// a gyro and flies the recorded path.
struct GyroLook {
    struct Look {
        float yaw = 0.0f;
        float pitch = 0.0f;
    };

    // Tilting this far reads as a full look to one side.
    static constexpr float kFullLookRadians = 0.6f;
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    // A NaN base yaw means "capture the next sample as the baseline".
    float baseYaw = std::numeric_limits<float>::quiet_NaN();
    float basePitch = 0.0f;
    Look look{};

    // Wraps an angle difference into -pi..pi.
    static float wrap(float a) {
        if (!std::isfinite(a)) return 0.0f;
        if (a > kPi || a < -kPi) a = std::remainder(a, kTwoPi);
        return a;
    }

    // One orientation reading; returns the look it makes. A non-finite reading
    // is ignored and the last look stays.
    Look sample(float azimuth, float pitch) {
        if (!std::isfinite(azimuth) || !std::isfinite(pitch)) return look;
        if (std::isnan(baseYaw)) {
            baseYaw = azimuth;
            basePitch = pitch;
        }
        look.yaw = std::clamp(wrap(azimuth - baseYaw) / kFullLookRadians, -1.0f, 1.0f);
        look.pitch = std::clamp((pitch - basePitch) / kFullLookRadians, -1.0f, 1.0f);
        return look;
    }

    // The gyro switched off (or on again): recentre, and recapture on the next sample.
    Look reset() {
        baseYaw = std::numeric_limits<float>::quiet_NaN();
        basePitch = 0.0f;
        look = Look{};
        return look;
    }
};

}  // namespace geode::viz
