#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

// When a surface with no app around it draws: the live wallpaper and the dream (screensaver).
//
// One rule for both, in priority order:
// - no surface, or nobody can see it (another app in front, screen off, not dreaming): stopped;
// - covered by the shade or recents (visibility stays true there): stopped, except the preview;
// - the entitlement does not allow the moving visual (free, or the trial has run out): a still frame;
// - reduced motion (the in-app Reduce motion switch, or the system's Remove animations): a still frame,
//   the preview included, since a preview that moves is still motion to someone it makes ill (ADR 0008);
// - the system wallpaper picker's preview: always moving, so picking it in a quiet room never
//   looks frozen;
// - battery saver (when the user keeps that option on) or silence (after core/sound/Silence's
//   grace, when the user keeps "rest when silent" on): a still frame;
// - otherwise: moving.
//
// The entitlement is a wall-clock time the visual may move until: kForever for Pro, the trial's end
// for a running trial, 0 for free. Nothing on the device announces a trial's end, so the caller asks
// recheckInMs how long to wait before restating the rule; the answer lands just past the end, so a
// wall clock that ticks a little late never reads the trial as still running (#376).
//
// A still frame is not one frame: a new scene is built across frames (ScenePrep), so the surface keeps
// drawing for settleMs after it enters STILL, and only then stops pacing. Each surface also draws at its
// own rate, one of kFpsChoices (ADR 0008: the wallpaper's options and Behaviour › Dream, default 30).
//
// Pure and stateless: the caller holds every input and restates on any change.
namespace geode::viz::surface {

enum class RunMode : int { Stopped = 0, Still = 1, Animated = 2 };

// Bits of what the caller observed. Values are the ABI (GEODE_SURFACE_* in geode_api.h).
enum Flag : uint32_t {
    kHasSurface = 1u << 0,
    kVisible = 1u << 1,
    kPreview = 1u << 2,
    kCovered = 1u << 3,
    kBatterySaver = 1u << 4,
    kPauseInBatterySaver = 1u << 5,
    kSilent = 1u << 6,
    kRestWhenSilent = 1u << 7,
    // Either reduced-motion source; the shell ORs them, the rule does not need to know which.
    kReducedMotion = 1u << 8,
};

inline constexpr int64_t kForever = std::numeric_limits<int64_t>::max();

// How far past the trial's end the restate lands.
inline constexpr int64_t kRecheckSlackMs = 1000;

inline bool has(uint32_t flags, Flag f) { return (flags & f) != 0; }

// Whether the entitlement lets the visual move at nowMs.
inline bool entitled(int64_t animateUntilMs, int64_t nowMs) { return nowMs < animateUntilMs; }

inline RunMode mode(uint32_t flags, int64_t animateUntilMs, int64_t nowMs) {
    if (!has(flags, kHasSurface) || !has(flags, kVisible)) return RunMode::Stopped;
    const bool preview = has(flags, kPreview);
    if (has(flags, kCovered) && !preview) return RunMode::Stopped;
    if (!entitled(animateUntilMs, nowMs)) return RunMode::Still;
    if (has(flags, kReducedMotion)) return RunMode::Still;
    if (preview) return RunMode::Animated;
    if (has(flags, kBatterySaver) && has(flags, kPauseInBatterySaver)) return RunMode::Still;
    if (has(flags, kSilent) && has(flags, kRestWhenSilent)) return RunMode::Still;
    return RunMode::Animated;
}

// Whether the surface should be hearing sound: whenever it would move if there were sound, cover
// aside. Hearing is what wakes a resting surface, and a second's glance at the shade should not
// release the capture and restart the silence clock.
inline bool listening(uint32_t flags, int64_t animateUntilMs, int64_t nowMs) {
    const uint32_t f = flags & ~static_cast<uint32_t>(kCovered) & ~static_cast<uint32_t>(kSilent);
    return mode(f, animateUntilMs, nowMs) == RunMode::Animated;
}

// Milliseconds until the entitlement changes the answer, plus the slack; -1 when it never will
// (Pro, free, or a trial already over).
inline int64_t recheckInMs(int64_t animateUntilMs, int64_t nowMs) {
    if (animateUntilMs == kForever || !entitled(animateUntilMs, nowMs)) return -1;
    return animateUntilMs - nowMs + kRecheckSlackMs;
}

// How long a surface keeps drawing after this restate, before it stops pacing: kStillSettleMs on the way
// into STILL, so a scene still being built finishes on screen rather than freezing black or half-built.
// 0 otherwise: already still, a restated look needs only one more frame; moving or stopped, the pacer
// is simply on or off.
inline constexpr int64_t kStillSettleMs = 2000;

inline int64_t settleMs(RunMode previous, RunMode now) {
    return now == RunMode::Still && previous != RunMode::Still ? kStillSettleMs : 0;
}

// The frame rates a surface may be set to. Whole divides of 60 and 120 Hz panels, so the pacer's
// whole-number divide (frame::Pacer) lands on them exactly; on a 90 Hz panel 30 and 15 divide too.
// A rate the pacer cannot reach evenly would be a setting that silently means something else.
inline constexpr std::array<int, 3> kFpsChoices = {15, 30, 60};
inline constexpr int kDefaultFps = 30;

// The stored choice as the pacer should use it: a listed rate as it is, anything else (a missing key,
// a value an older or newer build wrote) to the nearest listed rate, the lower one on a tie, and a
// non-positive value to the default.
inline int fpsChoice(int requested) {
    if (requested <= 0) return kDefaultFps;
    int best = kFpsChoices[0];
    for (const int c : kFpsChoices) {
        if (std::abs(c - requested) < std::abs(best - requested)) best = c;
    }
    return best;
}

}  // namespace geode::viz::surface
