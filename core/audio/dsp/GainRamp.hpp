#pragma once
#include <cmath>
#include <cstdint>

namespace geode::audio {

// The level the playback gain treats as silence. A fade "to silence" ramps here, linear in dB, and the stage
// multiplies by exactly 0 once the sum of its lanes reaches it. -80 dB is 1e-4 of full scale, about three
// 16-bit LSBs, so the last step down to 0 is below what the output format can resolve; a lower floor would
// spend more of a fade's length in the inaudible tail, a higher one would leave an audible last step.
inline constexpr float kSilenceDb = -80.0f;

// Nothing in the chain boosts by more than this; a tag or setting past it is a mistake, not a wish.
inline constexpr float kMaxBoostDb = 24.0f;

// Clamps a requested level to [kSilenceDb, kMaxBoostDb]. NaN and -inf (Kotlin's "silent") mean silence.
inline float clampDb(float db) {
    if (!(db > kSilenceDb)) return kSilenceDb;
    return db > kMaxBoostDb ? kMaxBoostDb : db;
}

// dB to a linear multiplier; at or below kSilenceDb exactly 0.
inline float dbToGain(double db) {
    if (db <= static_cast<double>(kSilenceDb)) return 0.0f;
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

// One gain lane: a ramp that is linear in dB and counted in frames, so it is sample-accurate whatever the
// block sizes. Every frame of a ramp moves by the same step, and the last frame lands exactly on the target.
// Linear in dB because that is how loudness is heard: equal steps in dB sound like an even fade, where a
// linear-amplitude ramp (what the old 25 ms player.volume steps did) lingers near full and drops at the end.
// Audio thread only.
class GainRamp {
public:
    // Ramps from the current level to `targetDb` over `frames` frames; 0 frames jumps.
    void start(float targetDb, int64_t frames) {
        target_ = clampDb(targetDb);
        if (frames <= 0 || target_ == current_) {
            jump(target_);
            return;
        }
        left_ = frames;
        step_ = (target_ - current_) / static_cast<double>(frames);
    }

    void jump(float db) {
        current_ = target_ = clampDb(db);
        left_ = 0;
        step_ = 0.0;
    }

    // Advances one frame and returns that frame's level.
    double next() {
        if (left_ > 0) {
            --left_;
            // The last frame is the target itself, never target ± rounding.
            current_ = left_ == 0 ? target_ : current_ + step_;
        }
        return current_;
    }

    double db() const { return current_; }
    double target() const { return target_; }
    double step() const { return step_; }
    bool ramping() const { return left_ > 0; }
    int64_t framesLeft() const { return left_; }

private:
    double current_ = 0.0;
    double target_ = 0.0;
    double step_ = 0.0;
    int64_t left_ = 0;
};

// Milliseconds at a rate to frames, rounded; never negative.
inline int64_t msToFrames(double ms, int sampleRate) {
    if (!(ms > 0.0) || sampleRate <= 0) return 0;
    return static_cast<int64_t>(std::llround(ms * static_cast<double>(sampleRate) / 1000.0));
}

}  // namespace geode::audio
