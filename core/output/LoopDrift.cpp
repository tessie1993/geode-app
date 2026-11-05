#include "output/LoopDrift.hpp"

#include <cmath>

namespace geode::output {

namespace {
// Kotlin's Float.coerceIn: comparisons only, so a NaN falls through both and comes back unchanged.
float coerceIn(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int clampStops(int stops) {
    if (stops < 1) return 1;
    if (stops > TimeOfDayDrift::kMaxStops) return TimeOfDayDrift::kMaxStops;
    return stops;
}
}  // namespace

bool TimeOfDayDrift::drifts() const { return clampStops(stops) > 1 && (hueTurns != 0.0f || warmth != 0.0f); }

std::vector<DriftStop> TimeOfDayDrift::stopPhases() const {
    if (!drifts()) return {DriftStop{}};
    const int n = clampStops(stops);
    const float last = static_cast<float>(n - 1);
    std::vector<DriftStop> phases(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float progress = static_cast<float>(i) / last;
        phases[static_cast<size_t>(i)] = DriftStop{i, hueTurns * progress, warmth * progress};
    }
    return phases;
}

int TimeOfDayDrift::stopsOver(int64_t totalMs, float hueTurns, float warmth) {
    if (hueTurns == 0.0f && warmth == 0.0f) return 1;
    // Long division, then a 32-bit narrowing that C++20 defines as modular, like Kotlin's Long.toInt().
    const auto count = static_cast<int32_t>(totalMs / kMinStopMs);
    return clampStops(count);
}

float driftColorShift(float colorShift, float hueOffset) {
    const float value = colorShift + hueOffset;
    const float fraction = value - std::floor(value);
    return coerceIn(fraction, 0.0f, 1.0f);
}

float driftTemperature(float temperature, float warmthOffset) {
    return coerceIn(temperature + warmthOffset, -1.0f, 1.0f);
}

}  // namespace geode::output
