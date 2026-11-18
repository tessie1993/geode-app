// The cast's watchdog and fallback rules (core/cast/CastWatch): the C ABI. Pure; host-tested (cast_watch_test).
#include <cstdint>

#include "api/geode_api.h"
#include "cast/CastWatch.hpp"

namespace watch = geode::cast::watch;

static_assert(GEODE_CAST_WATCH_WAIT == watch::kWait && GEODE_CAST_WATCH_READY == watch::kReady &&
              GEODE_CAST_WATCH_START_TIMEOUT == watch::kStartTimedOut && GEODE_CAST_WATCH_PLAYING == watch::kPlaying &&
              GEODE_CAST_WATCH_FROZEN == watch::kFrozen && GEODE_CAST_WATCH_NETWORK_GONE == watch::kNetworkGone);

extern "C" {

int geode_cast_watch(int announced, int64_t now_ms, int64_t waiting_since_ms, int playlist_ready,
                     int64_t segment_age_ms, int address_here) {
    return watch::verdict(announced != 0, now_ms, waiting_since_ms, playlist_ready != 0, segment_age_ms,
                          address_here != 0);
}

int64_t geode_cast_watch_next_ms(int announced) { return watch::nextPollMs(announced != 0); }

int geode_cast_fallback_release(int64_t now_ns, int64_t drew_ns) {
    return watch::releaseFallback(now_ns, drew_ns) ? 1 : 0;
}

}  // extern "C"
