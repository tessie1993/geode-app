// Host-side checks for the player's small numbers (core/audio/PlaybackMath, ported from Kotlin's
// PlaybackMath). Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the
// system compiler; no device. Goes through the C ABI the shell calls, so the export is pinned too.
//
// What is pinned: an octave doubles and halves the pitch factor; the sleep fade is full above its
// window, linear inside it, silent at the end, and with no window a hard cut; snap lands on the step,
// rounds halves up like Kotlin's roundToInt, and leaves the value alone for a step <= 0.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

void pitchRatio() {
    check(near(geode_playback_semitones_to_ratio(0.0f), 1.0f), "no shift is a factor of 1");
    check(near(geode_playback_semitones_to_ratio(12.0f), 2.0f), "an octave up doubles");
    check(near(geode_playback_semitones_to_ratio(-12.0f), 0.5f), "an octave down halves");
    check(near(geode_playback_semitones_to_ratio(6.0f), std::sqrt(2.0f)), "a tritone is root 2");
    check(geode_playback_semitones_to_ratio(-6.0f) > 0.0f, "the slider's floor is still a valid pitch");
}

void sleepFade() {
    const long long fade = geode_playback_sleep_fade_ms();
    check(fade == 3000, "the fade is 3 s");
    check(geode_playback_sleep_fade_volume(60000, fade) == 1.0f, "full volume before the fade");
    check(geode_playback_sleep_fade_volume(fade, fade) == 1.0f, "full at the fade's start");
    check(near(geode_playback_sleep_fade_volume(fade / 2, fade), 0.5f), "half way through, half volume");
    check(geode_playback_sleep_fade_volume(0, fade) == 0.0f, "silent at the end");
    check(geode_playback_sleep_fade_volume(-500, fade) == 0.0f, "past the end stays silent");
    check(geode_playback_sleep_fade_volume(1, 0) == 1.0f, "no fade: full while time is left");
    check(geode_playback_sleep_fade_volume(0, 0) == 0.0f, "no fade: silent at the end");
    check(geode_playback_sleep_fade_volume(0, -1) == 0.0f, "a negative fade is no fade");
}

void snapToStep() {
    check(near(geode_playback_snap(1.07f, 0.05f), 1.05f), "speed lands on 0.05");
    check(near(geode_playback_snap(-2.3f, 0.5f), -2.5f), "pitch lands on half semitones");
    check(near(geode_playback_snap(1120.0f, 250.0f), 1000.0f), "fade lands on 250 ms");
    check(near(geode_playback_snap(125.0f, 250.0f), 250.0f), "a half step rounds up");
    check(near(geode_playback_snap(-125.0f, 250.0f), 0.0f), "a negative half step rounds up too");
    check(geode_playback_snap(0.123f, 0.0f) == 0.123f, "a zero step leaves the value");
    check(geode_playback_snap(0.123f, -1.0f) == 0.123f, "a negative step leaves the value");
}

// ---- Added for #502's mutation pass. Seam: the geode_playback_* C calls the shell makes. Every
// expected value is a hand-worked literal, a musical fact or the deleted Kotlin's documented contract
// (Float.roundToInt: nearest, ties up), never the code's own formula. Each test names the mutant of
// core/audio/PlaybackMath.hpp (or its C wrapper) it kills.

