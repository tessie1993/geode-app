#include "audio/dsp/LoudnessTarget.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio {

namespace {

// YouTube plays at about -14 LUFS with 1 dB of true-peak headroom; short-form feeds sit phone-speaker loud,
// about -10 to -12. The Kotlin LoudnessTarget windows, unchanged.
constexpr LoudnessWindow kYouTube{-14.0, -15.0, -13.0, -1.0};
constexpr LoudnessWindow kShorts{-11.0, -12.0, -10.0, -1.0};

LoudnessAdvice asMixed(double lufs, double truePeakDbtp) {
    LoudnessAdvice a;
    a.kind = AdviceKind::AsMixed;
    a.lufs = lufs;
    a.truePeakDbtp = truePeakDbtp;
    const double underYouTube = kYouTube.aimLufs - lufs;
    a.quietUnderYouTubeLu = underYouTube > kQuietHintLu ? underYouTube : 0.0;
    a.peakWarning = truePeakDbtp > kYouTube.ceilingDbtp;
    return a;
}

// Which way the gain moves the file and why; only reached once |gain| is at least the tolerance.
AdviceKind normaliseKind(const LoudnessWindow& w, double lufs, double gain, double gainForLevel) {
    const bool ceilingLimited = gain < gainForLevel - kGainToleranceDb;
    if (gain > kGainToleranceDb) return ceilingLimited ? AdviceKind::RaiseLimited : AdviceKind::Raise;
    if (gain >= -kGainToleranceDb) return AdviceKind::Unchanged;
    if (lufs < w.lowLufs) return AdviceKind::LowerClipping;
    if (lufs > w.highLufs) return ceilingLimited ? AdviceKind::LowerLoudLimited : AdviceKind::LowerLoud;
    return AdviceKind::LowerPeak;
}

LoudnessAdvice normalise(const LoudnessWindow& w, double lufs, double truePeakDbtp) {
    LoudnessAdvice a;
    a.lufs = lufs;
    a.truePeakDbtp = truePeakDbtp;
    a.ceilingDbtp = w.ceilingDbtp;
    const double gainForLevel = w.aimLufs - lufs;
    // Never raise the peak past the ceiling, and pull it down even when the level is fine.
    const double gainForCeiling = w.ceilingDbtp - truePeakDbtp;
    const double gain = std::min(gainForLevel, gainForCeiling);
    const bool inBand = lufs >= w.lowLufs && lufs <= w.highLufs;
    const bool peakSafe = truePeakDbtp <= w.ceilingDbtp + kPeakToleranceDb;
    if ((inBand && peakSafe) || std::fabs(gain) < kGainToleranceDb) {
        a.kind = AdviceKind::OnTarget;
        return a;
    }
    a.kind = normaliseKind(w, lufs, gain, gainForLevel);
    a.gainDb = gain;
    a.gainForLevelDb = gainForLevel;
    a.resultingLufs = lufs + gain;
    a.resultingTruePeakDbtp = truePeakDbtp + gain;
    return a;
}

}  // namespace

const LoudnessWindow* loudnessWindow(LoudnessTargetId target) {
    switch (target) {
        case LoudnessTargetId::YouTube:
            return &kYouTube;
        case LoudnessTargetId::Shorts:
            return &kShorts;
        default:
            return nullptr;
    }
}

LoudnessAdvice adviseLoudness(const LoudnessReport& report, LoudnessTargetId target) {
    if (!report.hasIntegrated) return {};
    const LoudnessWindow* w = loudnessWindow(target);
    return w ? normalise(*w, report.integratedLufs, report.truePeakDbtp)
             : asMixed(report.integratedLufs, report.truePeakDbtp);
}

}  // namespace geode::audio
