// Host-side checks for Settings › Playback's speed and pitch rows (core/audio/dsp/PlaybackControls) and their
// C ABI (geode_playback_pitch_ratio, geode_playback_snap, geode_playback_error_action). Built and run by CI ("Native host tests") with the
// system compiler; no device.
//
// What is pinned: a semitone is a twelfth of an octave on the rate Media3 plays at, and a slider value lands
// on its step the way the rows always stored it (a half step rounds up), so saved speeds and pitches read
// back as they were.
#include <cmath>
#include <cstdio>
#include <limits>

#include "api/geode_api.h"
#include "audio/dsp/PlaybackControls.hpp"

using geode::audio::pitchRatio;
using geode::audio::snapToStep;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) <= 1e-5f * std::fmax(1.0f, std::fabs(b)); }

void semitonesAreTwelfthsOfAnOctave() {
    check(pitchRatio(0.0f) == 1.0f, "no shift plays at the recorded pitch");
    check(near(pitchRatio(12.0f), 2.0f) && near(pitchRatio(-12.0f), 0.5f), "an octave doubles or halves");
    check(near(pitchRatio(7.0f), 1.4983071f), "a fifth up is 2^(7/12)");
    check(near(pitchRatio(-0.5f), 0.97153194f), "half a semitone down");
    check(near(pitchRatio(1.0f) * pitchRatio(1.0f), pitchRatio(2.0f)), "shifts add");
}

void slidersLandOnTheirStep() {
    check(near(snapToStep(1.37f, 0.05f), 1.35f), "speed on a 0.05 step");
    check(near(snapToStep(1.38f, 0.05f), 1.4f), "the nearer step wins");
    check(near(snapToStep(-3.3f, 0.5f), -3.5f), "pitch on a 0.5 step, below zero");
    check(snapToStep(-0.25f, 0.5f) == 0.0f, "a half step rounds up, toward +infinity");
    check(snapToStep(0.25f, 0.5f) == 0.5f, "a half step rounds up above zero too");
    check(snapToStep(1234.0f, 10.0f) == 1230.0f && snapToStep(1236.0f, 10.0f) == 1240.0f, "picture delay on 10 ms");
    check(snapToStep(1880.0f, 250.0f) == 2000.0f, "a fade on 250 ms");
    check(snapToStep(0.33f, 0.0f) == 0.33f && snapToStep(0.33f, -1.0f) == 0.33f, "no step leaves the value alone");
    const float inf = std::numeric_limits<float>::infinity();
    check(snapToStep(0.33f, inf) == 0.33f, "an infinite step leaves the value alone");
    check(std::isnan(snapToStep(std::numeric_limits<float>::quiet_NaN(), 0.5f)), "NaN stays NaN");
    check(snapToStep(inf, 0.5f) == inf, "an infinite value is left alone");
}

void rangesHoldTheSavedOptions() {
    using namespace geode::audio;
    check(clampControl(kCrossfadeMs, 60000.0f) == 12000.0f, "a crossfade is held to 12 s");
    check(clampControl(kCrossfadeMs, -500.0f) == 0.0f, "a crossfade is never negative");
    check(clampControl(kCrossfadeMs, 4000.0f) == 4000.0f, "a crossfade in range is left alone");
    check(clampControl(kFadeMs, 9000.0f) == 6000.0f && clampControl(kFadeMs, -1.0f) == 0.0f, "a fade is 0..6 s");
    check(clampControl(kRepeatMode, 7.0f) == 2.0f && clampControl(kRepeatMode, -3.0f) == 0.0f, "repeat mode 0..2");
    check(clampControl(kSpeed, 9.0f) == 2.0f && clampControl(kSpeed, 0.1f) == 0.5f, "speed 0.5..2");
    check(clampControl(kPitchSemitones, 77.0f) == 6.0f && clampControl(kPitchSemitones, -77.0f) == -6.0f, "pitch is +-6");
    check(clampControl(kReplayGainMode, 5.0f) == 2.0f, "ReplayGain mode 0..2");
    check(clampControl(kReplayGainPreampDb, 93.0f) == 15.0f && clampControl(kReplayGainPreampDb, -93.0f) == -15.0f,
          "preamp is +-15 dB");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(clampControl(kSpeed, nan) == 1.0f && clampControl(kPitchSemitones, nan) == 0.0f, "NaN becomes the default");
    check(clampControl(99, 3.5f) == 3.5f && clampControl(-1, 3.5f) == 3.5f, "an unknown control is left alone");
    check(controlRange(99).max == 0.0f, "an unknown control has no range");
    check(controlRange(kSpeed).step == 0.05f && controlRange(kFadeMs).step == 250.0f, "the steps the rows move by");
}

