// Your colours (Customize › Look, viz/Colour.hpp): the picker's HSV and hex, the eyedropper's average,
// the stops' loop the shader and the swatches draw, and the stops as scene params (the wire, the base
// hue the simulation styles read, the Palette roll that clears them).
#include <cmath>
#include <cstdio>
#include <string>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/ParamRandomizer.hpp"
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

bool near(float a, float b, float eps = 1.0f / 255.0f) { return std::fabs(a - b) <= eps; }

void hsvRoundTrips() {
    check(colour::pack(colour::fromHsv(0.0f, 1.0f, 1.0f)) == 0xFF0000, "hue 0 is red");
    check(colour::pack(colour::fromHsv(1.0f / 3.0f, 1.0f, 1.0f)) == 0x00FF00, "hue 1/3 is green");
    check(colour::pack(colour::fromHsv(2.0f / 3.0f, 1.0f, 1.0f)) == 0x0000FF, "hue 2/3 is blue");
    check(colour::pack(colour::fromHsv(1.25f, 1.0f, 1.0f)) == colour::pack(colour::fromHsv(0.25f, 1.0f, 1.0f)), "hue wraps");
    check(colour::pack(colour::fromHsv(0.4f, 0.0f, 0.5f)) == 0x808080, "no saturation is grey");
    check(colour::pack(colour::fromHsv(0.4f, 2.0f, -1.0f)) == 0x000000, "saturation and value clamp");
    for (int rgb : {0xFF0000, 0x12AB7F, 0x808080, 0xFFFFFF, 0x000000, 0x3355EE}) {
        const auto hsv = colour::toHsv(colour::unpack(rgb));
        check(colour::pack(colour::fromHsv(hsv[0], hsv[1], hsv[2])) == rgb, "HSV round trips " + colour::hex(rgb));
    }
    const auto grey = colour::toHsv(colour::unpack(0x808080));
    check(grey[0] == 0.0f && grey[1] == 0.0f, "a grey has hue 0 and no saturation");
}

void hexParsesAndFormats() {
    check(colour::parseHex("#12ab7F") == 0x12AB7F, "#RRGGBB, either case");
    check(colour::parseHex("12AB7F") == 0x12AB7F, "without the #");
    check(colour::parseHex(" #f80 ") == 0xFF8800, "#RGB doubles each digit, spaces ignored");
    check(colour::parseHex("#12AB7") == colour::kNone, "five digits are not a colour");
    check(colour::parseHex("#12AB7G") == colour::kNone, "nor a non-hex digit");
    check(colour::parseHex("") == colour::kNone, "nor nothing");
    check(colour::hex(0x0A0B0C) == "#0A0B0C", "hex keeps leading zeros");
    check(colour::hex(-1).empty() && colour::hex(0x1000000).empty(), "no hex for a value that is not a colour");
    check(std::string(geode_colour_hex(0xFFFFFF)) == "#FFFFFF", "the ABI formats");
    check(geode_colour_parse_hex(nullptr) == -1, "the ABI refuses null");
}

void eyedropperAverages() {
    const int32_t px[] = {static_cast<int32_t>(0xFF000000u | 0x102030), static_cast<int32_t>(0x80000000u | 0x304050)};
    check(geode_colour_average_argb(px, 2) == 0x203040, "the mean of the pixels, alpha ignored");
    check(geode_colour_average_argb(px, 0) == -1 && geode_colour_average_argb(nullptr, 3) == -1, "no pixels, no colour");
}

void theLoopHasNoSeam() {
    const int stops[] = {0xFF0000, 0x00FF00, 0x0000FF, colour::kNone, 0xFFFFFF};
    check(colour::count(stops, 5) == 3, "a gap ends the stops");
    const auto at = [&](float t) { return colour::pack(colour::ramp(stops, 5, t)); };
    check(at(0.0f) == 0xFF0000 && at(1.0f / 3.0f) == 0x00FF00 && at(2.0f / 3.0f) == 0x0000FF, "each stop sits at i/n");
    check(at(1.0f) == at(0.0f) && at(-0.25f) == at(0.75f), "t wraps: the last stop blends back into the first");
    check(at(1.0f / 6.0f) == colour::pack({0.5f, 0.5f, 0.0f}), "halfway between two stops is their mean");
    const colour::Rgb a = colour::ramp(stops, 3, 0.999f);
    check(near(a.r, 1.0f, 0.01f) && near(a.b, 0.0f, 0.01f) && near(a.g, 0.0f, 0.01f), "just before the end is nearly the first stop again");
    const int one[] = {0x336699};
    check(colour::pack(colour::ramp(one, 1, 0.42f)) == 0x336699, "one stop is a flat colour");
    int out[4] = {};
    geode_palette_ramp(stops, 3, 3, out);
    check(out[0] == 0xFF0000 && out[1] == 0x00FF00 && out[2] == 0x0000FF, "a swatch of n samples shows each of n stops once");
}

void aHueGradientBecomesStops() {
    int stops[5] = {};
    colour::stopsFromHues(0.07f, 0.1f, 5, stops);
    for (int i = 0; i < 5; ++i) {
        const colour::Rgb want = colour::procedural(0.07f, 0.1f, static_cast<float>(i) / 5.0f);
        const colour::Rgb got = colour::ramp(stops, 5, static_cast<float>(i) / 5.0f);
        check(near(got.r, want.r) && near(got.g, want.g) && near(got.b, want.b), "stop " + std::to_string(i) + " is the gradient there");
    }
}

void stopsAreSceneParams() {
    SceneParams p;
    check(p.paletteStopCount() == 0, "no stops by default");
    const float builtIn = p.paletteBase();
    p.paletteStop0 = 0x00FF00;
    p.paletteStop1 = 0x0000FF;
    check(p.paletteStopCount() == 2, "two stops set");
    check(near(p.paletteBase(), 1.0f / 3.0f, 1e-4f) && builtIn != p.paletteBase(), "the first stop's hue is the slot's base");
    check(near(p.paletteRange(), 1.0f / 3.0f, 1e-4f), "the span reaches the last stop's hue");
    p.paletteStop1 = 0xFFFFFF;
    float wire[SceneParams::kFieldCount];
    p.toWire(wire);
    const SceneParams back = SceneParams::fromWire(wire);
    check(back.paletteStop1 == 0xFFFFFF && back.paletteStop0 == 0x00FF00, "white rides the float wire exactly");
    check(SceneParams::spec(ParamSlot::paletteStop0).fade == ParamFade::Hold, "a stop never glides through other colours");

    int ramp[2] = {};
    geode_params_palette_preview(wire, 0, 2, ramp);
    check(ramp[0] == 0x00FF00 && ramp[1] == 0xFFFFFF, "the preview of slot 1 is its stops");

    SceneParams rolled = p;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (std::string(roll.key) != "Palette") continue;
        bool listsStops = false;
        for (const char* f : roll.fields) listsStops = listsStops || (f && std::string(f) == "paletteStop4");
        check(listsStops, "the Palette control owns the stops: Keep, Reset and borrow reach them");
    }
    rolled = randomizer::resetTab(p, randomizer::Tab::Color);
    check(rolled.paletteStopCount() == 0, "Reset takes your colours off with the palette");
}

}  // namespace

int main() {
    hsvRoundTrips();
    hexParsesAndFormats();
    eyedropperAverages();
    theLoopHasNoSeam();
    aHueGradientBecomesStops();
    stopsAreSceneParams();
    if (failures == 0) std::printf("colour_test: all checks passed\n");
    return failures;
}
