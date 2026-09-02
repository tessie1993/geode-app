#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Renderer.hpp"

struct geode_viz {
    geode_viz(AAssetManager* assets, std::string cacheDir) : renderer(assets, std::move(cacheDir)) {}

    geode::viz::Renderer renderer;
    std::mutex configLock;
    std::array<geode::viz::LfoConfig, geode::viz::LfoEngine::kSlots> lfo{};
    std::array<geode::viz::AdsrConfig, geode::viz::AdsrEngine::kCount> adsr{};
};

namespace {

using namespace geode::viz;

static_assert(LfoEngine::kSlots == GEODE_LFO_SLOTS);
static_assert(AdsrEngine::kCount == GEODE_ADSR_SLOTS);

constexpr int kModSourceCount = static_cast<int>(ModSource::StereoPan) + 1;
constexpr int kLfoTargetCount = static_cast<int>(LfoTarget::Lfo3Depth) + 1;
constexpr int kLfoWaveCount = static_cast<int>(LfoWave::Random) + 1;
constexpr int kModPolarityCount = static_cast<int>(ModPolarity::Negative) + 1;
constexpr int kModCurveCount = static_cast<int>(ModCurve::Smooth) + 1;
constexpr int kEnvBandCount = static_cast<int>(EnvBand::Width) + 1;

template <typename E>
E enumAt(float value, int count) {
    return static_cast<E>(std::clamp(static_cast<int>(std::lround(value)), 0, count - 1));
}

bool flag(float value) { return value > 0.5f; }

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

extern "C" {

geode_viz* geode_viz_create(AAssetManager* assets, const char* cache_dir) {
    if (!assets || !cache_dir) return nullptr;
    return std::make_unique<geode_viz>(assets, std::string(cache_dir)).release();
}

void geode_viz_destroy(geode_viz* v) {
    std::unique_ptr<geode_viz> owned(v);
}

int geode_viz_param_count(void) {
    return SceneParams::kFieldCount;
}

const char* geode_viz_param_name(int index) {
    if (index < 0 || index >= SceneParams::kFieldCount) return nullptr;
    return SceneParams::fieldNames()[static_cast<size_t>(index)];
}

void geode_viz_set_params(geode_viz* v, const float* values, int count) {
    if (!v || !values || count != SceneParams::kFieldCount) return;
    const auto& names = SceneParams::fieldNames();
    SceneParams p;
    for (int i = 0; i < count; ++i) p.set(names[static_cast<size_t>(i)], values[i]);
    v->renderer.setParams(p);
}

int geode_viz_set_param(geode_viz* v, const char* name, float value) {
    return (v && name && v->renderer.setParam(name, value)) ? 1 : 0;
}

void geode_viz_set_features(geode_viz* v, const GeodeFeatureFrame* frame) {
    if (v && frame) v->renderer.setFeatures(*frame);
}

void geode_viz_set_reduced_motion(geode_viz* v, int on) {
    if (v) v->renderer.setReducedMotion(on != 0);
}

void geode_viz_set_layer(geode_viz* v, const char* scene_id, float mix, int blend_mode) {
    if (v) v->renderer.setLayer(text(scene_id), mix, blend_mode);
}

void geode_viz_set_transition(geode_viz* v, const char* id, int64_t duration_ms) {
    if (v) v->renderer.setTransition(text(id), duration_ms);
}

void geode_viz_begin_param_morph(geode_viz* v, float seconds) {
    if (v) v->renderer.beginParamMorph(seconds);
}

void geode_viz_set_touch(geode_viz* v, const float* xy_ndc, int points) {
    if (!v) return;
    v->renderer.submitTouchPoints(xy_ndc ? xy_ndc : nullptr, xy_ndc ? std::max(points, 0) : 0);
}

void geode_viz_push_pcm(geode_viz* v, const float* mono, int count) {
    if (v && mono && count > 0) v->renderer.pushPcm(mono, count);
}

void geode_viz_set_custom_shader(geode_viz* v, const char* scene_id, const char* fragment_source) {
    if (v && scene_id && fragment_source) v->renderer.setCustomShader(scene_id, fragment_source);
}

size_t geode_viz_custom_shader(geode_viz* v, const char* scene_id, char* out, size_t capacity) {
    if (!v || !scene_id) return 0;
    const std::string source = v->renderer.customShaderFor(scene_id);
    if (out && capacity > 0) {
        const size_t n = std::min(capacity - 1, source.size());
        std::memcpy(out, source.data(), n);
        out[n] = '\0';
    }
    return source.size();
}

void geode_viz_set_lfo(geode_viz* v, int slot, const float* c, int count) {
    if (!v || !c || slot < 0 || slot >= GEODE_LFO_SLOTS || count < GEODE_LFO_CONFIG_FLOATS) return;
    LfoConfig cfg;
    cfg.enabled = flag(c[0]);
    cfg.source = enumAt<ModSource>(c[1], kModSourceCount);
    cfg.target = enumAt<LfoTarget>(c[2], kLfoTargetCount);
    cfg.wave = enumAt<LfoWave>(c[3], kLfoWaveCount);
    cfg.rateSeconds = c[4];
    cfg.depth = c[5];
    cfg.polarity = enumAt<ModPolarity>(c[6], kModPolarityCount);
    cfg.curve = enumAt<ModCurve>(c[7], kModCurveCount);
    std::lock_guard<std::mutex> lock(v->configLock);
    v->lfo[static_cast<size_t>(slot)] = cfg;
    v->renderer.setLfoConfigs(v->lfo);
}

void geode_viz_set_adsr(geode_viz* v, int slot, const float* c, int count) {
    if (!v || !c || slot < 0 || slot >= GEODE_ADSR_SLOTS || count < GEODE_ADSR_CONFIG_FLOATS) return;
    AdsrConfig cfg;
    cfg.enabled = flag(c[0]);
    cfg.attack = c[1];
    cfg.decay = c[2];
    cfg.sustain = c[3];
    cfg.release = c[4];
    cfg.amount = c[5];
    cfg.band = enumAt<EnvBand>(c[6], kEnvBandCount);
    cfg.gateThreshold = c[7];
    cfg.sustainTrack = flag(c[8]);
    cfg.retrigger = flag(c[9]);
    for (int i = GEODE_ADSR_CONFIG_FLOATS; i < count; ++i) {
        cfg.targets.push_back(enumAt<LfoTarget>(c[i], kLfoTargetCount));
    }
    std::lock_guard<std::mutex> lock(v->configLock);
    v->adsr[static_cast<size_t>(slot)] = std::move(cfg);
    v->renderer.setAdsrConfigs(v->adsr);
}

void geode_viz_set_thermal(geode_viz* v, int platform_status, float headroom) {
    if (!v) return;
    if (platform_status >= 0) v->renderer.thermal().setPlatformStatus(platform_status);
    v->renderer.thermal().setThermalHeadroom(headroom);
}

void geode_viz_set_paced_fps(geode_viz* v, float fps) {
    if (v) v->renderer.thermal().setPacedFps(fps);
}

void geode_viz_set_offscreen(geode_viz* v, int on) {
    if (!v) return;
    if (on) {
        v->renderer.thermal().beginOffscreenRender();
    } else {
        v->renderer.thermal().endOffscreenRender();
    }
}

int geode_viz_knows(geode_viz* v, const char* scene_id) {
    return (v && scene_id && v->renderer.knows(scene_id)) ? 1 : 0;
}

size_t geode_viz_scene_ids(geode_viz* v, char* out, size_t capacity) {
    if (!v) return 0;
    std::string joined;
    for (const auto& id : v->renderer.availableSceneIds()) {
        if (!joined.empty()) joined += '\n';
        joined += id;
    }
    if (out && capacity > 0) {
        const size_t n = std::min(capacity - 1, joined.size());
        std::memcpy(out, joined.data(), n);
        out[n] = '\0';
    }
    return joined.size();
}

const char* geode_viz_last_error(geode_viz* v) {
    return v ? v->renderer.lastError().c_str() : "";
}

void geode_viz_surface_created(geode_viz* v) {
    if (v) v->renderer.onSurfaceCreated();
}

void geode_viz_surface_changed(geode_viz* v, int width, int height) {
    if (v) v->renderer.surfaceChanged(std::max(width, 1), std::max(height, 1));
}

int geode_viz_set_scene(geode_viz* v, const char* scene_id) {
    return (v && scene_id && v->renderer.setScene(scene_id)) ? 1 : 0;
}

void geode_viz_warm_transition(geode_viz* v, const char* id) {
    if (v && id) v->renderer.warmTransition(id);
}

void geode_viz_cut(geode_viz* v) {
    if (v) v->renderer.cut();
}

void geode_viz_render(geode_viz* v, double time_seconds, uint32_t target_fbo) {
    if (v) v->renderer.render(time_seconds, target_fbo);
}

void geode_viz_release_scenes(geode_viz* v) {
    if (v) v->renderer.releaseScenes();
}

}
