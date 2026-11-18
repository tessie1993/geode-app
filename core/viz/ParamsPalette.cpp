#include "viz/ParamsPalette.hpp"

#include <algorithm>
#include <cmath>

#include "viz/Colour.hpp"

namespace geode::viz::palette_params {

namespace {

float unit(float v) { return std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f); }

void setStops(SceneParams& p, const int* stops, int count) {
    int* slots[] = {&p.paletteStop0, &p.paletteStop1, &p.paletteStop2, &p.paletteStop3, &p.paletteStop4};
    const int n = stops ? std::clamp(count, 0, colour::kMaxStops) : 0;
    for (int i = 0; i < colour::kMaxStops; ++i) *slots[i] = i < n ? stops[i] : SceneParams::kNoStop;
}

}  // namespace

bool usesCustomPalette(const SceneParams& p) {
    return p.paletteBaseOverride >= 0.0f || p.paletteRangeOverride >= 0.0f || p.paletteStop0 != SceneParams::kNoStop;
}

SceneParams withoutCustomPalette(SceneParams p) {
    p.paletteBaseOverride = SceneParams::kUnsetOverride;
    p.paletteRangeOverride = SceneParams::kUnsetOverride;
    setStops(p, nullptr, 0);
    return p;
}

SceneParams withStops(SceneParams p, const int* stops, int count) {
    p = withoutCustomPalette(p);
    setStops(p, stops, count);
    return p;
}

SceneParams withGradient(SceneParams p, float base, float span) {
    setStops(p, nullptr, 0);
    p.paletteBaseOverride = unit(base);
    p.paletteRangeOverride = unit(span);
    return p;
}

int stopsOf(const SceneParams& p, int* out) {
    const auto stops = p.paletteStops();
    int n = 0;
    while (n < colour::kMaxStops && stops[static_cast<size_t>(n)] != SceneParams::kNoStop) {
        if (out) out[n] = stops[static_cast<size_t>(n)];
        ++n;
    }
    return n;
}

}  // namespace geode::viz::palette_params
