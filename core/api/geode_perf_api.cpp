#include <algorithm>

#include "api/geode_api.h"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Quality.hpp"

namespace frame = geode::viz::frame;

static_assert(frame::kScaleChoices.size() == GEODE_PERF_SCALE_CHOICES);
static_assert(frame::kMaxResolutionChoices == GEODE_PERF_RESOLUTION_CHOICES);
static_assert(frame::kNativeResolution == GEODE_PERF_NATIVE);

extern "C" {

int geode_perf_tier(int mode, float* out, int capacity) {
    if (!out || capacity < GEODE_PERF_TIER_FLOATS || mode < 0 || mode >= frame::kQualityModes) return 0;
    const frame::QualityTier& t = frame::qualityTier(static_cast<frame::QualityMode>(mode));
    out[0] = static_cast<float>(t.resolution);
    out[1] = t.fpsCap;
    out[2] = t.marchScale;
    out[3] = t.postFx ? 1.0f : 0.0f;
    return GEODE_PERF_TIER_FLOATS;
}

int geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class, int panel_width,
                               int panel_height) {
    return static_cast<int>(
        frame::modeForDevice(total_ram_bytes, low_ram != 0, media_performance_class, panel_width, panel_height));
}

int geode_perf_mode_of(int resolution, int fps_cap, float march_scale, int post_fx, int panel_width, int panel_height) {
    return frame::modeOf(resolution, fps_cap, march_scale, post_fx != 0, frame::shortSide(panel_width, panel_height));
}

int geode_perf_scale_choices(float* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int n = std::min(capacity, static_cast<int>(frame::kScaleChoices.size()));
    std::copy_n(frame::kScaleChoices.begin(), n, out);
    return n;
}

int geode_perf_resolution_choices(int panel_width, int panel_height, int* out, int capacity) {
    return frame::resolutionChoices(panel_width, panel_height, out, capacity);
}

int geode_perf_resolution_for_scale(float render_scale, int panel_width, int panel_height) {
    return frame::resolutionForScale(render_scale, panel_width, panel_height);
}

int geode_perf_buffer_size(int resolution, int mode_width, int mode_height, int view_width, int view_height, int* out,
                           int capacity) {
    if (!out || capacity < GEODE_PERF_BUFFER_INTS) return 0;
    const frame::BufferSize b = frame::bufferSize(resolution, mode_width, mode_height, view_width, view_height);
    if (b.width <= 0) return 0;
    out[0] = b.width;
    out[1] = b.height;
    return GEODE_PERF_BUFFER_INTS;
}

}  // extern "C"
