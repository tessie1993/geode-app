#pragma once

#include <cstddef>
#include <cstdint>

#include "viz/frame/CastTeeRule.hpp"

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
// A cast carries the stage's frames while the stage draws, and draws on its own renderer only while it does not.
// The auto-director's style moments are counted by one surface only, so Auto steps once per moment: the stage
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

// Where the shell lays the stage's view out and how it hides it (ui/Crystal's VisualizerCanvasHost). Off Visuals the
// view keeps its last Visuals rect at alpha 0, so its surface survives. While a cast streams it keeps drawing on
// every tab, since the stream is its frames (core/viz/frame/CastTee):
// - at a faint alpha under the other screens. With Visuals never laid out this process (a cast started from the
//   Player's Share to PC tile or the system's output switcher) there is no rect yet, so it takes the window: a
//   0 x 0 view never drew, and the stream fell back to a second renderer that showed another visual until Visuals
//   was opened.
// - in its whole rect, no bars on the phone; the tee turns a portrait stage and crops it to the stream
//   (tee::place).
enum HostFlag : uint32_t {
    kHostShown = 1u << 0,    // Visuals is the open screen
    kHostCasting = 1u << 1,  // a cast or a computer link streams
};
enum HostAlpha : int {
    kHostHidden = 0,
    kHostFaint = 1,  // one 8-bit step: invisible, yet drawing
    kHostOpaque = 2,
};
struct Host {
    int alpha = kHostHidden;
    frame::tee::Rect rect;  // window pixels; empty: the view is 0 x 0
};

inline Host host(uint32_t flags, frame::tee::Rect bounds, int windowW, int windowH) {
    const bool laidOut = bounds.x1 > bounds.x0 && bounds.y1 > bounds.y0;
    const bool casting = (flags & kHostCasting) != 0;
    Host h;
    if (!laidOut && !(casting && windowW > 0 && windowH > 0)) return h;
    h.rect = laidOut ? bounds : frame::tee::Rect{0, 0, windowW, windowH};
    h.alpha = (flags & kHostShown) != 0 && laidOut ? kHostOpaque : casting ? kHostFaint : kHostHidden;
    return h;
}

// The connected display the stage moves to (HDMI, USB-C DisplayPort, a wireless display), and whether it does.
// A display qualifies when Android lets a Presentation show on it: it has FLAG_PRESENTATION, is not the
// built-in screen, and is not the display the app's own window is on (desktop windowing on a connected display
// can move the whole Activity there, and a presentation over its own task is refused). A display that refused
// or dropped the presentation kMaxDisplayFailures times running is skipped until it changes or the app starts
// again. The user's pick wins while it qualifies; else the first one switched on; else the first.
enum DisplayFlag : uint32_t {
    kDisplayPresentation = 1u << 0,
    kDisplayDefault = 1u << 1,
    kDisplayHost = 1u << 2,
    kDisplayOn = 1u << 3,
};
inline constexpr uint32_t kDisplayFailureShift = 8;  // failures so far, bits 8..15
inline constexpr uint32_t kMaxDisplayFailures = 3;

inline bool displayEligible(uint32_t flags) {
    if ((flags & kDisplayPresentation) == 0 || (flags & (kDisplayDefault | kDisplayHost)) != 0) return false;
    return ((flags >> kDisplayFailureShift) & 0xFFu) < kMaxDisplayFailures;
}

inline int pickDisplay(const int32_t* ids, const uint32_t* flags, size_t count, int32_t preferredId) {
    if (ids == nullptr || flags == nullptr) return -1;
    int first = -1;
    int firstOn = -1;
    for (size_t i = 0; i < count; ++i) {
        if (!displayEligible(flags[i])) continue;
        const int index = static_cast<int>(i);
        if (ids[i] == preferredId) return index;
        if (first < 0) first = index;
        if (firstOn < 0 && (flags[i] & kDisplayOn) != 0) firstOn = index;
    }
    return firstOn >= 0 ? firstOn : first;
}

// The stage shows on the picked display only while the user wants it there, the entitlement allows it (Cast's
// gate) and there is a display to show on.
enum PresentFlag : uint32_t {
    kPresentWanted = 1u << 0,
    kPresentEntitled = 1u << 1,
    kPresentFound = 1u << 2,
};

inline bool presents(uint32_t flags) {
    constexpr uint32_t all = kPresentWanted | kPresentEntitled | kPresentFound;
    return (flags & all) == all;
}

}  // namespace geode::viz::stage
