#pragma once

#include <cstdint>
#include <limits>

// When a surface with no app around it draws: the live wallpaper and the dream (screensaver).
//
// One rule for both, in priority order:
// - no surface, or nobody can see it (another app in front, screen off, not dreaming): stopped;
// - covered by the shade or recents (visibility stays true there): stopped, except the preview;
// - the entitlement does not allow the moving visual (free, or the trial has run out): a still frame;
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

}  // namespace geode::viz::surface
