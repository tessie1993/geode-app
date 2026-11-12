// Host checks for the look's own palette in its params (core/viz/ParamsPalette): what clearing leaves, how many
// stops a look takes, the hue gradient's range and when a saved palette's id still means something. These were the
// Kotlin SceneParams helpers (withoutCustomPalette, withPaletteStops, usesCustomPalette) and PaletteStore's apply*.
// Built and run by CI (.github/workflows/native-host-tests.yml).
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "viz/Params.hpp"
#include "viz/ParamsPalette.hpp"

using namespace geode;
using viz::SceneParams;
namespace pp = viz::palette_params;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void paletteParams() {
    const SceneParams plain{};
    check(!pp::usesCustomPalette(plain), "the defaults draw the built-in palette");
    SceneParams p = plain;
    p.paletteBaseOverride = 0.2f;
    check(pp::usesCustomPalette(p), "a base override is the look's own palette");
    p = plain;
    p.paletteRangeOverride = 0.0f;
    check(pp::usesCustomPalette(p), "a span override of 0 is set");
    p = plain;
    p.paletteStop0 = 0x000000;
    check(pp::usesCustomPalette(p), "black is a colour");
    p = plain;
    p.paletteStop1 = 0xFF0000;
    check(!pp::usesCustomPalette(p), "a stop after an unset first does not count");

    const int six[] = {0x111111, 0x222222, 0x333333, 0x444444, 0x555555, 0x666666};
    SceneParams base = plain;
    base.palette = 4;
    base.paletteBaseOverride = 0.5f;
    const SceneParams stops = pp::withStops(base, six, 6);
    int out[5] = {};
    check(pp::stopsOf(stops, out) == 5, "a look takes five stops");
    check(out[0] == 0x111111 && out[4] == 0x555555, "in order from the first");
    check(stops.paletteBaseOverride == SceneParams::kUnsetOverride, "your colours clear the hue override");
    check(stops.palette == 4, "the built-in index is left alone");
    const SceneParams two = pp::withStops(stops, six, 2);
    check(pp::stopsOf(two, nullptr) == 2 && two.paletteStop2 == SceneParams::kNoStop, "fewer stops unset the rest");

    const SceneParams gradient = pp::withGradient(stops, 1.5f, -0.25f);
    check(gradient.paletteBaseOverride == 1.0f && gradient.paletteRangeOverride == 0.0f, "the gradient is held to 0..1");
    check(pp::stopsOf(gradient, nullptr) == 0, "a gradient drops your colours");
    const SceneParams nan = pp::withGradient(plain, std::numeric_limits<float>::quiet_NaN(), 0.3f);
    check(nan.paletteBaseOverride == 0.0f && std::fabs(nan.paletteRangeOverride - 0.3f) < 1e-6f, "NaN reads as 0");

    const SceneParams cleared = pp::withoutCustomPalette(gradient);
    check(!pp::usesCustomPalette(cleared), "clearing gives the built-in palette back");
    check(cleared.paletteRangeOverride == SceneParams::kUnsetOverride, "both overrides unset");
}

}  // namespace

int main() {
    paletteParams();
    if (failures == 0) std::printf("look files: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
