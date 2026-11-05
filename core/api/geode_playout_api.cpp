#include "api/geode_api.h"
#include "audio/PlayoutClock.hpp"

using geode::audio::PlayoutClock;

namespace {
PlayoutClock* of(geode_playout* p) { return reinterpret_cast<PlayoutClock*>(p); }
}  // namespace

extern "C" {

geode_playout* geode_playout_create(void) { return reinterpret_cast<geode_playout*>(new PlayoutClock()); }

void geode_playout_destroy(geode_playout* p) { delete of(p); }

void geode_playout_reset(geode_playout* p) { of(p)->reset(); }

void geode_playout_begin_epoch(geode_playout* p, int epoch, int rate_hz) { of(p)->beginEpoch(epoch, rate_hz); }

void geode_playout_buffer(geode_playout* p, long long media_us) { of(p)->bufferHandled(media_us); }

void geode_playout_flush(geode_playout* p) { of(p)->flush(); }

void geode_playout_position(geode_playout* p, long long media_us, long long now_ns) {
    of(p)->position(media_us, now_ns);
}

void geode_playout_playing(geode_playout* p, int playing, long long now_ns) { of(p)->playing(playing != 0, now_ns); }

void geode_playout_speed(geode_playout* p, float speed) { of(p)->speed(speed); }

long long geode_playout_heard_frame(const geode_playout* p, int epoch, long long written, long long now_ns) {
    return reinterpret_cast<const PlayoutClock*>(p)->heardFrame(epoch, written, now_ns);
}

}  // extern "C"
