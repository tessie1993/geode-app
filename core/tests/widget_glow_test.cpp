// The widget's generated glow (viz/WidgetGlow): where the three lights sit at a tick and the hue each takes,
// pinned to the Kotlin `WidgetVisual.glow` and `baseHue` it replaces.
#include <cmath>
#include <cstdio>
#include <string>

#include "api/geode_api.h"
#include "viz/WidgetGlow.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
    WidgetLight l[kWidgetLights];

    // No cover, no style: the theme's hue 0.59, the default spread 0.10 around it.
    widgetGlow({-1.0f, 0.0f, false, 0, 0}, l);
    check(near(l[0].hueDegrees, 0.49f * 360.0f), "fallback hue, first light");
    check(near(l[1].hueDegrees, 0.59f * 360.0f), "fallback hue, middle light");
    check(near(l[2].hueDegrees, 0.69f * 360.0f), "fallback hue, last light");

    // At tick 0 the first light is at its seed plus the drift along x; the second is at angle 2.1.
    check(near(l[0].x, 0.37f) && near(l[0].y, 0.35f), "tick 0, first light");
    check(near(l[1].x, 0.70f + 0.12f * std::cos(2.1f)) && near(l[1].y, 0.60f + 0.12f * std::sin(2.1f)), "tick 0, second light");
    check(near(l[2].radius, 0.35f) && near(l[2].saturation, 0.40f) && near(l[2].value, 1.0f), "third light's look");

    // A full turn of ticks lands back on tick 0, and the lights move between ticks.
    WidgetLight turned[kWidgetLights];
    widgetGlow({-1.0f, 0.0f, false, 0, 12}, turned);
    check(near(turned[0].x, l[0].x) && near(turned[2].y, l[2].y), "tick 12 is tick 0");
    widgetGlow({-1.0f, 0.0f, false, 0, 5}, turned);
    check(!near(turned[0].x, l[0].x), "lights move on a tick");

    // A cover wins: its hue, its span clamped into 0.04..0.18.
    widgetGlow({0.50f, 0.50f, true, 7, 0}, l);
    check(near(l[1].hueDegrees, 180.0f), "cover hue");
    check(near(l[0].hueDegrees, 0.32f * 360.0f) && near(l[2].hueDegrees, 0.68f * 360.0f), "wide span clamps to 0.18");
    widgetGlow({0.50f, 0.0f, true, 7, 0}, l);
    check(near(l[2].hueDegrees, 0.54f * 360.0f), "narrow span clamps to 0.04");

    // The hue wraps like Kotlin's Float.mod: a cover at 0.02 puts the first light at 0.02 - 0.10 + 1.
    widgetGlow({0.02f, 0.10f, true, 0, 0}, l);
    check(near(l[0].hueDegrees, 0.92f * 360.0f), "hue below zero wraps");
    widgetGlow({0.98f, 0.10f, true, 0, 0}, l);
    check(near(l[2].hueDegrees, 0.08f * 360.0f), "hue past one wraps");

    // No cover, a style: a hue of its hash in 360 steps. "a" hashes to 97, "xyz" to 119193 (33 steps).
    widgetGlow({-1.0f, 0.0f, true, 97, 0}, l);
    check(near(l[1].hueDegrees, 97.0f), "style hue from its hash");
    widgetGlow({-1.0f, 0.0f, true, -119193, 0}, l);
    check(near(l[1].hueDegrees, 33.0f), "negative hash takes its magnitude");

    // The C ABI lays six floats per light.
    float out[18];
    check(geode_widget_glow(-1.0f, 0.0f, 0, 0, 0, out) == 3, "abi count");
    widgetGlow({-1.0f, 0.0f, false, 0, 0}, l);
    check(near(out[3], l[0].hueDegrees) && near(out[6], l[1].x) && near(out[17], l[2].value), "abi layout");
    check(geode_widget_glow(-1.0f, 0.0f, 0, 0, 0, nullptr) == 0, "abi null out");

    if (failures == 0) std::printf("widget_glow_test: ok\n");
    return failures == 0 ? 0 : 1;
}
