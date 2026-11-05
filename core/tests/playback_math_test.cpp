// Host-side checks for the player's small numbers (core/audio/PlaybackMath, ported from Kotlin's
// PlaybackMath). Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the
// system compiler; no device. Goes through the C ABI the shell calls, so the export is pinned too.
//
// What is pinned: an octave doubles and halves the pitch factor; the sleep fade is full above its
// window, linear inside it, silent at the end, and with no window a hard cut; snap lands on the step,
// rounds halves up like Kotlin's roundToInt, and leaves the value alone for a step <= 0.
#include <cmath>
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
    check(near(geode_playback_snap(1130.0f, 250.0f), 1000.0f), "fade lands on 250 ms");
    check(near(geode_playback_snap(125.0f, 250.0f), 250.0f), "a half step rounds up");
    check(near(geode_playback_snap(-125.0f, 250.0f), 0.0f), "a negative half step rounds up too");
    check(geode_playback_snap(0.123f, 0.0f) == 0.123f, "a zero step leaves the value");
    check(geode_playback_snap(0.123f, -1.0f) == 0.123f, "a negative step leaves the value");
}

}  // namespace

int main() {
    pitchRatio();
    sleepFade();
    snapToStep();
    if (failures == 0) std::printf("playback_math_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
