#include <mutex>
#include <string>

#include "api/geode_api.h"
#include "viz/AutoDraw.hpp"
#include "viz/AutoPlan.hpp"
#include "viz/AutoTimer.hpp"
#include "viz/TransitionChoice.hpp"

namespace auto_timer = geode::viz::auto_timer;

static_assert(auto_timer::kPlaylist == GEODE_AUTO_TIMER_PLAYLIST);
static_assert(auto_timer::kRandom == GEODE_AUTO_TIMER_RANDOM);

namespace plan = geode::viz::auto_plan;

static_assert(plan::kMomentNone == GEODE_AUTO_MOMENT_NONE && plan::kMomentPlaylist == GEODE_AUTO_MOMENT_PLAYLIST &&
              plan::kMomentRandom == GEODE_AUTO_MOMENT_RANDOM);
static_assert(plan::kDrawStyles == GEODE_AUTO_DRAW_STYLES && plan::kDrawPresets == GEODE_AUTO_DRAW_PRESETS &&
              plan::kDrawKeepLook == GEODE_AUTO_DRAW_KEEP_LOOK);
static_assert(plan::kKindStyle == GEODE_AUTO_KIND_STYLE && plan::kKindPreset == GEODE_AUTO_KIND_PRESET);

namespace {

plan::Look lookOf(const char* style, const float* frame, int count, const char* palette_id) {
    plan::Look look;
    look.style = style ? style : "";
    if (frame && count > 0) look.frame.assign(frame, frame + count);
    look.paletteId = palette_id ? palette_id : "";
    return look;
}

template <typename Call>
auto withPlan(Call call) {
    auto& s = plan::detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return call(s.plan);
}

}  // namespace

extern "C" {

void geode_auto_timer_restart(int timer, long long now_ms) { auto_timer::restart(timer, now_ms); }

int geode_auto_timer_due(int timer, long long now_ms, int interval_s, int running) {
    return auto_timer::due(timer, now_ms, static_cast<double>(interval_s), running != 0) ? 1 : 0;
}

double geode_auto_timer_hold_s(void) { return auto_timer::liveHoldSeconds(); }

void geode_auto_mark_change(const char* key) {
    if (key) geode::viz::transition_choice::markAuto(key);
}

int geode_auto_pick(int count, int on_screen) { return geode::viz::auto_draw::pick(count, on_screen); }

static_assert(plan::kIntervalMinS == GEODE_AUTO_INTERVAL_MIN_S && plan::kIntervalMaxS == GEODE_AUTO_INTERVAL_MAX_S);
int geode_auto_interval_s(int requested_s) { return plan::clampInterval(requested_s); }

void geode_auto_editing_set(int on, long long now_ms) {
    withPlan([&](plan::Plan& p) { p.setEditing(on != 0, now_ms); });
}

void geode_auto_hand_pick(long long now_ms) {
    withPlan([&](plan::Plan& p) { p.handPick(now_ms); });
}

int geode_auto_playlist_tick(int count, int enabled, int playing, int interval_s, long long now_ms) {
    return withPlan([&](plan::Plan& p) { return p.playlistTick(count, enabled != 0, playing != 0, interval_s, now_ms); });
}

int geode_auto_playlist_step(int count, int step, long long now_ms) {
    return withPlan([&](plan::Plan& p) { return p.playlistStep(count, step, now_ms); });
}

int geode_auto_random_tick(int enabled, int playing, int interval_s, long long now_ms) {
    return withPlan([&](plan::Plan& p) { return p.randomTick(enabled != 0, playing != 0, interval_s, now_ms) ? 1 : 0; });
}

int geode_auto_moment(const char* style, const float* frame, int count, const char* palette_id, int playing,
                      int playlist_enabled) {
    plan::Look look = lookOf(style, frame, count, palette_id);
    return withPlan([&](plan::Plan& p) { return p.moment(std::move(look), playing != 0, playlist_enabled != 0); });
}

void geode_auto_left_as(const char* style, const float* frame, int count, const char* palette_id) {
    plan::Look look = lookOf(style, frame, count, palette_id);
    withPlan([&](plan::Plan& p) { p.leftAs(std::move(look)); });
}

}  // extern "C"
