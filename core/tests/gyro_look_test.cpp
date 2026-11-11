// Host-side checks for core/viz/GyroLook (#755): phone tilt to camera look. Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no sensor.
//
// What is pinned: the first reading is the baseline and a reset recaptures it, the yaw delta wraps
// at +-pi (turning across the sensor's seam is a small turn), 0.6 rad is a full look and further is
// clamped to -1..1, a reset recentres to (0,0), and a non-finite reading changes nothing. And the
// screen's own axes: at every display rotation a reading reads as SensorManager.getOrientation of
// the device frame remapped to the screen, so a landscape tilt toward you is pitch, and a new
// rotation recaptures the baseline.
#include <cmath>
#include <cstdio>
#include <limits>

#include "viz/GyroLook.hpp"

using geode::viz::GyroLook;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

constexpr float kPi = GyroLook::kPi;
constexpr float kFull = GyroLook::kFullLookRadians;

void baselineCapture() {
    GyroLook g;
    const GyroLook::Look first = g.sample(1.0f, 0.3f);
    check(near(first.yaw, 0.0f) && near(first.pitch, 0.0f), "first reading is the baseline: look (0,0)");
    const GyroLook::Look half = g.sample(1.0f + kFull * 0.5f, 0.3f - kFull * 0.25f);
    check(near(half.yaw, 0.5f), "half a full look of yaw reads 0.5");
    check(near(half.pitch, -0.25f), "a quarter of a full look of pitch down reads -0.25");
}

void recapture() {
    GyroLook g;
    g.sample(0.0f, 0.0f);
    g.sample(0.3f, 0.2f);
    g.reset();
    const GyroLook::Look again = g.sample(2.0f, -0.5f);
    check(near(again.yaw, 0.0f) && near(again.pitch, 0.0f), "after a reset the next reading is the new baseline");
    const GyroLook::Look moved = g.sample(2.0f + 0.3f, -0.5f + 0.3f);
    check(near(moved.yaw, 0.3f / kFull) && near(moved.pitch, 0.3f / kFull), "the look is measured from the new baseline");
}

void wrapAtPi() {
    GyroLook g;
    g.sample(kPi - 0.1f, 0.0f);
    // Across the seam: from pi - 0.1 to -pi + 0.1 is a 0.2 rad turn, not -2pi + 0.2.
    const GyroLook::Look across = g.sample(-kPi + 0.1f, 0.0f);
    check(near(across.yaw, 0.2f / kFull), "yaw wraps across +pi to a small positive turn");
    GyroLook h;
    h.sample(-kPi + 0.1f, 0.0f);
    const GyroLook::Look back = h.sample(kPi - 0.1f, 0.0f);
    check(near(back.yaw, -0.2f / kFull), "yaw wraps across -pi to a small negative turn");
    check(near(GyroLook::wrap(3.0f * kPi), kPi) || near(GyroLook::wrap(3.0f * kPi), -kPi), "wrap folds 3pi to the seam");
    check(near(GyroLook::wrap(0.5f), 0.5f), "wrap leaves an angle inside -pi..pi alone");
    check(GyroLook::wrap(std::numeric_limits<float>::infinity()) == 0.0f, "wrap of infinity is 0, never a hang");
}

void clamp() {
    GyroLook g;
    g.sample(0.0f, 0.0f);
    const GyroLook::Look far = g.sample(1.5f, 1.2f);
    check(near(far.yaw, 1.0f) && near(far.pitch, 1.0f), "tilting past a full look clamps to +1");
    const GyroLook::Look farOther = g.sample(-1.5f, -1.2f);
    check(near(farOther.yaw, -1.0f) && near(farOther.pitch, -1.0f), "and to -1 the other way");
    const GyroLook::Look edge = g.sample(kFull, -kFull);
    check(near(edge.yaw, 1.0f) && near(edge.pitch, -1.0f), "exactly a full look is +-1");
}

void recentre() {
    GyroLook g;
    g.sample(0.0f, 0.0f);
    g.sample(0.4f, -0.4f);
    const GyroLook::Look off = g.reset();
    check(near(off.yaw, 0.0f) && near(off.pitch, 0.0f), "switching off recentres to (0,0)");
    check(near(g.look.yaw, 0.0f) && near(g.look.pitch, 0.0f), "and the held look is (0,0)");
    check(std::isnan(g.baseYaw), "and the baseline is cleared for recapture");
}

void nonFiniteIgnored() {
    GyroLook g;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const GyroLook::Look none = g.sample(nan, 0.0f);
    check(near(none.yaw, 0.0f) && std::isnan(g.baseYaw), "a NaN reading is not captured as the baseline");
    g.sample(0.0f, 0.0f);
    const GyroLook::Look held = g.sample(0.3f, 0.0f);
    const GyroLook::Look still = g.sample(0.0f, nan);
    check(near(still.yaw, held.yaw) && near(still.pitch, held.pitch), "a NaN reading keeps the last look");
}

struct Quat {
    float x, y, z, w;
};

Quat axisAngle(float ax, float ay, float az, float angle) {
    const float s = std::sin(angle * 0.5f);
    return {ax * s, ay * s, az * s, std::cos(angle * 0.5f)};
}

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// SensorManager.getRotationMatrixFromVector then getOrientation, unremapped, as GyroLook.kt read it before.
GyroLook::Angles androidOrientation(const Quat& q) {
    const float r1 = 2.0f * (q.x * q.y - q.z * q.w);
    const float r4 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
    const float r7 = 2.0f * (q.y * q.z + q.x * q.w);
    return {std::atan2(r1, r4), std::asin(-r7)};
}

