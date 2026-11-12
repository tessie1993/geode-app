#include "output/LoudnessAdvice.hpp"

#include <cmath>

namespace geode::output::loudness {

namespace {
// Kotlin's minOf(Double, Double) (Math.min): NaN wins, and -0 is under +0.
double javaMin(double a, double b) {
    if (std::isnan(a)) return a;
    if (std::isnan(b)) return b;
    if (a == 0.0 && b == 0.0) return std::signbit(a) ? a : b;
    return a <= b ? a : b;
}
}  // namespace

bool windowOf(int target, Window* out) {
    Window w;
    switch (target) {
    case kYouTube: w = {-14.0, -15.0, -13.0, -1.0}; break;
    case kShortsAndTikTok: w = {-11.0, -12.0, -10.0, -1.0}; break;
    default: return false;
    }
    if (out) *out = w;
    return true;
}

Advice advise(int target, bool gated, double lufs, double truePeakDbtp) {
    Advice a;
    if (!gated) return a;
    Window w;
    if (!windowOf(target, &w)) {
        a.kind = kAsMixed;
        Window youTube;
        windowOf(kYouTube, &youTube);
        const double under = youTube.aimLufs - lufs;
        a.quietLu = under > kQuietHintLu ? under : 0.0;
        a.peakWarning = truePeakDbtp > youTube.ceilingDbtp;
        return a;
    }
    const double gainForLevel = w.aimLufs - lufs;
    // Never raise the peak past the ceiling, and pull it down even when the level is fine.
    const double gain = javaMin(gainForLevel, w.ceilingDbtp - truePeakDbtp);
    const bool inBand = lufs >= w.lowLufs && lufs <= w.highLufs;
    const bool peakSafe = truePeakDbtp <= w.ceilingDbtp + kPeakToleranceDb;
    if ((inBand && peakSafe) || std::abs(gain) < kGainToleranceDb) {
        a.kind = kOnTarget;
        return a;
    }
    a.kind = kNormalise;
    a.gainDb = gain;
    a.gainForLevelDb = gainForLevel;
    // A flat gain shifts every gated block by the same amount, so it shifts the integrated figure by exactly
    // that much; the only wobble is a block sitting on the -70 LUFS gate.
    a.resultingLufs = lufs + gain;
    a.resultingTruePeakDbtp = truePeakDbtp + gain;
    a.direction = gain > kGainToleranceDb ? 1 : (gain < -kGainToleranceDb ? -1 : 0);
    a.ceilingLimited = gain < gainForLevel - kGainToleranceDb;
    a.band = lufs < w.lowLufs ? -1 : (lufs > w.highLufs ? 1 : 0);
    return a;
}

}  // namespace geode::output::loudness
