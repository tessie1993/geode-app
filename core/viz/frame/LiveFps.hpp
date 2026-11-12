#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "viz/frame/Pacer.hpp"

// Settings › Performance's live frame-rate cap above 60 (owner decision of 2026-11-12: "Offer 90 and 120 in
// Performance"), and how a hot phone steps it down (ADR 0008: fps goes first). Pure, with no GL and no
// Android, so the host can test it (core/tests/live_fps_test.cpp).
//
// Live only: the in-app stage (and its PiP window and second screen). The wallpaper and the dream pace to
// their own choice (viz/SurfaceRun, kFpsChoices), an export to its own fixed step, Cast to its fixed clock;
// none of them reads this cap.
//
// Android keeps only the read of what the display can show: the current mode's refresh rate and its
// seamless alternatives (Display.Mode.getAlternativeRefreshRates), the set the pacer's two-argument
// Surface.setFrameRate can reach, since it asks for a seamless switch only.
namespace geode::viz::frame {

// The live caps, ascending: also the rungs a hot phone steps a cap down, one rung per thermal tier.
inline constexpr std::array<int, 4> kLiveFpsRungs = {30, 60, 90, 120};
// What the tab starts on and what any phone keeps: 60 is offered on every display, and so is 30 (Saver's).
inline constexpr int kLiveFpsDefault = 60;
// "Follow device" (the owner, 2026-11-12: "so fps follows the device settings"): stored as the live cap, and
// handed to the pacer as one. The stage draws every vsync of the rate the display runs now, which is the
// system's choice (Smooth display, battery saver, the panel's own modes), and states no rate of its own
// (Surface.setFrameRate(0): the platform's "no preference", which clears any standing request). Negative so
// no rate means it, and -1 so an older build reading it draws every vsync (it took a negative cap as none).
inline constexpr int kLiveFpsFollow = -1;
inline constexpr int kLiveFpsAlwaysUpTo = 60;
// How far a reported refresh rate, once divided, may sit from a cap and still show it (59.94 or 119.88 Hz
// modes): well inside what the pacer's divide absorbs (Pacer::kDivisorEpsilon, 5 % of the cap).
inline constexpr float kRefreshToleranceHz = 1.0f;
// A whole divide of a faster rate counts (a 240 Hz mode shows 120 by drawing every second vsync), up to
// the pacer's own limit of divides (Pacer::kMaxDivisor).
inline constexpr int kMaxRefreshDivide = 8;

// True when one of `rates` shows `fps` evenly: it is that rate, or a whole multiple of it, so the pacer's
// whole-number divide lands on it exactly. A rate the panel can only reach unevenly would be a setting
// that silently means something else (a 120 cap on a 90 Hz panel draws 90).
inline bool displayShows(int fps, const float* rates, int count) {
    if (fps <= 0 || !rates) return false;
    for (int i = 0; i < count; ++i) {
        const float r = rates[i];
        if (!std::isfinite(r) || r <= 0.0f) continue;
        const float divide = std::round(r / static_cast<float>(fps));
        if (divide < 1.0f || divide > static_cast<float>(kMaxRefreshDivide)) continue;
        if (std::abs(r / divide - static_cast<float>(fps)) <= kRefreshToleranceHz) return true;
    }
    return false;
}

// True when Settings › Performance offers `fps` on a display with these refresh rates: 30 and 60 on every
// display, as they always were (Saver's 30 and the default 60; setFrameRate asks a fast panel for its 60 Hz
// mode), 90 and 120 only where the display shows them.
inline bool liveFpsOffered(int fps, const float* rates, int count) {
    if (std::find(kLiveFpsRungs.begin(), kLiveFpsRungs.end(), fps) == kLiveFpsRungs.end()) return false;
    return fps <= kLiveFpsAlwaysUpTo || displayShows(fps, rates, count);
}

inline bool isLiveFpsFollow(float capFps) { return capFps == static_cast<float>(kLiveFpsFollow); }

// The caps offered, ascending, then Follow device, into out (up to capacity); returns how many there are.
// No rates (a display not yet attached, a platform that says nothing) offers 30, 60 and Follow: following
// needs no list, it reads the panel's vsync.
inline int liveFpsChoices(const float* rates, int count, int* out, int capacity) {
    int n = 0;
    for (const int fps : kLiveFpsRungs) {
        if (!liveFpsOffered(fps, rates, count)) continue;
        if (out && n < capacity) out[n] = fps;
        ++n;
    }
    if (out && n < capacity) out[n] = kLiveFpsFollow;
    return n + 1;
}

// The stored cap as this display runs it: an offered cap as it is; anything else (a 120 picked on the
// inner screen of a foldable now on its 60 Hz outer one, a value another build wrote) the nearest offered
// cap, the lower one on a tie; Follow device as it is; any other non-positive value the default. Snapped
// where it is applied, so the
// thermal steps below always start from a rate the panel shows: a 120 cap left on a 60 Hz panel would
// step to 90 when warm, which that panel still draws at 60, and the first rung of the ladder would be lost.
inline int liveFpsCap(int stored, const float* rates, int count) {
    if (stored == kLiveFpsFollow) return kLiveFpsFollow;
    if (stored <= 0) return kLiveFpsDefault;
    int best = 0;
    for (const int fps : kLiveFpsRungs) {
        if (!liveFpsOffered(fps, rates, count)) continue;
        if (best == 0 || std::abs(fps - stored) < std::abs(best - stored)) best = fps;
    }
    return best > 0 ? best : kLiveFpsDefault;
}

// The lowest rung: what any hot tier caps a free-running surface to, and the floor a cap steps down to.
inline constexpr float kThermalFloorFps = static_cast<float>(kLiveFpsRungs.front());

// A surface's cap under a thermal tier `rungsDown` steps hot (0 Full, 1 Eased, 2 Reduced, 3 Minimal;
// ThermalTierInfo::fpsRungsDown). Each hotter tier takes the cap one rung lower, never under the floor:
// 120 falls to 90, 60 and 30; 90 to 60 and 30; 60 to 30, as it always did. A cap at or under the floor
// (Cast's 30, the wallpaper's 15 or 24) is already as cheap as any rung and stays; a cap between rungs
// (none the tab offers) takes the rung under it as its first step. A free-running surface (<= 0, or not a
// number) takes the floor once the phone is warm, and stays free while it is cool. Not idempotent: apply
// it to the surface's own cap, never to a rate it already produced (ThermalGovernor keeps the two apart).
inline float thermalStepCapFps(float surfaceCapFps, int rungsDown) {
    const bool capped = std::isfinite(surfaceCapFps) && surfaceCapFps > 0.0f;
    if (rungsDown <= 0) return capped ? surfaceCapFps : 0.0f;
    if (!capped) return kThermalFloorFps;
    if (surfaceCapFps <= kThermalFloorFps) return surfaceCapFps;
    // The highest rung strictly under the cap is the first step down; the floor is under every cap here.
    int below = 0;
    for (size_t i = 0; i < kLiveFpsRungs.size(); ++i) {
        if (static_cast<float>(kLiveFpsRungs[i]) < surfaceCapFps) below = static_cast<int>(i);
    }
    const int rung = std::max(below - (rungsDown - 1), 0);
    return static_cast<float>(kLiveFpsRungs[static_cast<size_t>(rung)]);
}

// Follow device's cap for one vsync, from the pacer's own estimate of the panel (Pacer::trackPeriod, which
// hears every vsync whether or not it draws, so it follows the system's mode changes and needs no Display).
//
// Cool (rungsDown 0): the panel's rate as it is, to the half hertz the paced rate is read in, so the pacer
// draws every vsync (a 144 Hz panel draws 144, an odd rate included: the divide is 1, never uneven) and its
// paced slot names the real rate for MilkDrop's fps and the frame-time target. That rate is kept
// (followedMilliHz). Until the estimate settles: 0, every vsync.
//
// Hot: the kept rate stepped down like any cap (thermalStepCapFps: 120 to 90, 60, 30), and the pacer's whole
// divide of the panel under it keeps the frames even (a 120 Hz panel asked 90 draws 60). Stepped from the kept
// rate, not the panel as it is then: drawing fewer frames can let the system lower the panel itself (adaptive
// refresh rate), and stepping from that would walk a warm phone all the way down to the floor.
inline float followCapFps(PacerState& s, int rungsDown) {
    const bool settled = s.periodNs > 0 && s.samples >= Pacer::kSettleFrames;
    const int64_t panelMilliHz = settled ? std::llround(Pacer::panelHz(s) * 2.0) * 500 : 0;
    if (rungsDown <= 0) {
        if (panelMilliHz > 0) s.followedMilliHz = panelMilliHz;
        return static_cast<float>(panelMilliHz) / 1000.0f;
    }
    // A surface that starts hot keeps the first rate it measures, for the same reason.
    if (s.followedMilliHz <= 0) s.followedMilliHz = panelMilliHz;
    return thermalStepCapFps(static_cast<float>(s.followedMilliHz) / 1000.0f, rungsDown);
}

// The cap the pacer paces one vsync to: Follow device from the panel, any other cap stepped as it always was.
inline float pacerCapFps(PacerState& s, float surfaceCapFps, int rungsDown) {
    return isLiveFpsFollow(surfaceCapFps) ? followCapFps(s, rungsDown) : thermalStepCapFps(surfaceCapFps, rungsDown);
}

}  // namespace geode::viz::frame
