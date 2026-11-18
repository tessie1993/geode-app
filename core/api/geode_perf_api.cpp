#include <algorithm>

#include "api/geode_api.h"
#include "viz/frame/FrameGovernor.hpp"
#include "viz/frame/Quality.hpp"

namespace frame = geode::viz::frame;

static_assert(frame::kScaleChoices.size() == GEODE_PERF_SCALE_CHOICES);

extern "C" {

int geode_perf_tier(int mode, float* out, int capacity) {
    if (!out || capacity < GEODE_PERF_TIER_FLOATS || mode < 0 || mode >= frame::kQualityModes) return 0;
    const frame::QualityTier& t = frame::qualityTier(static_cast<frame::QualityMode>(mode));
    out[0] = t.renderScale;
    out[1] = t.fpsCap;
    out[2] = t.marchScale;
    out[3] = t.postFx ? 1.0f : 0.0f;
    return GEODE_PERF_TIER_FLOATS;
}

int geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class) {
    return static_cast<int>(frame::modeForDevice(total_ram_bytes, low_ram != 0, media_performance_class));
}

int geode_perf_mode_of(float render_scale, int fps_cap, float march_scale, int post_fx) {
    return frame::modeOf(render_scale, fps_cap, march_scale, post_fx != 0);
}

int geode_perf_scale_choices(float* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int n = std::min(capacity, static_cast<int>(frame::kScaleChoices.size()));
    std::copy_n(frame::kScaleChoices.begin(), n, out);
    return n;
}

int geode_perf_adapt_now(float* out, int capacity) {
    if (!out || capacity < GEODE_PERF_ADAPT_NOW_FLOATS) return 0;
    const frame::AdaptNow& now = frame::adaptNow();
    out[0] = now.fps.load(std::memory_order_relaxed);
    out[1] = now.renderScale.load(std::memory_order_relaxed);
    out[2] = now.marchScale.load(std::memory_order_relaxed);
    out[3] = now.postFx.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    out[4] = static_cast<float>(now.steps.load(std::memory_order_relaxed));
    return GEODE_PERF_ADAPT_NOW_FLOATS;
}

}  // extern "C"
