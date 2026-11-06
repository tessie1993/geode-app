#pragma once
#include "audio/dsp/Loudness.hpp"

namespace geode::audio {

// Where an export's loudness should end up, and what to do about a measured file (#422). Ported from the
// Kotlin LoudnessTargets: the windows, the gain and ceiling decision and which advice applies are logic, so
// they live here; the sentences stay Kotlin (export/LoudnessTarget.kt), picked by AdviceKind and filled
// with the numbers below. Pure, any thread, host-tested by core/tests/loudness_test.

// The three choices the export offers, in menu order. Any other value reads as AsIs, as an id from an older
// build always did (LoudnessTarget.byId falls back to the safest choice).
enum class LoudnessTargetId : int { YouTube = 0, Shorts = 1, AsIs = 2 };

// The level a platform plays back at, and the headroom it wants left underneath. lowLufs..highLufs is the
// band that counts as "already right": nobody should be told to re-export over a difference they cannot
// hear. aimLufs is where a normalising gain lands.
struct LoudnessWindow {
    double aimLufs;
    double lowLufs;
    double highLufs;
    double ceilingDbtp;
};

// The window a target normalises to; nullptr for AsIs (or an unknown id), which never moves the level.
const LoudnessWindow* loudnessWindow(LoudnessTargetId target);

// Which piece of advice applies. Kotlin keys its headline and paragraph on this, so the values are ABI
// (GEODE_LOUDNESS_ADVICE_* in geode_api.h).
enum class AdviceKind : int {
    NothingToMeasure = 0,  // every block under the -70 LUFS gate
    AsMixed = 1,           // Leave as-is: measured, nothing applied
    OnTarget = 2,          // in the band with the peak safe, or the gain would be inaudible
    Unchanged = 3,         // a normalise whose gain sits exactly on the tolerance: neither up nor down
    Raise = 4,
    RaiseLimited = 5,      // raised, but the ceiling stopped it short of the target level
    LowerClipping = 6,     // quiet, yet already over the ceiling: gain cannot help, only a limiter can
    LowerLoudLimited = 7,  // louder than the platform, and the ceiling took off more than the level needed
    LowerLoud = 8,         // genuinely louder than the platform plays at
    LowerPeak = 9,         // the level is fine; the ceiling is the only reason to touch it
};

struct LoudnessAdvice {
    AdviceKind kind = AdviceKind::NothingToMeasure;
    // Always the gain to apply on export (0 unless a normalise kind), so a caller that only wants the
    // number never has to look at the kind.
    double gainDb = 0.0;
    // What hitting the target level alone would have needed; differs from gainDb when the ceiling got in
    // the way.
    double gainForLevelDb = 0.0;
    double lufs = 0.0;
    double truePeakDbtp = 0.0;
    // A flat gain shifts every gated block by the same amount, so it shifts the integrated figure by exactly
    // that much; the only wobble is a block sitting on the -70 LUFS gate.
    double resultingLufs = 0.0;
    double resultingTruePeakDbtp = 0.0;
    // The target's ceiling (0 for AsIs), for the sentences that quote it.
    double ceilingDbtp = 0.0;
    // AsMixed only: how far under YouTube's level the file sits, when that is worth saying (over
    // kQuietHintLu); 0 otherwise.
    double quietUnderYouTubeLu = 0.0;
    // AsMixed only: the true peak is over YouTube's -1 dBTP ceiling.
    bool peakWarning = false;
};

// A gain smaller than this is inaudible; treating it as "on target" saves a pointless re-export.
inline constexpr double kGainToleranceDb = 0.1;
// A peak this far over the ceiling is rounding, not clipping.
inline constexpr double kPeakToleranceDb = 0.05;
// Under this much, "quieter than YouTube" is noise rather than information.
inline constexpr double kQuietHintLu = 1.0;

LoudnessAdvice adviseLoudness(const LoudnessReport& report, LoudnessTargetId target);

}  // namespace geode::audio
