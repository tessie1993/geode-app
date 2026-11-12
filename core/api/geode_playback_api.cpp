#include "api/geode_api.h"
#include "audio/dsp/PlaybackControls.hpp"

extern "C" {

float geode_playback_pitch_ratio(float semitones) { return geode::audio::pitchRatio(semitones); }

float geode_playback_snap(float value, float step) { return geode::audio::snapToStep(value, step); }

int geode_playback_range(int control, float* out4) {
    if (control < 0 || control >= geode::audio::kPlaybackControls) return 0;
    const geode::audio::ControlRange r = geode::audio::controlRange(control);
    if (out4) {
        out4[0] = r.min;
        out4[1] = r.max;
        out4[2] = r.step;
        out4[3] = r.fallback;
    }
    return 1;
}

float geode_playback_clamp(int control, float value) { return geode::audio::clampControl(control, value); }

int geode_playback_crossfade_ms(float seconds) { return geode::audio::crossfadeMsFromSeconds(seconds); }

}  // extern "C"
