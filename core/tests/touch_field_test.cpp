// Host-side checks for the touch field (core/viz/input/TouchField): a point that is not a number is dropped, not counted
// as a finger, so the fingers that are real stay packed from the first slot and the anchor follows them. Built
// and run by CI (.github/workflows/native-host-tests.yml); no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/touch_field_test.cpp core/viz/input/TouchField.cpp
//     -o touch_field_test && ./touch_field_test
#include <cmath>
#include <cstdio>

#include "viz/input/TouchField.hpp"

using geode::viz::TouchField;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void aNonFinitePointIsNotAFinger() {
    TouchField field;
    const float nan = std::nanf("");
    const float xy[4] = {nan, 0.2f, 0.5f, -0.25f};
    field.submit(xy, 2);
    field.step(1.0f / 60.0f);
    check(field.count() == 1, "one real finger: one point");
    const auto& pts = field.points();
    check(pts[0] == 0.5f && pts[1] == -0.25f && pts[2] == 1.0f, "the real finger sits in the first slot");
    check(field.anchorX() == 0.5f && field.anchorY() == -0.25f, "the anchor is the real finger");
    check(field.gesture() != TouchField::kGestureAxis && field.gesture() != TouchField::kGestureVortex,
          "one finger makes no two-finger gesture");
}

void twoRealFingersStayTwo() {
    TouchField field;
    const float xy[4] = {-0.5f, 0.0f, 0.5f, 0.0f};
    field.submit(xy, 2);
    field.step(1.0f / 60.0f);
    check(field.count() == 2, "two real fingers: two points");
}

}  // namespace

int main() {
    aNonFinitePointIsNotAFinger();
    twoRealFingersStayTwo();
    if (failures) {
        std::printf("%d touch field check(s) failed\n", failures);
        return 1;
    }
    std::printf("touch field: all checks passed\n");
    return 0;
}
