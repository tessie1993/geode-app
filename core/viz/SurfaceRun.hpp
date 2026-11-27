#pragma once

#include <cstdint>

// What a stage's run rule answers (core/stage/StageLifecycle).
namespace geode::viz::surface {

// Dark: no visual; the shell covers it in black and draws no frames.
enum class RunMode : int { Stopped = 0, Still = 1, Animated = 2, Dark = 3 };

// How long a stage keeps drawing after this restate, before it stops pacing: kStillSettleMs on the way
// into STILL, so a scene still being built finishes on screen rather than freezing black or half-built.
// 0 otherwise: already still, a restated look needs only one more frame; moving or stopped, the pacer
// is simply on or off.
inline constexpr int64_t kStillSettleMs = 2000;

inline int64_t settleMs(RunMode previous, RunMode now) {
    return now == RunMode::Still && previous != RunMode::Still ? kStillSettleMs : 0;
}

}  // namespace geode::viz::surface
