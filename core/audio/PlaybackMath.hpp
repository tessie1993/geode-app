#pragma once

#include <cmath>
#include <cstdint>

// The player's small numbers: the pitch shift, the sleep timer's fade and the Settings › Playback
// slider steps. Ported from Kotlin's PlaybackMath (ADR 0003) so the shell holds no audio maths; the
// shell calls these through geode_playback_* and hands the results to Media3 and its sliders.
//
// Pure and header-only: no state, nothing to own, safe from any thread.
namespace geode::audio::playback {

// How long the sleep timer fades the volume to silence before it pauses: long enough to hear as a
// fade, short enough that the song does not drag on past the time the user set.
inline constexpr int64_t kSleepFadeMs = 3000;

// The pitch factor for a shift of semitones: 2^(semitones / 12), so +12 doubles and -12 halves.
// Media3's PlaybackParameters takes this factor (> 0) as its pitch.
inline float semitonesToRatio(float semitones) {
    return static_cast<float>(std::pow(2.0, static_cast<double>(semitones) / 12.0));
}

// The sleep timer's volume with remainingMs left: a straight line from 1 at fadeMs out to 0 at the
// end, clamped to 0..1. With no fade (fadeMs <= 0) it is full until the end, then silent.
inline float sleepFadeVolume(int64_t remainingMs, int64_t fadeMs = kSleepFadeMs) {
    if (fadeMs <= 0) return remainingMs > 0 ? 1.0f : 0.0f;
    const float v = static_cast<float>(remainingMs) / static_cast<float>(fadeMs);
    if (!(v > 0.0f)) return 0.0f;
    return v < 1.0f ? v : 1.0f;
}

// value rounded to the nearest multiple of step, halves rounding up (floor(x + 0.5), as Kotlin's
// roundToInt did), so a slider lands on its steps; a step <= 0 leaves value as it is.
inline float snap(float value, float step) {
    if (!(step > 0.0f)) return value;
    return static_cast<float>(std::floor(value / step + 0.5f)) * step;
}

}  // namespace geode::audio::playback
