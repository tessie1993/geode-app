// Host-side checks for core/viz/TouchGesture (was VisualGestures.kt): touch play's fingers into y-up NDC, the
// two-finger reading the pinch and twist come from, and touch play's stir.
// Built and run by CI (.github/workflows/native-host-tests.yml) with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <string>

#include "viz/TouchGesture.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

}  // namespace

int main() {
    // Pixels into y-up NDC: the top-left corner is (-1, 1), the bottom-right (1, -1), the centre (0, 0).
    float xy[6] = {0.0f, 0.0f, 400.0f, 800.0f, 200.0f, 400.0f};
    touch::toNdc(xy, 3, 400.0f, 800.0f);
    check(near(xy[0], -1.0f) && near(xy[1], 1.0f), "top-left is (-1, 1)");
    check(near(xy[2], 1.0f) && near(xy[3], -1.0f), "bottom-right is (1, -1)");
    check(near(xy[4], 0.0f) && near(xy[5], 0.0f), "the centre is (0, 0)");
    float tiny[2] = {0.5f, 0.5f};
    touch::toNdc(tiny, 1, 0.0f, 0.0f);
    check(near(tiny[0], 0.0f) && near(tiny[1], 0.0f), "an unlaid view reads as 1 px");
    float left[4] = {10.0f, 10.0f, 99.0f, 99.0f};
    touch::toNdc(left, 1, 20.0f, 20.0f);
    check(left[2] == 99.0f && left[3] == 99.0f, "only the live points are rewritten");

    // Two fingers: a pan moves the midpoint, a spread is the zoom, a turn the twist.
    const float start[4] = {100.0f, 100.0f, 200.0f, 100.0f};
    const float panned[4] = {130.0f, 60.0f, 230.0f, 60.0f};
    auto r = touch::readPair(start, start, panned);
    check(near(r.panX, 30.0f) && near(r.panY, -40.0f), "a drag pans");
    check(near(r.zoom, 1.0f) && near(r.twist, 0.0f), "a drag neither zooms nor twists");
    check(near(r.stepFactor, 1.0f) && near(r.stepDegrees, 0.0f), "a drag has no pinch step");

    const float spread[4] = {50.0f, 100.0f, 250.0f, 100.0f};
    r = touch::readPair(start, start, spread);
    check(near(r.zoom, 2.0f) && near(r.stepFactor, 2.0f), "spreading to twice is zoom 2");
    check(near(r.panX, 0.0f) && near(r.panY, 0.0f), "a centred spread does not pan");

    const float quarter[4] = {150.0f, 50.0f, 150.0f, 150.0f};
    r = touch::readPair(start, start, quarter);
    check(near(r.twist, touch::kPi / 2.0f) && near(r.stepDegrees, 90.0f), "a quarter turn is 90 degrees");
    check(near(r.zoom, 1.0f), "a turn alone does not zoom");

    // The step is from the last event, not the start: a pinch recognised late does not jump.
    r = touch::readPair(start, spread, spread);
    check(near(r.zoom, 2.0f) && near(r.stepFactor, 1.0f) && near(r.stepDegrees, 0.0f), "no change since last, no step");
    const float wider[4] = {0.0f, 100.0f, 300.0f, 100.0f};
    r = touch::readPair(start, spread, wider);
    check(near(r.zoom, 3.0f) && near(r.stepFactor, 1.5f), "the step is the spread since the last event");

    // A twist across the half turn wraps: from +170 to -170 degrees is 20 degrees on, not 340 back.
    const float d = touch::kPi / 180.0f;
    const float at170[4] = {0.0f, 0.0f, 100.0f * std::cos(170.0f * d), 100.0f * std::sin(170.0f * d)};
    const float atMinus170[4] = {0.0f, 0.0f, 100.0f * std::cos(-170.0f * d), 100.0f * std::sin(-170.0f * d)};
    const float flat[4] = {0.0f, 0.0f, 100.0f, 0.0f};
    r = touch::readPair(flat, at170, atMinus170);
    check(near(r.stepDegrees, 20.0f, 1e-2f), "a twist wraps across the half turn");
    check(near(r.twist, -170.0f * d, 1e-4f), "the twist since start is wrapped");

    // Fingers that met last event give no factor; fingers that came down together read as 1 px apart.
    const float met[4] = {100.0f, 100.0f, 100.0f, 100.0f};
    r = touch::readPair(start, met, spread);
    check(r.stepFactor == 1.0f, "no factor from a zero spread");
    r = touch::readPair(met, met, spread);
    check(near(r.zoom, 200.0f) && std::isfinite(r.stepFactor), "a zero start spread reads as 1 px");
    check(touch::readPair(nullptr, start, start).zoom == 1.0f, "a missing pair reads as nothing");

    // The stir: two opposite points circling the centre at the radius, a step a frame, then over.
    float p[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    check(touch::pulse(0, p) == touch::kPulseFrameMs, "the stir's first step holds a frame");
    check(near(p[0], touch::kPulseRadius) && near(p[1], 0.0f) && near(p[2], -touch::kPulseRadius) && near(p[3], 0.0f),
          "the stir starts on the x axis, the points opposite");
    check(touch::pulse(touch::kPulseSteps / 4, p) > 0, "a quarter of the way");
    check(near(p[0], 0.0f) && near(p[1], touch::kPulseRadius) && near(p[3], -touch::kPulseRadius), "a quarter turn round");
    int steps = 0;
    for (int i = 0; touch::pulse(i, p) > 0; ++i) ++steps;
    check(steps == 36, "the stir is 36 steps, one turn");
    float untouched[4] = {7.0f, 7.0f, 7.0f, 7.0f};
    check(touch::pulse(touch::kPulseSteps, untouched) == 0 && untouched[0] == 7.0f, "past the end: over, xy untouched");
    check(touch::pulse(-1, untouched) == 0, "a negative step is over");
    check(touch::pulse(0, nullptr) == 0, "no buffer, no stir");
    check(near(touch::kPulseRadius, 0.35f) && touch::kPulseFrameMs == 33, "the stir is the shell's old one");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("touch_gesture_test: OK\n");
    return 0;
}
