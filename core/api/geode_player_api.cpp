#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "api/geode_api.h"
#include "audio/player/Player.hpp"

struct geode_player {
    geode::audio::player::Player player;
};

namespace {
using geode::audio::player::PlayerState;
static_assert(static_cast<int>(PlayerState::Idle) == GEODE_PLAYER_IDLE);
static_assert(static_cast<int>(PlayerState::Buffering) == GEODE_PLAYER_BUFFERING);
static_assert(static_cast<int>(PlayerState::Ready) == GEODE_PLAYER_READY);
static_assert(static_cast<int>(PlayerState::Ended) == GEODE_PLAYER_ENDED);
static_assert(static_cast<int>(PlayerState::Error) == GEODE_PLAYER_ERROR);
}  // namespace

extern "C" {

geode_player* geode_player_create(void) { return std::make_unique<geode_player>().release(); }

void geode_player_destroy(geode_player* p) { std::unique_ptr<geode_player> owned(p); }

void geode_player_open(geode_player* p, int fd, int64_t offset, int64_t length, int64_t token) {
    if (p) p->player.open(fd, offset, length, token);
}

void geode_player_set_next(geode_player* p, int fd, int64_t offset, int64_t length, int64_t token) {
    if (p) p->player.setNext(fd, offset, length, token);
}

void geode_player_play(geode_player* p) {
    if (p) p->player.play();
}

void geode_player_pause(geode_player* p) {
    if (p) p->player.pause();
}

void geode_player_stop(geode_player* p) {
    if (p) p->player.stop();
}

void geode_player_seek(geode_player* p, int64_t position_us) {
    if (p) p->player.seek(position_us);
}

void geode_player_set_crossfade(geode_player* p, int duration_ms, int curve) {
    if (p) p->player.setCrossfade(duration_ms, curve);
}

void geode_player_set_dsp(geode_player* p, geode_dsp* dsp) {
    if (p) p->player.setDsp(dsp);
}

void geode_player_set_volume(geode_player* p, float volume) {
    if (p) p->player.setVolume(std::clamp(volume, 0.0f, 1.0f));
}

int geode_player_state(geode_player* p) {
    return p ? static_cast<int>(p->player.state()) : GEODE_PLAYER_IDLE;
}

int geode_player_play_when_ready(geode_player* p) { return p && p->player.playWhenReady() ? 1 : 0; }

int64_t geode_player_position_us(geode_player* p) { return p ? p->player.positionUs() : 0; }

int64_t geode_player_duration_us(geode_player* p) { return p ? p->player.durationUs() : 0; }

int64_t geode_player_current_token(geode_player* p) { return p ? p->player.currentToken() : -1; }

int geode_player_output_sample_rate(geode_player* p) { return p ? p->player.outputSampleRate() : 0; }

size_t geode_player_last_error(geode_player* p, char* out, size_t capacity) {
    const std::string error = p ? p->player.lastError() : std::string();
    if (out && capacity > 0) {
        const size_t n = std::min(error.size(), capacity - 1);
        std::memcpy(out, error.data(), n);
        out[n] = '\0';
    }
    return error.size();
}

size_t geode_player_read_tap(geode_player* p, float* stereo, size_t frames) {
    return p && stereo ? p->player.readTap(stereo, frames) : 0;
}

}  // extern "C"
