#include <atomic>
#include <memory>

#include "api/geode_api.h"
#include "sound/PlayerSignals.hpp"
#include "sound/interp/PlayoutClock.hpp"
#include "sound/interp/SpeakerSync.hpp"

struct geode_playout {
    geode::sound::PlayoutClock clock;
    std::atomic<bool> bypass{false};
};

extern "C" {

geode_playout* geode_playout_create(void) {
    return new geode_playout{};
}

void geode_playout_destroy(geode_playout* p) {
    std::unique_ptr<geode_playout> owned(p);
}

void geode_playout_buffer_handled(geode_playout* p, int64_t presentation_us, int64_t ring_frame, int64_t frames) {
    if (p) p->clock.bufferHandled(presentation_us, ring_frame, frames);
}

void geode_playout_position(geode_playout* p, int64_t sink_us, int64_t now_ns) {
    if (p) p->clock.position(sink_us, now_ns);
}

void geode_playout_playing(geode_playout* p, int playing, int64_t now_ns) {
    if (p) p->clock.playing(playing != 0, now_ns);
}

void geode_playout_flush(geode_playout* p) {
    if (p) p->clock.flush();
}

void geode_playout_speed(geode_playout* p, float speed) {
    if (p) p->clock.speed(speed);
}

void geode_playout_set_offset_ms(geode_playout* p, int ms) {
    if (p) p->clock.setOffsetMs(ms);
}

int geode_playout_max_offset_ms(void) {
    return geode::sound::PlayoutClock::kMaxOffsetMs;
}

int64_t geode_playout_speaker_frame(const geode_playout* p, int64_t now_ns) {
    return p ? p->clock.speakerFrame(now_ns) : geode::sound::PlayoutClock::kUnknown;
}

void geode_playout_set_bypass(geode_playout* p, int on) {
    if (p) p->bypass.store(on != 0, std::memory_order_relaxed);
}

int64_t geode_playout_listen_limit(const geode_playout* p, int64_t now_ns) {
    return geode::sound::interp::listenLimit(p ? &p->clock : nullptr, geode::sound::player::playerIsSource(),
                                             p && p->bypass.load(std::memory_order_relaxed), now_ns);
}

int geode_playout_lag_ms(const geode_playout* p, int64_t now_ns) {
    return p ? p->clock.lagMs(now_ns) : 0;
}

int geode_playout_ring_frames(void) {
    return geode::sound::interp::kPlayerRingFrames;
}

int geode_playout_ring_runway(void) {
    return geode::sound::interp::kPlayerRingRunway;
}

}  // extern "C"
