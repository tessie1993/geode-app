#include <algorithm>

#include "api/geode_api.h"
#include "viz/frame/Quality.hpp"
#include "viz/liquid/LiquidMath.hpp"

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
    out[4] = t.adaptive ? 1.0f : 0.0f;
    return GEODE_PERF_TIER_FLOATS;
}

int geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class) {
    return static_cast<int>(frame::modeForDevice(total_ram_bytes, low_ram != 0, media_performance_class));
}

int geode_perf_mode_of(float render_scale, int fps_cap, float march_scale, int post_fx, int adaptive) {
    return frame::modeOf(render_scale, fps_cap, march_scale, post_fx != 0, adaptive != 0);
}

int geode_perf_scale_choices(float* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int n = std::min(capacity, static_cast<int>(frame::kScaleChoices.size()));
    std::copy_n(frame::kScaleChoices.begin(), n, out);
    return n;
}

int geode_perf_liquid_hot(int stored) { return static_cast<int>(geode::viz::liquid::hotPolicyFrom(stored)); }

}  // extern "C"
