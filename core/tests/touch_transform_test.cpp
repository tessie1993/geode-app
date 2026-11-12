// Host-side checks for core/viz/TouchTransform (#654): a pinch scales Zoom and a twist turns Rotation,
// clamped to each field's range in the params schema (core/viz/ParamTable.inc).
// Built and run by CI (.github/workflows/android.yml, "Native host tests") with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "viz/Params.hpp"
#include "viz/TouchTransform.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

}  // namespace

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const ParamSpec& zoom = SceneParams::spec(ParamSlot::zoom);
    const ParamSpec& rotation = SceneParams::spec(ParamSlot::rotation);

    // A pinch multiplies Zoom, inside the schema's range.
    check(near(touch::zoom(1.0f, 1.5f), 1.5f), "pinch scales zoom");
    check(touch::zoom(2.0f, 10.0f) == zoom.max, "pinch clamps to zoom max");
    check(touch::zoom(1.0f, 0.01f) == zoom.min, "pinch clamps to zoom min");
    check(near(zoom.min, 0.3f) && near(zoom.max, 3.0f), "zoom range is the catalogue's");
    // A broken or non-positive factor leaves Zoom alone.
    check(touch::zoom(1.2f, 0.0f) == 1.2f, "zero factor ignored");
    check(touch::zoom(1.2f, -2.0f) == 1.2f, "negative factor ignored");
    check(touch::zoom(1.2f, nan) == 1.2f, "NaN factor ignored");
    check(touch::zoom(1.2f, inf) == 1.2f, "infinite factor ignored");

    // A twist adds 0.012 per degree to Rotation, inside the schema's range.
    check(near(touch::rotation(0.0f, 100.0f), 1.2f), "twist turns rotation");
    check(near(touch::rotation(0.5f, -50.0f), -0.1f), "twist turns back");
    check(touch::rotation(2.9f, 100.0f) == rotation.max, "twist clamps to rotation max");
    check(touch::rotation(-2.9f, -100.0f) == rotation.min, "twist clamps to rotation min");
    check(touch::rotation(0.7f, nan) == 0.7f, "NaN twist ignored");
    check(touch::rotation(0.7f, -inf) == 0.7f, "infinite twist ignored");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("touch_transform_test: OK\n");
    return 0;
}