bool nearTol(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Kills: semitones truncated to whole steps (static_cast<int>, lround) before the pow. The pitch
// slider moves in half semitones, and every older check used whole ones.
void pitchHalfSemitoneIsAQuarterTone() {
    check(nearTol(geode_playback_semitones_to_ratio(0.5f), 1.0293022f, 1e-6f), "+0.5 st is a quarter tone, 2^(1/24)");
    check(nearTol(geode_playback_semitones_to_ratio(-0.5f), 0.9715319f, 1e-6f), "-0.5 st is a quarter tone down");
    check(nearTol(geode_playback_semitones_to_ratio(1.0f), 1.0594631f, 1e-6f), "one semitone is 2^(1/12)");
}

// Kills: a clamp of the shift to the slider's +-6 or to one octave, and a wrong value on the downward
// side. The slider's own ends are a tritone (root 2 either way); two octaves are 4 and 1/4.
void pitchAtTheSemitoneExtremes() {
    check(nearTol(geode_playback_semitones_to_ratio(-6.0f), 0.70710678f, 1e-6f), "the slider's floor is 1/root 2");
    check(nearTol(geode_playback_semitones_to_ratio(6.0f), 1.41421356f, 1e-6f), "the slider's ceiling is root 2");
    check(nearTol(geode_playback_semitones_to_ratio(24.0f), 4.0f, 1e-5f), "two octaves up is 4");
    check(nearTol(geode_playback_semitones_to_ratio(-24.0f), 0.25f, 1e-6f), "two octaves down is a quarter");
}

// Kills: truncation to whole semitones (flat between half steps) and a sign flip (falling factor).
// Property over every slider position: a higher setting always sounds higher, and a setting an
// octave above another has exactly twice its factor (the musical definition of an octave).
void pitchRisesWithEverySliderStepAndOctavesDouble() {
    float prev = 0.0f;
    for (int half = -12; half <= 12; ++half) {  // -6 .. +6 st in the slider's 0.5 st steps
        const float st = static_cast<float>(half) * 0.5f;
        const float r = geode_playback_semitones_to_ratio(st);
        check(r > prev, "the pitch factor rises with every half-semitone step");
        prev = r;
        const float up = geode_playback_semitones_to_ratio(st + 12.0f);
        check(nearTol(up / r, 2.0f, 2e-6f), "an octave above any slider setting doubles its factor");
    }
}

// Kills: `fadeMs <= 0` -> `fadeMs == 0`. A negative fade must be "no fade" (full while time is
// left); the mutant divides by the negative length and goes silent at once. The older check only
// tried a negative fade with no time left, where the mutant also reads 0.
void sleepNegativeFadeIsFullUntilTheEnd() {
    check(geode_playback_sleep_fade_volume(5000, -1) == 1.0f, "negative fade, time left: full volume");
    check(geode_playback_sleep_fade_volume(1, -3000) == 1.0f, "negative fade, 1 ms left: still full");
    check(geode_playback_sleep_fade_volume(-5, -1) == 0.0f, "negative fade, past the end: silent");
}

// Kills: `remainingMs > 0` -> `remainingMs != 0` in the no-fade branch. Overshooting the end (the
// timer loop wakes late, so remaining is negative) must stay silent, not jump back to full.
void sleepNoFadeOvershootStaysSilent() {
    check(geode_playback_sleep_fade_volume(-1, 0) == 0.0f, "no fade, 1 ms past the end: silent");
    check(geode_playback_sleep_fade_volume(-60000, 0) == 0.0f, "no fade, a minute past: silent");
}

// Kills: the C call ignoring fade_ms and using the built-in 3 s (or the default argument). Every
// older in-window check used the 3 s fade itself, so a hard-wired 3000 passed them.
void sleepFadeFollowsTheGivenLength() {
    check(nearTol(geode_playback_sleep_fade_volume(3000, 6000), 0.5f, 1e-6f), "half of a 6 s fade is half volume");
    check(nearTol(geode_playback_sleep_fade_volume(500, 1000), 0.5f, 1e-6f), "half of a 1 s fade is half volume");
    check(geode_playback_sleep_fade_volume(1000, 1000) == 1.0f, "a 1 s fade starts at full");
}

// Kills: a smoothstep (or any S-curve symmetric about the middle) in place of the straight line;
// it also reads 0.5 at the midpoint, the only interior point the older checks tried.
void sleepFadeIsAStraightLine() {
    check(nearTol(geode_playback_sleep_fade_volume(750, 3000), 0.25f, 1e-6f), "a quarter of the fade left: quarter volume");
    check(nearTol(geode_playback_sleep_fade_volume(2250, 3000), 0.75f, 1e-6f), "three quarters left: three quarters");
}

// Kills: an off-by-one at either edge of the fade (remaining + 1 or - 1 in the numerator) and a cut
// to silence before the last millisecond. Both sides of the fade's start and of its end.
void sleepFadeEdgesOnBothSides() {
    check(geode_playback_sleep_fade_volume(3001, 3000) == 1.0f, "1 ms before the fade: full");
    check(nearTol(geode_playback_sleep_fade_volume(2999, 3000), 0.9996667f, 1e-6f), "1 ms into the fade: just under full");
    check(nearTol(geode_playback_sleep_fade_volume(1, 3000), 0.0003333f, 1e-6f), "1 ms left: barely audible, not cut");
    check(geode_playback_sleep_fade_volume(-1, 3000) == 0.0f, "1 ms past the end: silent");
}

// Kills: remaining_ms narrowed to 32 bits on the way in (a timer far ahead wraps negative and goes
// silent) and a dropped upper clamp. 1e12 ms is about 31 years.
void sleepFarFromTheEndIsFull() {
    check(geode_playback_sleep_fade_volume(86400000LL, 3000) == 1.0f, "a day left: full volume");
    check(geode_playback_sleep_fade_volume(1000000000000LL, 3000) == 1.0f, "a timer years ahead: full volume");
}

// Kills: a sign flip (1 - v) and a dropped clamp. Property: as the timer counts down 1 ms at a
// time the volume never rises and never leaves 0..1, so the fade never pumps back up.
void sleepVolumeNeverRisesAsTimeRunsOut() {
    float prev = 1.0f;
    bool monotone = true;
    bool bounded = true;
    for (long long left = 4000; left >= -20; --left) {
        const float v = geode_playback_sleep_fade_volume(left, 3000);
        if (v > prev) monotone = false;
        if (!(v >= 0.0f && v <= 1.0f)) bounded = false;
        prev = v;
    }
    check(monotone, "the sleep fade never gets louder as time runs out");
    check(bounded, "the sleep fade stays within 0..1");
}

// Kills: any nudge of the half (`+ 0.5f` -> `+ 0.50001f` or `+ 0.49999f`) and ties to even
// (nearbyint/rint). On the pitch slider's 0.5 st steps: the float just below 1.25 st goes down to
// 1.0, 1.25 itself and the float just above go up to 1.5 (ties up; ties-to-even would give 1.0).
// The same on the negative side, where "up" is toward zero.
void snapHalfThresholdOnBothSides() {
    const float tie = 1.25f;  // 2.5 steps of 0.5
    check(geode_playback_snap(std::nextafter(tie, 0.0f), 0.5f) == 1.0f, "just under a half step rounds down");
    check(geode_playback_snap(tie, 0.5f) == 1.5f, "exactly a half step rounds up");
    check(geode_playback_snap(std::nextafter(tie, 2.0f), 0.5f) == 1.5f, "just over a half step rounds up");
    check(geode_playback_snap(std::nextafter(-tie, -2.0f), 0.5f) == -1.5f, "just past a negative half step rounds away");
    check(geode_playback_snap(-tie, 0.5f) == -1.0f, "a negative half step rounds up, toward zero");
    check(geode_playback_snap(std::nextafter(-tie, 0.0f), 0.5f) == -1.0f, "just inside a negative half step rounds toward zero");
}

// Kills: the same half-nudge mutants on the crossfade slider (250 ms steps, 0..6000), and a push
// past its top: 6000 must stay 6000.
void snapFadeSliderStepsAndTop() {
    check(geode_playback_snap(374.0f, 250.0f) == 250.0f, "374 ms snaps down to 250");
    check(geode_playback_snap(375.0f, 250.0f) == 500.0f, "375 ms (a half step) snaps up to 500");
    check(geode_playback_snap(376.0f, 250.0f) == 500.0f, "376 ms snaps up to 500");
    check(geode_playback_snap(5874.0f, 250.0f) == 5750.0f, "5874 ms snaps down to 5750");
    check(geode_playback_snap(5876.0f, 250.0f) == 6000.0f, "5876 ms snaps up to the 6000 top");
    check(geode_playback_snap(6000.0f, 250.0f) == 6000.0f, "the top stays the top");
    check(geode_playback_snap(0.0f, 250.0f) == 0.0f, "no crossfade stays none");
}

// Kills: floor without the half (values below a step's midpoint fall one step short) and ceil.
// Property over the speed slider (0.5x .. 2x in 0.05 steps) with a fixed seed: every position lands
// within half a step, and snapping a snapped speed changes nothing.
void snapSpeedLandsOnTheNearestTwentieth() {
    std::uint32_t seed = 502u;  // fixed: a failure names the same inputs on every run
    bool nearest = true;
    bool idempotent = true;
    for (int i = 0; i < 20000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float x = 0.5f + 1.5f * static_cast<float>(seed >> 8) / 16777216.0f;
        const float s = geode_playback_snap(x, 0.05f);
        if (std::fabs(s - x) > 0.025f + 1e-5f) nearest = false;
        if (!near(geode_playback_snap(s, 0.05f), s)) idempotent = false;
    }
    check(nearest, "a speed snaps to a 0.05 step at most half a step away");
    check(idempotent, "snapping a snapped speed leaves it where it is");
    check(near(geode_playback_snap(0.5f, 0.05f), 0.5f), "the slowest speed stays 0.5x");
    check(near(geode_playback_snap(2.0f, 0.05f), 2.0f), "the fastest speed stays 2x");
    check(near(geode_playback_snap(1.024f, 0.05f), 1.0f), "1.024x lands on 1x");
    check(near(geode_playback_snap(1.026f, 0.05f), 1.05f), "1.026x lands on 1.05x");
}

// PARITY WITH THE DELETED KOTLIN. Kotlin's snap was (value / step).roundToInt() * step, and
// Float.roundToInt is Math.round on the JVM: the NEAREST integer, ties up, computed exactly (the
// JDK 7 fix of JDK-6430675; Android's OpenJDK libcore). 0.49999997f is nearest to 0, so Kotlin
// snapped the float just under 0.25 st to 0. floor(q + 0.5f) rounds q + 0.5f to 1.0f in float
// first, so the C++ port lands a whole step up here.
// Kills: the PR's first floor(x + 0.5f), which failed this; snap now compares q's fraction with 0.5.
void snapJustUnderAQuarterMatchesKotlin() {
    check(geode_playback_snap(std::nextafter(0.25f, 0.0f), 0.5f) == 0.0f,
          "PARITY: just under a quarter semitone snaps to 0, as Kotlin's roundToInt did");
    check(geode_playback_snap(std::nextafter(0.5f, 0.0f), 1.0f) == 0.0f,
          "PARITY: the float just under one half rounds to 0, as Math.round(0.49999997f) does");
}

}  // namespace

int main() {
    pitchRatio();
    sleepFade();
    snapToStep();
    pitchHalfSemitoneIsAQuarterTone();
    pitchAtTheSemitoneExtremes();
    pitchRisesWithEverySliderStepAndOctavesDouble();
    sleepNegativeFadeIsFullUntilTheEnd();
    sleepNoFadeOvershootStaysSilent();
    sleepFadeFollowsTheGivenLength();
    sleepFadeIsAStraightLine();
    sleepFadeEdgesOnBothSides();
    sleepFarFromTheEndIsFull();
    sleepVolumeNeverRisesAsTimeRunsOut();
    snapHalfThresholdOnBothSides();
    snapFadeSliderStepsAndTop();
    snapSpeedLandsOnTheNearestTwentieth();
    snapJustUnderAQuarterMatchesKotlin();
    if (failures == 0) std::printf("playback_math_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
