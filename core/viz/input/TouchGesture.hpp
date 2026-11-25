#pragma once
#include <algorithm>
#include <cmath>

namespace geode::viz::touch {

// The maths of the fingers on the visual (was VisualGestures.kt): where touch play's fingers land in the scenes'
// frame, what two fingers have done (the pinch and twist geode_touch_zoom / geode_touch_rotation then apply), and
// touch play's stir. The shell keeps only the pointer loop and which gesture a mapped action belongs to.

constexpr float kPi = 3.14159265358979f;

// The stir: two points opposite each other circling the centre once, a step per kPulseFrameMs.
constexpr int kPulseSteps = 36;
constexpr int kPulseFrameMs = 33;
constexpr float kPulseRadius = 0.35f;

// An angle brought into [-pi, pi]; one that is not finite is left alone.
inline float wrap(float a) {
    if (!std::isfinite(a)) return a;
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

// points (x, y) pairs in px of a width x height view, in place, into y-up NDC; a side under 1 px reads as 1.
inline void toNdc(float* xy, int points, float width, float height) {
    if (!xy) return;
    const float w = std::max(width, 1.0f);
    const float h = std::max(height, 1.0f);
    for (int i = 0; i < points; ++i) {
        xy[i * 2] = xy[i * 2] / w * 2.0f - 1.0f;
        xy[i * 2 + 1] = 1.0f - xy[i * 2 + 1] / h * 2.0f;
    }
}

// What two fingers did, each pair given as (ax, ay, bx, by) px.
struct PairReading {
    float panX = 0.0f, panY = 0.0f;  // the midpoint's travel since the pair came down
    float zoom = 1.0f;               // spread now over spread then (that one read as at least 1 px)
    float twist = 0.0f;              // radians since the pair came down, wrapped
    float stepFactor = 1.0f;         // the pinch since the last event, as a scale factor
    float stepDegrees = 0.0f;        // the twist since the last event
};

inline float pairSpan(const float* p) { return std::hypot(p[2] - p[0], p[3] - p[1]); }
inline float pairAngle(const float* p) { return std::atan2(p[3] - p[1], p[2] - p[0]); }

// The step is taken from the last event, never from the start, so a pinch recognised late does not jump.
inline PairReading readPair(const float* start, const float* last, const float* now) {
    PairReading r;
    if (!start || !last || !now) return r;
    const float spanStart = std::max(pairSpan(start), 1.0f);
    const float angleStart = pairAngle(start);
    r.panX = (now[0] + now[2]) * 0.5f - (start[0] + start[2]) * 0.5f;
    r.panY = (now[1] + now[3]) * 0.5f - (start[1] + start[3]) * 0.5f;
    r.zoom = pairSpan(now) / spanStart;
    r.twist = wrap(pairAngle(now) - angleStart);
    // Fingers that met last event give no factor (Kotlin divided by zero; Zoom ignored the result anyway).
    const float lastSpan = pairSpan(last);
    r.stepFactor = lastSpan > 0.0f ? r.zoom / (lastSpan / spanStart) : 1.0f;
    r.stepDegrees = wrap(r.twist - wrap(pairAngle(last) - angleStart)) * (180.0f / kPi);
    return r;
}

// The stir's step: its two points (4 floats, NDC) and how long to hold them in ms; 0, xy untouched, once over.
inline int pulse(int step, float* xy) {
    if (!xy || step < 0 || step >= kPulseSteps) return 0;
    const float a = static_cast<float>(step) / kPulseSteps * 2.0f * kPi;
    xy[0] = kPulseRadius * std::cos(a);
    xy[1] = kPulseRadius * std::sin(a);
    xy[2] = -xy[0];
    xy[3] = -xy[1];
    return kPulseFrameMs;
}

}  // namespace geode::viz::touch
