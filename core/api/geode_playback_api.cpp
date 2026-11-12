#include "api/geode_api.h"
#include "audio/dsp/PlaybackControls.hpp"

extern "C" {

float geode_playback_pitch_ratio(float semitones) { return geode::audio::pitchRatio(semitones); }

float geode_playback_snap(float value, float step) { return geode::audio::snapToStep(value, step); }

}  // extern "C"
