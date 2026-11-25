#pragma once
#include <cmath>

// Tap along (Visuals): taps on the visual in time with the song roll new Music moments and reactions.
// Pure, so core/tests/tap_along_test.cpp drives it; the renderer hands it each frame's beat as the sound
// engine heard it (frame()), the shell each tap's touch-down time (tap()).
//
// Read only: the taps are compared with the beat the engine already tracks and never reach Rhythm. No tap
// tempo, no nudge, no lock: nothing here is written back anywhere.
//
// A tap is on the beat when it lands within kEarlyS before to kLateS after one (late is wider: the engine
// hears the samples before the speaker plays them). kTaps on-beat taps in a row fire once, when they also
// keep one stride (every beat, or every other) and one feel: each tap's offset from its beat stays within
// kSpreadS of the streak's mean offset. Anything else (an off-beat tap, two taps in one beat, a changed stride, a
// frame too old, a beat the tracker is unsure of, silence) breaks the streak.
namespace geode::viz {

class TapAlong {
public:
    static constexpr int kTaps = 4;
    static constexpr double kEarlyS = 0.08;
    static constexpr double kLateS = 0.16;
    static constexpr double kSpreadS = 0.06;
    static constexpr double kFrameAgeS = 0.25;
    static constexpr float kMinConf = 0.35f;     // the tracker's own "locked" line (SoundFrame.h)
    static constexpr float kMinPresence = 0.5f;  // music, not the silence fade

    // The frame's beat at t seconds (CLOCK_MONOTONIC): beatPhase 0..1, bpm, beatConf and presence.
    void frame(double t, float beatPhase, float bpm, float beatConf, float presence) {
        frameT_ = t;
        phase_ = beatPhase;
        period_ = bpm > 0.0f ? 60.0 / bpm : 0.0;
        heard_ = beatConf >= kMinConf && presence >= kMinPresence && period_ > 0.0;
        seen_ = true;
    }

    // A touch-down at t seconds, same clock. True on the tap that completes a streak.
    bool tap(double t) {
        if (!seen_ || !heard_ || std::fabs(t - frameT_) > kFrameAgeS) return breakStreak();
        const double phase = phase_ + (t - frameT_) / period_;
        const double frac = phase - std::floor(phase);
        const double offset = (frac < 0.5 ? frac : frac - 1.0) * period_;  // + late, - early
        if (offset < -std::fmin(kEarlyS, 0.2 * period_) || offset > std::fmin(kLateS, 0.35 * period_)) return breakStreak();
        if (streak_ > 0) {
            const double beats = (t - lastT_) / period_;
            const double stride = std::round(beats);
            const bool steady = stride >= 1.0 && stride <= 2.0 && (streak_ < 2 || stride == stride_) &&
                                std::fabs(offset - sum_ / streak_) <= kSpreadS;
            if (steady) {
                stride_ = stride;
                lastT_ = t;
                sum_ += offset;
                if (++streak_ < kTaps) return false;
                streak_ = 0;
                return true;
            }
        }
        streak_ = 1;  // this tap is on a beat: it starts a new streak
        sum_ = offset;
        lastT_ = t;
        stride_ = 0.0;
        return false;
    }

private:
    bool breakStreak() {
        streak_ = 0;
        return false;
    }

    bool seen_ = false;
    bool heard_ = false;
    double frameT_ = 0.0;
    double phase_ = 0.0;
    double period_ = 0.0;
    int streak_ = 0;
    double sum_ = 0.0;  // the streak's offsets, for their mean
    double lastT_ = 0.0;
    double stride_ = 0.0;
};

}  // namespace geode::viz
