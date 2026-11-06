#include "api/geode_api.h"
#include "viz/SurfaceRun.hpp"

namespace surface = geode::viz::surface;

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

}  // extern "C"
