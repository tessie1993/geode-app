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
    aFailingTrackSkipsOrStops();
    cAbi();
    if (failures == 0) std::printf("playback_controls_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
