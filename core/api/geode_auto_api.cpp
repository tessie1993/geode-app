#include "api/geode_api.h"
#include "viz/AutoTimer.hpp"
#include "viz/TransitionChoice.hpp"

namespace auto_timer = geode::viz::auto_timer;

static_assert(auto_timer::kPlaylist == GEODE_AUTO_TIMER_PLAYLIST);
static_assert(auto_timer::kRandom == GEODE_AUTO_TIMER_RANDOM);

extern "C" {

void geode_auto_timer_restart(int timer, long long now_ms) { auto_timer::restart(timer, now_ms); }

int geode_auto_timer_due(int timer, long long now_ms, int interval_s, int running) {
    return auto_timer::due(timer, now_ms, static_cast<double>(interval_s), running != 0) ? 1 : 0;
}

double geode_auto_timer_hold_s(void) { return auto_timer::liveHoldSeconds(); }

void geode_auto_mark_change(const char* key) {
    if (key) geode::viz::transition_choice::markAuto(key);
}

}  // extern "C"
