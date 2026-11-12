// Host-side checks for core/viz/ControlRange: the range a Customize slider offers for a scene param is the
// schema's, cut short only where the control stops early (the flash clamp's limits and Fly's stop).
// The cases of the shell's old ControlRangeTest. Built and run by CI (native-host-tests.yml); no GL.
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "viz/ControlRange.hpp"
#include "viz/Params.hpp"

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

bool is(std::string_view name, float lo, float hi) {
    control::Range r{};
    return control::of(name, r) && near(r.min, lo) && near(r.max, hi);
}

}  // namespace

int main() {
    // A control range is the schema range unless the control stops short.
    check(is("speed", 0.05f, 4.0f), "speed is the schema range");
    check(is("flowCurl", 0.0f, 50.0f), "flowCurl is the schema range");
    check(is("cymaticsModes", 1.0f, 8.0f), "cymaticsModes is the schema range");
    // Both Dive speed sliders read this; the Style page's used to stop at 0.05.
    check(is("endlessZoomSpeed", -1.2f, 1.2f), "endlessZoomSpeed is the schema range");
    // The caps: the flash clamp's limits and Fly's stop; the LFO still reaches the schema's max.
    check(is("brightness", 0.2f, 1.25f), "brightness stops at 1 + the flash depth");
    check(is("intensity", 0.2f, 1.25f), "intensity stops at 1 + the flash depth");
    check(is("bloom", 0.0f, 0.6f), "bloom stops at its safety cap");
    check(is("camFly", -2.0f, 2.0f), "camFly stops at 2 each way");
    // An unknown name has no range and leaves out alone.
    control::Range untouched{7.0f, 9.0f};
    check(!control::of("noSuchParam", untouched) && untouched.min == 7.0f && untouched.max == 9.0f, "unknown name refused");

    // Every control range lies inside its schema range and is not empty.
    const auto& rows = SceneParams::schema();
    for (int i = 0; i < SceneParams::kFieldCount; ++i) {
        const ParamSpec& spec = rows[static_cast<size_t>(i)];
        const control::Range r = control::of(i);
        check(r.min >= spec.min && r.max <= spec.max && r.min <= r.max, std::string(spec.name) + " inside the schema range");
    }

    // Every cap really stops short: one at or past the schema's end would be dead weight.
    for (const ParamSlot slot : {ParamSlot::brightness, ParamSlot::intensity, ParamSlot::bloom, ParamSlot::camFly}) {
        const ParamSpec& spec = SceneParams::spec(slot);
        const control::Range r = control::of(static_cast<int>(slot));
        check(r.max > spec.min && r.max < spec.max, std::string(spec.name) + " cap stops short");
    }
    const ParamSpec& fly = SceneParams::spec(ParamSlot::camFly);
    check(control::of(static_cast<int>(ParamSlot::camFly)).min > fly.min, "camFly floor stops short");

    if (failures == 0) std::printf("control_range_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
