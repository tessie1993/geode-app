#include <algorithm>
#include <mutex>

#include "api/geode_api.h"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Quality.hpp"
#include "viz/frame/ResolutionState.hpp"

namespace frame = geode::viz::frame;

static_assert(frame::kScaleChoices.size() == GEODE_PERF_SCALE_CHOICES);
static_assert(frame::kNativeResolution == GEODE_PERF_NATIVE);
static_assert(frame::kDescriptorInts == GEODE_PERF_DESCRIPTOR_INTS);
static_assert(frame::kFieldSteps == GEODE_PERF_FIELD_STEPS);

namespace {

struct Shared {
    std::mutex lock;
    frame::ResolutionState state;
};

Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace

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

int geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    const frame::Panel& panel = s.state.panel();
    return static_cast<int>(
        frame::modeForDevice(total_ram_bytes, low_ram != 0, media_performance_class, panel.width, panel.height));
}

int geode_perf_scale_choices(float* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int n = std::min(capacity, static_cast<int>(frame::kScaleChoices.size()));
    std::copy_n(frame::kScaleChoices.begin(), n, out);
    return n;
}

int geode_perf_resolution_for_scale(float render_scale) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    const frame::Panel& panel = s.state.panel();
    return frame::resolutionForScale(render_scale, panel.width, panel.height);
}

void geode_perf_panel_set(int panel_width, int panel_height, int mode_width, int mode_height) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.setPanel({panel_width, panel_height, mode_width, mode_height});
}

void geode_perf_view_set(int view_width, int view_height) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.setView({view_width, view_height});
}

void geode_perf_settings_set(int resolution, int fps_cap, float march_scale, int post_fx) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.setSettings({resolution, fps_cap, march_scale, post_fx != 0});
}

int geode_perf_resolution_read(int* out, int capacity) {
    if (!out || capacity < GEODE_PERF_DESCRIPTOR_INTS) return 0;
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.lock);
    const frame::Descriptor& d = s.state.descriptor();
    std::copy(d.begin(), d.end(), out);
    return GEODE_PERF_DESCRIPTOR_INTS;
}

}  // extern "C"
