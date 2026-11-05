#pragma once
#include <cstdint>
#include <vector>

namespace geode::output {

// A long-form render's time-of-day drift (#424): slow palette movement across the finished video, so a
// three-hour render is not one picture for three hours. The extend step repeats each encoded loop verbatim,
// so the drift is quantised: `stops` loops are rendered, each with its own palette offset, and each fills its
// share of the running time (LoopExtendPlan spreads the repeats over them). A step lands on a loop seam.
//
// It lives beside LoopPlan rather than in core/signal because a stop is a loop render, not a point on
// programme time: the offset is constant for every frame of one stop's loop, and the count is how many
// loops are rendered. The later modulator (#344, core/signal/modulators) calls these for its per-job offset
// and hands the two clamps below over to core/signal/params.
//
// Ported from export/LoopRender.kt's TimeOfDayDrift and DriftStop with the same float arithmetic. Kotlin keeps
// the data class the loop sheet and request carry (hueTurns, warmth, stops) and its constructor checks.
struct DriftStop {
    int index = 0;
    float hueOffset = 0.0f;     // turns added to SceneParams.colorShift
    float warmthOffset = 0.0f;  // added to SceneParams.temperature
};

struct TimeOfDayDrift {
    static constexpr int kMaxStops = 8;
    // One stop at most every twenty minutes: each stop is a whole extra loop render.
    static constexpr int64_t kMinStopMs = 20LL * 60'000;

    float hueTurns = 0.0f;
    float warmth = 0.0f;
    int stops = 1;

    // More than one stop and something to move. Kotlin's `drifts`.
    bool drifts() const;

    // The palette offset for each stop, from none at the first to the full drift at the last: stop i of n
    // gets hueTurns * (i / (n - 1)) and warmth likewise, in float. One stop, or nothing to move, is one stop
    // with no offset. Stops are taken as 1..kMaxStops (the Kotlin constructor rejects anything else).
    std::vector<DriftStop> stopPhases() const;

    // How many stops a drift over totalMs gets: totalMs / kMinStopMs (narrowed to 32 bits, as Kotlin's
    // toInt() did) clamped to 1..kMaxStops, and 1 when neither hue nor warmth moves. Kotlin's `over`.
    static int stopsOver(int64_t totalMs, float hueTurns, float warmth);
};

// colorShift + hueOffset wrapped into one turn: value - floor(value), then clamped to 0..1 (a NaN stays NaN,
// as Kotlin's coerceIn left it). Kotlin's DriftStop.wrapTurn.
float driftColorShift(float colorShift, float hueOffset);

// temperature + warmthOffset clamped to -1..1 (a NaN stays NaN).
float driftTemperature(float temperature, float warmthOffset);

}  // namespace geode::output
