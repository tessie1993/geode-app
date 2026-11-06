// Host-side checks for core/viz/GyroLook (#755): phone tilt to camera look. Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no sensor.
//
// What is pinned: the first reading is the baseline and a reset recaptures it, the yaw delta wraps
// at +-pi (turning across the sensor's seam is a small turn), 0.6 rad is a full look and further is
// clamped to -1..1, a reset recentres to (0,0), and a non-finite reading changes nothing.
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

}  // namespace

int main() {
    baselineCapture();
    recapture();
    wrapAtPi();
    clamp();
    recentre();
    nonFiniteIgnored();
    if (failures > 0) {
        std::printf("%d gyro look check(s) failed\n", failures);
        return 1;
    }
    std::printf("gyro look: all checks passed\n");
    return 0;
}
