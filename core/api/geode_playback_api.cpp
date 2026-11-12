#include "api/geode_api.h"
#include "audio/dsp/PlaybackControls.hpp"

extern "C" {

float geode_playback_pitch_ratio(float semitones) { return geode::audio::pitchRatio(semitones); }

float geode_playback_snap(float value, float step) { return geode::audio::snapToStep(value, step); }

int geode_playback_error_action(int consecutive_failures, int has_next) {
    return static_cast<int>(geode::audio::errorAction(consecutive_failures, has_next != 0));
}

}  // extern "C"
