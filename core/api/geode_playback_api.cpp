#include "api/geode_api.h"
#include "audio/PlaybackMath.hpp"

extern "C" {

float geode_playback_semitones_to_ratio(float semitones) {
    return geode::audio::playback::semitonesToRatio(semitones);
}

long long geode_playback_sleep_fade_ms(void) { return geode::audio::playback::kSleepFadeMs; }

float geode_playback_sleep_fade_volume(long long remaining_ms, long long fade_ms) {
    return geode::audio::playback::sleepFadeVolume(remaining_ms, fade_ms);
}

float geode_playback_snap(float value, float step) { return geode::audio::playback::snap(value, step); }

}  // extern "C"
