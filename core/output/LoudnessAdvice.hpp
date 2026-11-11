#pragma once

// What to do about an export's measured loudness (core/output; ADR 0003): the gain a platform target asks
// for, held under its true-peak ceiling, and whether the file is already in the band that counts as right.
// Ported from the Kotlin LoudnessTargets.normalise and the decisions inside LoudnessAdvice
// (export/LoudnessTarget.kt), which keep only the wording. The gain is advice: nothing applies it yet.
namespace geode::output::loudness {

// The targets, numbered as the shell's menu (LoudnessTarget.nativeIndex).
enum Target : int { kLeaveAsIs = 0, kYouTube = 1, kShortsAndTikTok = 2 };

// The level a platform plays back at: low..high counts as already right, aim is where a gain lands, and
// ceiling is the true-peak headroom it wants.
struct Window {
    double aimLufs = 0.0;
    double lowLufs = 0.0;
    double highLufs = 0.0;
    double ceilingDbtp = 0.0;
};

// False for a target that does not normalise (Leave as-is) or an unknown one.
bool windowOf(int target, Window* out);

enum Kind : int { kNothingToMeasure = 0, kAsMixed = 1, kOnTarget = 2, kNormalise = 3 };

// A gain smaller than this is inaudible; a peak this far over the ceiling is rounding; under this much
// quieter than YouTube is not worth saying.
constexpr double kGainToleranceDb = 0.1;
constexpr double kPeakToleranceDb = 0.05;
constexpr double kQuietHintLu = 1.0;

struct Advice {
    Kind kind = kNothingToMeasure;
    double gainDb = 0.0;          // the gain to apply; 0 unless kNormalise
    double gainForLevelDb = 0.0;  // what the level alone would have needed (kNormalise)
    double resultingLufs = 0.0;
    double resultingTruePeakDbtp = 0.0;
    int direction = 0;            // kNormalise: 1 up, -1 down, 0 within the tolerance
    bool ceilingLimited = false;  // kNormalise: the ceiling stopped the gain short of the level
    int band = 0;                 // kNormalise: -1 under the window's low, 1 over its high, 0 inside
    double quietLu = 0.0;         // kAsMixed: LU under YouTube's aim when more than kQuietHintLu, else 0
    bool peakWarning = false;     // kAsMixed: the true peak is over YouTube's ceiling
};

// gated false: the meter found nothing above its gate (silence).
Advice advise(int target, bool gated, double lufs, double truePeakDbtp);

}  // namespace geode::output::loudness
