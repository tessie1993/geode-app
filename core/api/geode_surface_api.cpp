#include "api/geode_api.h"
#include "viz/StageRun.hpp"
#include "viz/SurfaceRun.hpp"

namespace surface = geode::viz::surface;

// The header's numbers are the rule's; a drift here would make the shell pass one bit and mean another.
static_assert(surface::kReducedMotion == GEODE_SURFACE_REDUCED_MOTION);
static_assert(surface::kDefaultFps == GEODE_SURFACE_DEFAULT_FPS);
static_assert(geode::viz::stage::kCasting == GEODE_STAGE_CASTING);
static_assert(geode::viz::stage::kCastLeadsAuto == GEODE_STAGE_CAST_LEADS_AUTO);

extern "C" {

int geode_surface_run_mode(unsigned flags, long long animate_until_ms, long long now_ms) {
    return static_cast<int>(surface::mode(flags, animate_until_ms, now_ms));
}

int geode_surface_listening(unsigned flags, long long animate_until_ms, long long now_ms) {
    return surface::listening(flags, animate_until_ms, now_ms) ? 1 : 0;
}

int geode_surface_entitled(long long animate_until_ms, long long now_ms) {
    return surface::entitled(animate_until_ms, now_ms) ? 1 : 0;
}

long long geode_surface_recheck_in_ms(long long animate_until_ms, long long now_ms) {
    return surface::recheckInMs(animate_until_ms, now_ms);
}

long long geode_surface_settle_ms(int previous_mode, int mode) {
    return surface::settleMs(static_cast<surface::RunMode>(previous_mode), static_cast<surface::RunMode>(mode));
}

int geode_surface_fps_choice(int requested_fps) { return surface::fpsChoice(requested_fps); }

int geode_surface_fps_choices(int* out, int capacity) {
    const int n = static_cast<int>(surface::kFpsChoices.size());
    if (out != nullptr) {
        for (int i = 0; i < n && i < capacity; ++i) out[i] = surface::kFpsChoices[static_cast<size_t>(i)];
    }
    return n;
}

unsigned geode_stage_run(unsigned flags) { return geode::viz::stage::run(flags); }

}  // extern "C"
