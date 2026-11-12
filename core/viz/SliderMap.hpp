#pragma once
#include <algorithm>
#include <cmath>

namespace geode::viz::slider {

// Where a Customize slider's thumb sits for a value, and the value a thumb position means. Pure; host-tested
// in core/tests/slider_map_test.cpp. The shell's Compose sliders hand over the raw position through
// geode_slider_* and send what comes back, so a step, a snap or a curve is decided here once.

// How far a value may sit from its default and still count as at it: the shell offers Reset only past this.
constexpr float kResetEpsilon = 1e-4f;

// True when Reset would move `value` to `def` (NaN for a control with no default).
inline bool offDefault(float value, float def) {
    return !std::isnan(def) && std::fabs(value - def) > kResetEpsilon;
}

// The ticks between the ends of a whole-number slider over first..last: one per value in between.
inline int intSteps(int first, int last) {
    return std::max(last - first - 1, 0);
}

// The whole number a stepped slider's position means, inside first..last. Rounded, not truncated: the
// position comes back from the track's pixel maths as a float a hair under or over its tick (2.9999998 for
// the third), and truncation would drop a whole step. NaN reads as 0, clamped.
inline int intValue(float position, int first, int last) {
    if (last < first) return first;
    const double p = std::isnan(position) ? 0.0 : static_cast<double>(position);
    return static_cast<int>(std::floor(std::clamp(p, static_cast<double>(first), static_cast<double>(last)) + 0.5));
}

// A log track needs 0 < lo < hi, both finite; outside that it pins to lo.
inline bool logSpan(float lo, float hi) {
    return std::isfinite(lo) && std::isfinite(hi) && lo > 0.0f && hi > lo;
}

// The 0..1 position of `value` on a log track over lo..hi: equal ratios take equal lengths, so a span from a
// twentieth of a second to a minute does not spend most of the track on the long end.
inline float logPosition(float value, float lo, float hi) {
    if (!logSpan(lo, hi) || std::isnan(value)) return 0.0f;
    const double v = std::clamp(value, lo, hi);
    const double t = std::log(v / lo) / std::log(static_cast<double>(hi) / lo);
    return static_cast<float>(std::clamp(t, 0.0, 1.0));
}

// The value at 0..1 `position` on a log track over lo..hi: lo * (hi / lo)^position, the inverse of logPosition.
inline float logValue(float position, float lo, float hi) {
    if (!logSpan(lo, hi)) return lo;
    const double t = std::isnan(position) ? 0.0 : std::clamp(static_cast<double>(position), 0.0, 1.0);
    return static_cast<float>(lo * std::pow(static_cast<double>(hi) / lo, t));
}

}  // namespace geode::viz::slider