void crossfadeSliderRoundsToWholeSeconds() {
    using geode::audio::crossfadeMsFromSeconds;
    check(crossfadeMsFromSeconds(0.0f) == 0 && crossfadeMsFromSeconds(0.4f) == 0, "Off below half a second");
    check(crossfadeMsFromSeconds(0.6f) == 1000, "0.6 s is one second");
    check(crossfadeMsFromSeconds(5.2f) == 5000, "5.2 s is five");
    check(crossfadeMsFromSeconds(12.0f) == 12000, "the longest");
    check(crossfadeMsFromSeconds(13.0f) == 12000, "never past the longest");
    check(crossfadeMsFromSeconds(std::numeric_limits<float>::max()) == 12000, "a huge value does not overflow");
    check(crossfadeMsFromSeconds(-3.0f) == 0, "never below 0");
    check(crossfadeMsFromSeconds(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN is Off");
}

void aFailingTrackSkipsOrStops() {
    using geode::audio::ErrorAction;
    using geode::audio::errorAction;
    check(errorAction(1, true) == ErrorAction::SkipToNext, "one failure with a next track skips");
    check(errorAction(2, true) == ErrorAction::SkipToNext, "two failures in a row still skip");
    check(errorAction(3, true) == ErrorAction::StopSourceUnavailable, "three in a row stop");
    check(errorAction(7, true) == ErrorAction::StopSourceUnavailable, "more than three stop too");
    check(errorAction(1, false) == ErrorAction::StopEndOfQueue, "nothing queued stops");
    check(errorAction(2, false) == ErrorAction::StopEndOfQueue, "nothing queued stops after two");
    check(errorAction(3, false) == ErrorAction::StopSourceUnavailable, "the failure count is checked first");
}

void cAbi() {
    float r[4] = {};
    check(geode_playback_range(3, r) == 1 && r[0] == 0.5f && r[1] == 2.0f && r[2] == 0.05f && r[3] == 1.0f, "speed range");
    check(geode_playback_range(GEODE_PLAYBACK_CONTROLS, r) == 0 && geode_playback_range(-1, nullptr) == 0, "no such control");
    check(geode_playback_clamp(1, 60000.0f) == 12000.0f, "the C ABI's clamp is the engine's");
    check(geode_playback_crossfade_ms(5.2f) == 5000, "the C ABI's crossfade is the engine's");
    check(geode_playback_pitch_ratio(12.0f) == pitchRatio(12.0f), "the C ABI's pitch is the engine's");
    check(geode_playback_snap(1.37f, 0.05f) == snapToStep(1.37f, 0.05f), "the C ABI's snap is the engine's");
    check(geode_playback_error_action(1, 1) == 0 && geode_playback_error_action(1, 0) == 1 &&
              geode_playback_error_action(3, 1) == 2,
          "the C ABI's error action is the engine's");
}

}  // namespace

int main() {
    semitonesAreTwelfthsOfAnOctave();
    slidersLandOnTheirStep();
    rangesHoldTheSavedOptions();
    crossfadeSliderRoundsToWholeSeconds();
    aFailingTrackSkipsOrStops();
    cAbi();
    if (failures == 0) std::printf("playback_controls_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
