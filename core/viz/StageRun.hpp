#pragma once

#include <cstdint>

// When the app's own stage (the one visual the Activity hosts) draws, and who feeds Auto while it does not.
//
// The stage draws for as long as somebody can see it, which is not the same as the Activity being resumed:
// - resumed: it draws;
// - paused but still started, in a picture-in-picture window or on a second screen: it draws (Android pauses
//   an Activity in PiP and keeps showing it; the phone's own window may be behind a dialog while the second
//   screen still shows the visual);
// - paused otherwise (behind a dialog, losing focus), or stopped (another app in front, the screen off): it
//   does not. A stopped Activity's windows are hidden, the PiP window and the second screen's included.
//
// A cast draws on its own renderer for as long as its session lasts, whatever the stage does. The
// auto-director's style moments are counted by one surface only, so Auto steps once per moment: the stage
// while it draws, the cast's renderer while the stage does not.
//
// Pure and stateless: the shell restates on every change.
namespace geode::viz::stage {

// Bits of what the shell observed. Values are the ABI (GEODE_STAGE_* in geode_api.h).
enum Flag : uint32_t {
    kStarted = 1u << 0,
    kResumed = 1u << 1,
    kPip = 1u << 2,
    kSecondScreen = 1u << 3,
    kCasting = 1u << 4,
};

// Bits of the answer (GEODE_STAGE_DRAWS, GEODE_STAGE_CAST_LEADS_AUTO).
inline constexpr uint32_t kDraws = 1u << 0;
inline constexpr uint32_t kCastLeadsAuto = 1u << 1;

inline bool draws(uint32_t flags) {
    if ((flags & kStarted) == 0) return false;
    return (flags & (kResumed | kPip | kSecondScreen)) != 0;
}

inline bool castLeadsAuto(uint32_t flags) { return (flags & kCasting) != 0 && !draws(flags); }

inline uint32_t run(uint32_t flags) { return (draws(flags) ? kDraws : 0u) | (castLeadsAuto(flags) ? kCastLeadsAuto : 0u); }

}  // namespace geode::viz::stage
