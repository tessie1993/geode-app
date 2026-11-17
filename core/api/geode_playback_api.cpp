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

int geode_playback_error_action(int consecutive_failures, int has_next) {
    return static_cast<int>(geode::audio::errorAction(consecutive_failures, has_next != 0));
}

void geode_ab_loop_cycle(int64_t start_ms, int64_t end_ms, int64_t at_ms, int64_t* out2) {
    if (!out2) return;
    const geode::audio::AbLoop next = geode::audio::abLoopCycle(geode::audio::AbLoop{start_ms, end_ms}, at_ms);
    out2[0] = next.startMs;
    out2[1] = next.endMs;
}

int64_t geode_ab_loop_seek(int64_t start_ms, int64_t end_ms, int64_t position_ms) {
    return geode::audio::abLoopSeek(geode::audio::AbLoop{start_ms, end_ms}, position_ms);
}

}  // extern "C"
