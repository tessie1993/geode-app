// Host test: every reader that asks "is the beat heard?" of beatConf uses the beat tracker's own lock lines
// (GEODE_SOUND_BEAT_LOCK_ON / _OFF in core/sound/SoundFrame.h), not a number of its own.
// Built with the runner's own compiler in CI (core/tests/host-tests.txt: beat_lock_test).
#include <cmath>
#include <cstdio>
#include <string>

#include "sound/SoundFrame.h"
#include "viz/input/TapAlong.hpp"
#include "viz/passes/EndlessZoom.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// Four taps on the beat of a song at 93 BPM (no tempo is meant), the frame's confidence at `conf`: true when the
// fourth completes a streak, which tap along allows only while it hears the beat.
bool streakAt(float conf) {
    geode::viz::TapAlong tapAlong;
    const double period = 60.0 / 93.0;
    bool done = false;
    for (int beat = 1; beat <= 4; ++beat) {
        const double t = 10.0 + beat * period;
        tapAlong.frame(t - 0.01, static_cast<float>(1.0 - 0.01 / period), 93.0f, conf, 1.0f);
        done = tapAlong.tap(t);
    }
    return done;
}

}  // namespace

int main() {
    static_assert(GEODE_SOUND_BEAT_LOCK_OFF < GEODE_SOUND_BEAT_LOCK_ON, "the tracker lets go below where it locks");
    static_assert(geode::viz::TapAlong::kMinConf == GEODE_SOUND_BEAT_LOCK_ON, "tap along hears the beat from the lock line");
    static_assert(geode::viz::endless::Follow::kLocked == GEODE_SOUND_BEAT_LOCK_ON, "the dive follows the beat from the lock line");
    check(streakAt(GEODE_SOUND_BEAT_LOCK_ON), "tap along takes a streak at the lock line");
    check(!streakAt(std::nextafter(GEODE_SOUND_BEAT_LOCK_ON, 0.0f)), "and none just below it");
    if (failures != 0) {
        std::printf("%d FAILED\n", failures);
        return 1;
    }
    std::printf("beat_lock_test OK\n");
    return 0;
}
