#include "api/geode_api.h"
#include "output/TakeSession.hpp"

#include <utility>

using geode::output::TakeSession;

extern "C" {

int geode_take_begin(int fd, float display_fps) { return TakeSession::shared().begin(fd, display_fps) ? 1 : 0; }

int geode_take_begin_limited(int fd, float display_fps, int64_t cap_ms, const uint32_t* tile_argb, int tile_w, int tile_h,
                             int tile_short_side) {
    return geode_take_begin_from(fd, display_fps, cap_ms, tile_argb, tile_w, tile_h, tile_short_side, 0);
}

int geode_take_begin_from(int fd, float display_fps, int64_t cap_ms, const uint32_t* tile_argb, int tile_w, int tile_h,
                          int tile_short_side, int source) {
    geode::output::TakeLimits limits;
    limits.source = source == 1 ? geode::output::TakeSource::Input : geode::output::TakeSource::Player;
    limits.capMs = cap_ms > 0 ? cap_ms : 0;
    if (tile_argb && tile_w > 0 && tile_h > 0 && tile_short_side > 0) {
        limits.tileArgb.assign(tile_argb, tile_argb + static_cast<size_t>(tile_w) * static_cast<size_t>(tile_h));
        limits.tileWidth = tile_w;
        limits.tileHeight = tile_h;
        limits.tileForShortSide = tile_short_side;
    }
    return TakeSession::shared().begin(fd, display_fps, std::move(limits)) ? 1 : 0;
}

int geode_take_reached_cap(void) { return TakeSession::shared().reachedCap() ? 1 : 0; }

void geode_take_set_rule(int rule) { TakeSession::shared().setRule(geode::output::takeRuleOf(rule)); }

int geode_take_event(int event) {
    if (event < 0 || event > static_cast<int>(geode::output::TakeEvent::TrackChange)) return 0;
    return static_cast<int>(TakeSession::shared().event(static_cast<geode::output::TakeEvent>(event)));
}

void geode_take_write_input(const float* interleaved, int frames, int channels, int sample_rate) {
    TakeSession::shared().writeInput(interleaved, frames, channels, sample_rate);
}

int64_t geode_take_end(void) { return TakeSession::shared().end(); }

int geode_take_active(void) { return TakeSession::shared().active() ? 1 : 0; }

void geode_take_write_audio(const int16_t* interleaved, int frames, int channels, int sample_rate) {
    TakeSession::shared().writeAudio(interleaved, frames, channels, sample_rate);
}

}
