#pragma once
#include <cmath>

// Shake to screenshot (Visuals): raw accelerometer samples in, "this was a shake" out. Pure, so
// core/tests/shake_test.cpp drives it with synthetic motion; the shell only registers the sensor
// while Visuals shows and hands each SensorEvent across (geode_shake_sample).
//
// Gravity is a slow low-pass of the samples; what is left is the hand. A shake is kPeaks lobes of that
// hand acceleration past kThreshold, each pointing against the one before (a reversal), each kMinGapS..
// kMaxGapS after it: a hand shakes at roughly 2..7 Hz. Walking stays far under the threshold, setting the
// phone down is one lobe (its ringing comes faster than kMinGapS), and turning it over is a slow drift
// the low-pass follows. After a shake nothing fires again until the phone has been still (no lobe) for
// kCalmS and kCooldownS has passed, so one shake saves one picture however long it goes on.
namespace geode::viz {

class ShakeDetector {
public:
    static constexpr float kThreshold = 15.0f;  // m/s² of hand acceleration (~1.5 g on top of gravity)
    static constexpr int kPeaks = 4;            // lobes, so three reversals
    static constexpr double kMinGapS = 0.07;
    static constexpr double kMaxGapS = 0.40;
    static constexpr double kCooldownS = 1.5;
    static constexpr double kCalmS = 0.6;
    static constexpr double kGravityTauS = 0.5;
    static constexpr double kGapResetS = 0.5;  // a gap in the samples this long starts over
    static constexpr double kSettleS = 0.3;

    void reset() { *this = ShakeDetector{}; }

    // One sample at t seconds (any monotonic base) in m/s². True on the sample that completes a shake.
    bool sample(double t, float x, float y, float z) {
        if (!started_ || t - lastT_ > kGapResetS || t < lastT_) {
            *this = ShakeDetector{};
            started_ = true;
            g_[0] = x, g_[1] = y, g_[2] = z;
            lastT_ = t;
            armedAt_ = t + kSettleS;  // the first sample's gravity may hold some hand: let the low-pass settle
            return false;
        }
        const double dt = t - lastT_;
        lastT_ = t;
        const float k = static_cast<float>(1.0 - std::exp(-dt / kGravityTauS));
        const float a[3] = {x, y, z};
        float h[3];
        for (int i = 0; i < 3; ++i) {
            g_[i] += (a[i] - g_[i]) * k;
            h[i] = a[i] - g_[i];
        }
        const bool strong = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]) > kThreshold;
        if (t < armedAt_) {
            // Cooling down: a shake still going on pushes the re-arm out until the phone is calm.
            if (strong) armedAt_ = std::fmax(armedAt_, t + kCalmS);
            return false;
        }
        if (!strong) {
            if (peaks_ > 0 && t - peakT_ > kMaxGapS) peaks_ = 0;
            return false;
        }
        const float dot = h[0] * dir_[0] + h[1] * dir_[1] + h[2] * dir_[2];
        if (peaks_ > 0 && dot >= 0.0f) return false;  // the same lobe, still going
        const double gap = t - peakT_;
        if (peaks_ > 0 && gap < kMinGapS) return false;  // ringing, not a hand
        peaks_ = (peaks_ > 0 && gap <= kMaxGapS) ? peaks_ + 1 : 1;
        peakT_ = t;
        for (int i = 0; i < 3; ++i) dir_[i] = h[i];
        if (peaks_ < kPeaks) return false;
        peaks_ = 0;
        armedAt_ = t + kCooldownS;
        return true;
    }

private:
    bool started_ = false;
    double lastT_ = 0.0;
    float g_[3] = {0.0f, 0.0f, 0.0f};
    float dir_[3] = {0.0f, 0.0f, 0.0f};
    int peaks_ = 0;
    double peakT_ = 0.0;
    double armedAt_ = 0.0;  // no shake counts before this time
};

}  // namespace geode::viz
