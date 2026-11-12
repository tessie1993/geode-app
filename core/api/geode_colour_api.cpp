// Your colours' C ABI (viz/Colour.hpp): the picker's conversions, the eyedropper's average and the
// palette swatches the shell draws. Pure, any thread; marshalled by geode_viz_jni.cpp.
#include <algorithm>
#include <cmath>
#include <string>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/Params.hpp"

using geode::viz::SceneParams;
namespace colour = geode::viz::colour;

namespace {

// `samples` colours of a ramp, t from 0 to 1.
template <typename At>
void sampleRamp(int samples, int* out, At at) {
    if (!out) return;
    for (int i = 0; i < samples; ++i) {
        out[i] = colour::pack(at(samples > 1 ? static_cast<float>(i) / static_cast<float>(samples - 1) : 0.0f));
    }
}


}  // namespace

extern "C" {

int geode_colour_from_hsv(float h, float s, float v) {
    if (!std::isfinite(h) || !std::isfinite(s) || !std::isfinite(v)) return colour::kNone;
    return colour::pack(colour::fromHsv(h, s, v));
}

void geode_colour_to_hsv(int rgb, float* out) {
    if (!out) return;
    const auto hsv = colour::toHsv(colour::unpack(rgb));
    std::copy(hsv.begin(), hsv.end(), out);
}

int geode_colour_parse_hex(const char* text) { return text ? colour::parseHex(text) : colour::kNone; }

const char* geode_colour_hex(int rgb) {
    thread_local std::string text;
    text = colour::hex(rgb);
    return text.c_str();
}

int geode_colour_average_argb(const int32_t* argb, int n) { return colour::averageArgb(argb, n); }

int geode_colour_palette_accent(float base, float range) {
    if (!std::isfinite(base) || !std::isfinite(range)) return colour::kNone;
    return colour::pack(colour::paletteAccent(base, range));
}

void geode_palette_ramp(const int* stops, int count, int samples, int* out) {
    if (!stops) return;
    const int n = std::clamp(count, 0, colour::kMaxStops);
    // The last sample sits a step short of the loop's end, so a swatch shows every stop and not the first twice.
    sampleRamp(samples, out, [&](float t) { return colour::ramp(stops, n, t * static_cast<float>(samples - 1) / static_cast<float>(samples)); });
}

void geode_palette_stops_from_hues(float base, float range, int count, int* out) {
    if (out) colour::stopsFromHues(base, range, count, out);
}

void geode_params_palette_preview(const float* params, int samples, int* out) {
    if (!params) return;
    const SceneParams p = SceneParams::fromWire(params);
    const auto stops = p.paletteStops();
    if (p.paletteStopCount() > 0) {
        geode_palette_ramp(stops.data(), p.paletteStopCount(), samples, out);
        return;
    }
    const float base = p.paletteBase();
    const float range = p.paletteRange();
    sampleRamp(samples, out, [&](float t) { return colour::procedural(base, range, t); });
}

}  // extern "C"
