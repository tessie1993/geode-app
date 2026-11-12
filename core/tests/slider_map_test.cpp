// Host-side checks for a Customize slider's maths (core/viz/SliderMap) and its C ABI (geode_slider_*). Built and
// run by CI ("Native host tests") with the system compiler; no device.
//
// What is pinned: when Reset is offered, a whole-number slider's ticks and snap (rounded, so a position a hair
// under its tick keeps its step), and the wave period's log track over its real span (0.05 s to 60 s), both ways.
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "api/geode_api.h"
#include "viz/SliderMap.hpp"

namespace slider = geode::viz::slider;

namespace {

int failures = 0;
const float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kLo = 0.05f;
constexpr float kHi = 60.0f;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float got, float want, float tolerance) {
    return std::fabs(got - want) <= tolerance;
}

void resets() {
    check(!slider::offDefault(0.5f, 0.5f), "at the default: no Reset");
    check(!slider::offDefault(0.50005f, 0.5f), "inside the epsilon: no Reset");
    check(slider::offDefault(0.5002f, 0.5f), "past the epsilon: Reset");
    check(slider::offDefault(-0.3f, 0.2f), "below the default: Reset");
    check(!slider::offDefault(0.7f, kNaN), "no default: no Reset");
    check(!slider::offDefault(kNaN, 0.5f), "a NaN value: no Reset");
}

void wholeNumbers() {
    check(slider::intSteps(1, 8) == 6, "1..8 has six ticks between its ends");
    check(slider::intSteps(0, 1) == 0, "0..1 has none");
    check(slider::intSteps(3, 3) == 0, "a one-value range has none");
    check(slider::intSteps(5, 2) == 0, "a reversed range has none");
    check(slider::intValue(3.0f, 1, 8) == 3, "on its tick");
    check(slider::intValue(2.9999998f, 1, 8) == 3, "a hair under its tick keeps the step");
    check(slider::intValue(4.0000005f, 1, 8) == 4, "a hair over its tick");
    check(slider::intValue(3.4f, 1, 8) == 3, "nearer 3");
    check(slider::intValue(3.5f, 1, 8) == 4, "a half rounds up");
    check(slider::intValue(9.0f, 1, 8) == 8, "clamped to the top");
    check(slider::intValue(-1.0f, 1, 8) == 1, "clamped to the bottom");
    check(slider::intValue(kNaN, 1, 8) == 1, "NaN reads as 0, clamped");
    check(slider::intValue(kNaN, -2, 2) == 0, "NaN reads as 0");
    check(slider::intValue(4.0f, 5, 2) == 5, "a reversed range gives its first");
}

void logTrack() {
    check(slider::logPosition(kLo, kLo, kHi) == 0.0f, "the shortest period is the left end");
    check(slider::logPosition(kHi, kLo, kHi) == 1.0f, "the longest is the right end");
    check(slider::logPosition(100.0f, kLo, kHi) == 1.0f, "past the top pins right");
    check(slider::logPosition(0.01f, kLo, kHi) == 0.0f, "under the bottom pins left");
    check(near(slider::logPosition(std::sqrt(kLo * kHi), kLo, kHi), 0.5f, 1e-5f), "the geometric middle is the middle");
    check(near(slider::logValue(0.0f, kLo, kHi), kLo, 1e-7f), "the left end is 0.05 s");
    check(near(slider::logValue(1.0f, kLo, kHi), kHi, 1e-4f), "the right end is 60 s");
    check(near(slider::logValue(0.5f, kLo, kHi), std::sqrt(kLo * kHi), 1e-5f), "the middle is about 1.73 s");
    check(slider::logValue(-0.2f, kLo, kHi) == slider::logValue(0.0f, kLo, kHi), "a position under 0 pins left");
    for (const float seconds : {0.05f, 0.2f, 1.0f, 2.0f, 7.5f, 33.0f, 60.0f}) {
        const float back = slider::logValue(slider::logPosition(seconds, kLo, kHi), kLo, kHi);
        check(near(back, seconds, seconds * 1e-5f), "a period survives position and back");
    }
    check(slider::logPosition(kNaN, kLo, kHi) == 0.0f, "NaN sits left");
    check(slider::logPosition(1.0f, 0.0f, kHi) == 0.0f, "a zero bottom is no log track");
    check(slider::logValue(0.5f, 2.0f, 2.0f) == 2.0f, "an empty span pins to lo");
}

void cAbi() {
    check(geode_slider_off_default(0.5002f, 0.5f) == 1 && geode_slider_off_default(0.5f, 0.5f) == 0, "C: off default");
    check(geode_slider_int_steps(1, 8) == 6, "C: steps");
    check(geode_slider_int_value(2.9999998f, 1, 8) == 3, "C: int value");
    check(geode_slider_log_position(kHi, kLo, kHi) == 1.0f, "C: log position");
    check(near(geode_slider_log_value(0.0f, kLo, kHi), kLo, 1e-7f), "C: log value");
}

}  // namespace

int main() {
    resets();
    wholeNumbers();
    logTrack();
    cAbi();
    if (failures == 0) std::printf("slider_map_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