// The screen held as `screen` (screen frame to world); the device inside it turned by the display
// rotation: Surface.ROTATION_90 is the device turned a quarter counter-clockwise, its x the screen's up.
Quat deviceIn(const Quat& screen, int rotation) { return mul(screen, axisAngle(0, 0, 1, kPi * 0.5f * rotation)); }

void portraitIsGetOrientation() {
    const Quat qs[] = {axisAngle(1, 0, 0, 0.7f), mul(axisAngle(0, 0, 1, 2.0f), axisAngle(1, 0, 0, -0.4f)),
                       mul(axisAngle(0, 0, 1, -1.1f), axisAngle(0, 1, 0, 0.3f))};
    for (const Quat& q : qs) {
        const GyroLook::Angles a = GyroLook::screenAngles(q.x, q.y, q.z, q.w, 0);
        const GyroLook::Angles ref = androidOrientation(q);
        check(near(a.azimuth, ref.azimuth) && near(a.pitch, ref.pitch), "ROTATION_0 reads exactly what getOrientation did");
    }
}

void everyRotationReadsTheScreen() {
    const float yaws[] = {-2.5f, -0.4f, 0.0f, 1.3f, 3.0f};
    const float tilts[] = {-0.9f, -0.2f, 0.0f, 0.5f, 1.0f};
    const float rolls[] = {-0.3f, 0.0f, 0.25f};
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (float yaw : yaws) {
            for (float tilt : tilts) {
                for (float roll : rolls) {
                    const Quat screen = mul(mul(axisAngle(0, 0, 1, yaw), axisAngle(1, 0, 0, tilt)), axisAngle(0, 1, 0, roll));
                    const Quat device = deviceIn(screen, rotation);
                    const GyroLook::Angles want = GyroLook::screenAngles(screen.x, screen.y, screen.z, screen.w, 0);
                    const GyroLook::Angles got = GyroLook::screenAngles(device.x, device.y, device.z, device.w, rotation);
                    check(near(GyroLook::wrap(got.azimuth - want.azimuth), 0.0f) && near(got.pitch, want.pitch),
                          "a turned display reads the screen's azimuth and pitch, not the device's");
                }
            }
        }
    }
}

void landscapeTiltIsPitch() {
    // Landscape (ROTATION_90), held upright-ish, then the top edge tipped 0.3 rad toward the user.
    const Quat held = deviceIn(axisAngle(1, 0, 0, 1.0f), 1);
    const Quat tipped = deviceIn(axisAngle(1, 0, 0, 0.7f), 1);
    GyroLook g;
    g.sampleScreen(held.x, held.y, held.z, held.w, 1);
    const GyroLook::Look look = g.sampleScreen(tipped.x, tipped.y, tipped.z, tipped.w, 1);
    check(near(look.pitch, 0.3f / kFull) && near(look.yaw, 0.0f), "a landscape tilt toward you is pitch, not yaw");
    const GyroLook::Angles before = androidOrientation(held);
    const GyroLook::Angles after = androidOrientation(tipped);
    check(near(after.pitch, before.pitch), "unremapped, getOrientation's pitch never saw that tilt (the old bug)");
    // ROTATION_270 is the other landscape: the same tilt reads the same.
    const Quat held3 = deviceIn(axisAngle(1, 0, 0, 1.0f), 3);
    const Quat tipped3 = deviceIn(axisAngle(1, 0, 0, 0.7f), 3);
    GyroLook h;
    h.sampleScreen(held3.x, held3.y, held3.z, held3.w, 3);
    const GyroLook::Look look3 = h.sampleScreen(tipped3.x, tipped3.y, tipped3.z, tipped3.w, 3);
    check(near(look3.pitch, look.pitch) && near(look3.yaw, 0.0f), "ROTATION_270 reads the same tilt the same");
}

void newRotationRecaptures() {
    GyroLook g;
    const Quat a = axisAngle(1, 0, 0, 0.8f);
    const Quat b = mul(axisAngle(0, 0, 1, 0.3f), axisAngle(1, 0, 0, 0.8f));
    g.sampleScreen(a.x, a.y, a.z, a.w, 0);
    const GyroLook::Look turned = g.sampleScreen(b.x, b.y, b.z, b.w, 0);
    check(near(turned.yaw, -0.3f / kFull), "turning 0.3 rad counter-clockwise about the vertical is -0.3 of yaw");
    const GyroLook::Look rotated = g.sampleScreen(b.x, b.y, b.z, b.w, 1);
    check(near(rotated.yaw, 0.0f) && near(rotated.pitch, 0.0f), "a new display rotation is a new baseline");
}

}  // namespace

int main() {
    baselineCapture();
    recapture();
    wrapAtPi();
    clamp();
    recentre();
    nonFiniteIgnored();
    portraitIsGetOrientation();
    everyRotationReadsTheScreen();
    landscapeTiltIsPitch();
    newRotationRecaptures();
    if (failures > 0) {
        std::printf("%d gyro look check(s) failed\n", failures);
        return 1;
    }
    std::printf("gyro look: all checks passed\n");
    return 0;
}
