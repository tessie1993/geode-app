#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::viz {

// Phone tilt as a camera look, relative to how the phone was held when the
// gyro was switched on. The shell hands over the game rotation vector (no
// magnetometer, so a speaker or a metal table cannot swing the view) and the
// display rotation; screenAngles reads them as azimuth (-pi..pi) and pitch
// (-pi/2..pi/2) of the screen as the user sees it, and sample turns those into
// the -1..1 look that CameraRig reads while camGyro is on (Renderer::setLook).
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

    struct Angles {
        float azimuth = 0.0f;
        float pitch = 0.0f;
    };

    // A NaN base yaw means "capture the next sample as the baseline".
    float baseYaw = std::numeric_limits<float>::quiet_NaN();
    float basePitch = 0.0f;
    int baseRotation = -1;
    Look look{};

    // SensorManager.getOrientation's azimuth and pitch after remapCoordinateSystem
    // by the display rotation (Surface.ROTATION_0..270 as 0..3), from the rotation
    // vector's quaternion. Both read only the screen's up axis in the world: the
    // device's y in portrait, its x in landscape. Without the remap a landscape
    // screen's tilt toward or away is the device's roll, which pitch never sees.
    static Angles screenAngles(float x, float y, float z, float w, int displayRotation) {
        const int r = displayRotation & 3;
        // Columns 0 and 1 of getRotationMatrixFromVector: the device's x and y axes in the world.
        const float devX[3] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w)};
        const float devY[3] = {2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w)};
        const float* up = (r & 1) ? devX : devY;
        const float s = r < 2 ? 1.0f : -1.0f;
        return {std::atan2(s * up[0], s * up[1]), std::asin(std::clamp(-s * up[2], -1.0f, 1.0f))};
    }

    // One rotation-vector reading on a screen at displayRotation. A new rotation
    // (the app turned with the phone) recaptures the baseline: the old one was
    // measured about other axes.
    Look sampleScreen(float x, float y, float z, float w, int displayRotation) {
        const int r = displayRotation & 3;
        if (r != baseRotation) {
            reset();
            baseRotation = r;
        }
        const Angles a = screenAngles(x, y, z, w, r);
        return sample(a.azimuth, a.pitch);
    }

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
        baseRotation = -1;
        look = Look{};
        return look;
    }
};

}  // namespace geode::viz
