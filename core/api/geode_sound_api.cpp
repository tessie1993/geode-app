#include "api/geode_api.h"
#include "sound/PlayerSignals.hpp"
#include "sound/Silence.hpp"

extern "C" {

long long geode_silence_heard_at(const float* const* channels, int channel_count, int frames, long long now_ms,
                                 long long heard_at_ms) {
    return geode::sound::silence::heardAt(channels, channel_count, frames, now_ms, heard_at_ms);
}

int geode_silence_resting(long long now_ms, long long heard_at_ms) {
    return geode::sound::silence::resting(now_ms, heard_at_ms) ? 1 : 0;
}

long long geode_sound_track_boundary(long long seed) {
    return static_cast<long long>(geode::sound::player::trackBoundary(static_cast<uint64_t>(seed)));
}

void geode_sound_set_fade_gain(float gain) { geode::sound::player::setFadeGain(gain); }

void geode_sound_set_player_source(int on) { geode::sound::player::setPlayerIsSource(on != 0); }

}  // extern "C"
