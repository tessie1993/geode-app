// The C ABI over the estimate of a render's time left (geode_api.h, "A render's time left"). Only argument
// checks, the handle's lock and copies live here; the rules are output/RenderEta, host-tested in core/tests.
#include <memory>
#include <mutex>

#include "api/geode_api.h"
#include "output/RenderEta.hpp"

namespace out = geode::output;

// The render's thread samples while the main thread resets at a run's start and end, so the handle locks.
struct geode_render_eta {
    std::mutex lock;
    out::RenderEta eta;
};

namespace {
static_assert(GEODE_RENDER_ETA_UNKNOWN == out::RenderEta::kUnknown);
static_assert(GEODE_ETA_ALMOST_DONE == out::eta::kAlmostDone && GEODE_ETA_ABOUT_A_MINUTE == out::eta::kAboutAMinute);
static_assert(GEODE_ETA_MINUTES == out::eta::kMinutes && GEODE_ETA_HOURS == out::eta::kHours);
}  // namespace

extern "C" {

geode_render_eta* geode_render_eta_create(void) {
    try {
        return std::make_unique<geode_render_eta>().release();
    } catch (...) {
        return nullptr;
    }
}

void geode_render_eta_destroy(geode_render_eta* e) {
    std::unique_ptr<geode_render_eta> owned(e);
}

int64_t geode_render_eta_sample(geode_render_eta* e, float progress, int64_t at_ms, int steady) {
    if (!e) return GEODE_RENDER_ETA_UNKNOWN;
    try {
        std::lock_guard<std::mutex> hold(e->lock);
        return e->eta.sample(progress, at_ms, steady != 0);
    } catch (...) {
        return GEODE_RENDER_ETA_UNKNOWN;
    }
}

void geode_render_eta_reset(geode_render_eta* e) {
    if (!e) return;
    try {
        std::lock_guard<std::mutex> hold(e->lock);
        e->eta.reset();
    } catch (...) {
    }
}

int geode_render_eta_phrase(int64_t seconds_remaining) { return out::eta::wordingFor(seconds_remaining).phrase; }

int geode_render_eta_phrase_count(int64_t seconds_remaining) { return out::eta::wordingFor(seconds_remaining).count; }

}  // extern "C"
