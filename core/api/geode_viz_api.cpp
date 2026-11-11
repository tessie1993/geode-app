#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <EGL/egl.h>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/ingest/LayerMix.hpp"
#include "viz/Director.hpp"
#include "viz/GyroLook.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/Macros.hpp"
#include "viz/LayerState.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/ReactionResolve.hpp"
#include "viz/Renderer.hpp"
#include "viz/SceneRegistry.hpp"
#include "viz/SceneResidency.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/TransitionLibrary.hpp"
#include "viz/frame/Cadence.hpp"
#include "viz/frame/Pacer.hpp"
#include "viz/frame/PushVersions.hpp"
#include "viz/frame/Thermal.hpp"
#include "viz/frame/Quality.hpp"
#include "viz/ShaderOverrides.hpp"
#include "viz/frame/WallpaperWindow.hpp"

struct geode_viz {
    geode_viz(AAssetManager* assets, std::string cacheDir) : renderer(assets, std::move(cacheDir)) {
        // An export's style schedule switches the scene without the shell: the routes follow it in the same
        // frame, on the render thread, before the frame's params resolve (#352).
        renderer.setScheduledSceneHook([this](const std::string& id) {
            geode::viz::RouteArray routes{};
            std::lock_guard<std::mutex> lock(reactLock);
            reactions.setScene(id);
            if (reactions.resolve(routes)) renderer.setReactionRoutes(routes);
        });
    }

    geode::viz::Renderer renderer;
    geode::viz::GyroLook gyro;  // GL thread only (geode_viz_gyro_*)
    std::mutex configLock;
    std::array<geode::viz::LfoConfig, geode::viz::LfoEngine::kSlots> lfo{};
    std::array<geode::viz::AdsrConfig, geode::viz::AdsrEngine::kCount> adsr{};
    // Set by geode_viz_prepare_offscreen: every set_params then carries the export's pinned params.
    std::atomic<bool> offline{false};
    // The live shell's per-frame cadence (geode_viz_frame_begin); GL thread only.
    geode::viz::frame::FrameCadence cadence;
    // Which of the shell's pushed inputs this surface has applied (geode_viz_frame_begin); GL thread only.
    geode::viz::frame::PushVersions pushes;
    // What the reaction matrix plays (#750): scene, reactSeed, the user's routes and hand mask in, resolved
    // on the render thread only when one changed. reactLock guards it: set_params may come from any thread.
    std::mutex reactLock;
    geode::viz::ReactionResolve reactions{&styleTraits};
    // The live feed (geode_viz_feed_ring): the ring this renderer hears and its place in it. The cursor is
    // declared after the ring it refers to. Render thread only.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedRing;
    std::unique_ptr<geode::audio::ingest::Cursor> feedCursor;
    // The ring layered over it (geode_pcm_ring_set_layer) and this renderer's mix of the two.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedLayerRing;
    std::unique_ptr<geode::audio::ingest::LayerMix> feedLayer;

    static geode::viz::StyleTraits styleTraits(std::string_view id) {
        const std::string s(id);
        return {geode::viz::SceneRegistry::spinRate(s), geode::viz::SceneRegistry::isShaderStyle(s)};
    }
};

namespace {

using namespace geode::viz;

// Takes per geode_viz_feed_ring: one per rate in the frame's audio, with room for a change or two.
constexpr int kFeedRunsPerFrame = 4;

static_assert(LfoEngine::kSlots == GEODE_LFO_SLOTS);
static_assert(AdsrEngine::kCount == GEODE_ADSR_SLOTS);
static_assert(ReactionEngine::kRoutes == GEODE_REACT_ROUTES);
static_assert(ReactionEngine::kConfigFloats == GEODE_REACT_ROUTE_FLOATS);
static_assert(frame::edge_glow::kFloats == GEODE_EDGE_GLOW_FLOATS);
static_assert(frame::wallpaper::kOffsetFloats == GEODE_WALLPAPER_OFFSETS);
static_assert(frame::kFrameClearOnly == GEODE_FRAME_CLEAR_ONLY);
static_assert(frame::kFrameThermalDue == GEODE_FRAME_THERMAL_DUE);
static_assert(frame::kFrameLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(frame::kFrameErrorChanged == GEODE_FRAME_ERROR_CHANGED);
static_assert(frame::kFramePushParams == GEODE_FRAME_PUSH_PARAMS);
static_assert(frame::kFramePushLfo == GEODE_FRAME_PUSH_LFO);
static_assert(frame::kFramePushAdsr == GEODE_FRAME_PUSH_ADSR);
static_assert(frame::kFramePushTuning == GEODE_FRAME_PUSH_TUNING);
static_assert(frame::kFramePushTuning < (1u << frame::kFrameLiquidShift), "the push bits sit below the Liquid field");
static_assert(GEODE_FRAME_LIQUID_MODE(frame::kFrameLiquidMask << frame::kFrameLiquidShift) == static_cast<int>(frame::kFrameLiquidMask));

constexpr int kModSourceCount = static_cast<int>(ModSource::StereoPan) + 1;
constexpr int kLfoTargetCount = static_cast<int>(LfoTarget::EndlessZoomSpeed) + 1;
constexpr int kLfoWaveCount = static_cast<int>(LfoWave::RandomGlide) + 1;
constexpr int kModPolarityCount = static_cast<int>(ModPolarity::Negative) + 1;

// The thermal state's clock: CLOCK_MONOTONIC on Android, the same base as System.nanoTime. Only the gaps
// between calls matter, and steady_clock never jumps with the wall clock.
double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
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
    SceneParams p = SceneParams::fromWire(values);
    // Quality never adapts downward in an export: its wall-clock frame time is the encoder's pace.
    // The renderer takes this one value: a second setParams of the raw wire used to follow and put
    // the user's switch back, so an export's fluid lowered its quality whenever the encoder was slow.
    if (v->offline.load(std::memory_order_relaxed)) p.fluidAutoQuality = false;
    {
        std::lock_guard<std::mutex> lock(v->reactLock);
        v->reactions.setSeed(p.reactSeed);
    }
    v->renderer.setParams(p);
}

void geode_viz_set_reduced_motion(geode_viz* v, int on) {
    if (v) v->renderer.setReducedMotion(on != 0);
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

void geode_viz_set_fluid_injection(geode_viz* v, const char* force_source, const char* dye_source) {
    if (v) v->renderer.setFluidInjectionShaders(text(force_source), text(dye_source));
}

void geode_viz_load_milk_preset(geode_viz* v, const char* path) {
    if (v && path) v->renderer.loadMilkPreset(path);
}

void geode_viz_reload_milk_preset(geode_viz* v) {
    if (v) v->renderer.reloadMilkPreset();
}

void geode_viz_set_milk_texture_dir(geode_viz* v, const char* dir) {
    if (v && dir) v->renderer.setMilkTextureDir(dir);
}

size_t geode_viz_take_milk_preset_loaded(geode_viz* v, char* out, size_t capacity) {
    if (!v) return 0;
    const std::string path = v->renderer.takeMilkPresetLoaded();
    if (out && capacity > 0) {
        const size_t n = std::min(capacity - 1, path.size());
        std::memcpy(out, path.data(), n);
        out[n] = '\0';
    }
    return path.size();
}

int geode_viz_take_director_style_moments(geode_viz* v) {
    return v ? v->renderer.takeDirectorStyleMoments() : 0;
}

void geode_viz_write_audio(geode_viz* v, const float* interleaved, int frames, int channels, int sample_rate) {
    if (v) v->renderer.writeAudio(interleaved, frames, channels, sample_rate);
}

void geode_viz_feed_ring(geode_viz* v, const geode_pcm_ring* ring) {
    if (!v) return;
    if (!ring) {
        v->feedCursor.reset();
        v->feedRing.reset();
        v->feedLayer.reset();
        v->feedLayerRing.reset();
        v->renderer.setLayerAlone(false);
        return;
    }
    if (auto layer = ring->layerRing(); layer != v->feedLayerRing) {
        v->feedLayer.reset();
        v->feedLayerRing = std::move(layer);
        try {
            if (v->feedLayerRing) v->feedLayer = std::make_unique<geode::audio::ingest::LayerMix>(*v->feedLayerRing);
        } catch (...) {
            // No memory for the mix: hear the ring alone this frame and try again on the next.
            v->feedLayerRing.reset();
        }
    }
    if (v->feedRing != ring->ring) {
        v->feedCursor.reset();
        v->feedRing = ring->ring;
        try {
            v->feedCursor = std::make_unique<geode::audio::ingest::Cursor>(*v->feedRing);
        } catch (...) {
            // No memory for the cursor: hear nothing this frame and try again on the next.
            v->feedRing.reset();
            return;
        }
    }
    // A take ends where the rate changes, so a frame that straddles one takes twice; the bound keeps a
    // writer that outruns this loop from holding the frame.
    for (int run = 0; run < kFeedRunsPerFrame; ++run) {
        const geode::audio::ingest::Cursor::Take take = v->feedCursor->take();
        if (take.restarted) v->renderer.resetAudio();
        if (take.frames <= 0) break;
        const int channels = v->feedCursor->channels();
        const float* block = v->feedCursor->interleaved();
        if (v->feedLayer) block = v->feedLayer->mixed(block, take.frames, channels, take.sampleRateHz);
        v->renderer.writeAudio(block, take.frames, channels, take.sampleRateHz);
    }
    if (v->feedLayer) {
        // With the ring's own source stopped, the layer is what this renderer hears.
        const geode::audio::ingest::LayerMix::Alone alone = v->feedLayer->takeAlone();
        v->renderer.writeAudio(alone.interleaved, alone.frames, alone.channels, alone.sampleRateHz);
    }
    v->renderer.setLayerAlone(v->feedLayer && v->feedLayer->heardAlone());
}

void geode_viz_set_sound_tuning(geode_viz* v, const GeodeSoundTuning* tuning) {
    if (v && tuning) v->renderer.setSoundTuning(*tuning);
}

void geode_shader_override_set(const char* style_id, const char* fragment_source) {
    if (style_id) geode::viz::shader_overrides::set(style_id, fragment_source ? fragment_source : "");
}

size_t geode_shader_override(const char* style_id, char* out, size_t capacity) {
    if (!style_id) return 0;
    const std::string source = geode::viz::shader_overrides::get(style_id);
    if (out && capacity > 0) {
        const size_t n = std::min(capacity - 1, source.size());
        std::memcpy(out, source.data(), n);
        out[n] = '\0';
    }
    return source.size();
}

// Only the periods the editor offers; anything else is free timing.
static float beatsOf(float v) {
    for (float b : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f}) if (v == b) return b;
    return 0.0f;
}

void geode_viz_set_lfo(geode_viz* v, int slot, const float* c, int count) {
    if (!v || !c || slot < 0 || slot >= GEODE_LFO_SLOTS || count < 8) return;
    LfoConfig cfg;
    cfg.enabled = flag(c[0]);
    cfg.source = enumAt<ModSource>(c[1], kModSourceCount);
    cfg.target = enumAt<LfoTarget>(c[2], kLfoTargetCount);
    cfg.wave = enumAt<LfoWave>(c[3], kLfoWaveCount);
    cfg.rateSeconds = c[4];
    cfg.depth = c[5];
    cfg.polarity = enumAt<ModPolarity>(c[6], kModPolarityCount);
    cfg.curve = enumAt<ModCurve>(c[7], kModCurveCount);
    // Float 9 (beat timing) is absent from a shorter array: that slot stays free.
    if (count >= GEODE_LFO_CONFIG_FLOATS) cfg.beats = beatsOf(c[8]);
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

const char* geode_transitions_table(AAssetManager* assets) {
    static std::mutex lock;
    static std::string table;
    static bool loaded = false;
    std::lock_guard<std::mutex> guard(lock);
    if (!loaded && assets) {
        loaded = true;
        const auto text = geode::viz::ShaderSource(assets).readAsset("gl_transitions.json");
        if (text) table = geode::viz::encodeTransitionTable(geode::viz::parseTransitionLibrary(*text));
    }
    return table.c_str();
}

int geode_perf_tier(int mode, float* out, int capacity) {
    namespace frame = geode::viz::frame;
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
    return static_cast<int>(geode::viz::frame::modeForDevice(total_ram_bytes, low_ram != 0, media_performance_class));
}

double geode_pacer_on_vsync(long long* state, long long frame_time_ns, float cap_fps) {
    namespace frame = geode::viz::frame;
    static_assert(frame::kPacerStateLongs == GEODE_PACER_STATE_LONGS);
    static_assert(sizeof(long long) == sizeof(int64_t));
    auto* wire = reinterpret_cast<int64_t*>(state);
    static_assert(frame::kPacerCapSlot == GEODE_PACER_CAP_SLOT);
    static_assert(frame::kPacerPacedSlot == GEODE_PACER_PACED_SLOT);
    frame::PacerState s = frame::unpack(wire);
    // The thermal cap is applied here, once, for every surface the shell paces (ADR 0008).
    const float cap = geode::viz::ThermalState::process().capFor(cap_fps);
    const double dt = frame::Pacer::onVsync(s, frame_time_ns, cap);
    frame::pack(s, wire);
    return dt;
}

float geode_pacer_effective_cap(float cap_fps) {
    return geode::viz::ThermalState::process().capFor(cap_fps);
}

void geode_thermal_push(int platform_status, float headroom) {
    geode::viz::ThermalState::process().push(platform_status, headroom, monotonicSeconds());
}

void geode_memory_trim(int android_level) {
    geode::viz::SceneResidency::trimSignal().request(geode::viz::SceneResidency::trimForAndroidLevel(android_level));
}

void geode_pacer_resume(long long* state) {
    namespace frame = geode::viz::frame;
    auto* wire = reinterpret_cast<int64_t*>(state);
    frame::PacerState s = frame::unpack(wire);
    frame::Pacer::resume(s);
    frame::pack(s, wire);
}

}  // extern "C"

namespace {

// The routes layout (GEODE_REACT_ROUTE_FLOATS per route) in; returns how many routes were read.
int readRoutes(const float* c, int count, RouteArray& routes) {
    routes = RouteArray{};
    if (!c || count < 0) return 0;
    const int n = std::min(count / GEODE_REACT_ROUTE_FLOATS, ReactionEngine::kRoutes);
    for (int i = 0; i < n; ++i) {
        const float* r = c + i * GEODE_REACT_ROUTE_FLOATS;
        ReactRoute& out = routes[static_cast<size_t>(i)];
        out.enabled = flag(r[0]);
        out.source = enumAt<ReactSource>(r[1], kReactSourceCount);
        out.target = enumAt<LfoTarget>(r[2], kLfoTargetCount);
        out.mode = enumAt<ReactMode>(r[3], kReactModeCount);
        out.amount = std::clamp(r[4], -2.0f, 2.0f);
        out.curve = enumAt<ModCurve>(r[5], kModCurveCount);
        out.attackSeconds = std::clamp(r[6], 0.0f, 5.0f);
        out.releaseSeconds = std::clamp(r[7], 0.0f, 10.0f);
    }
    return n;
}

// The same layout out, at most capacity / ROUTE_FLOATS routes; returns how many were written.
int writeRoutes(const RouteArray& routes, int count, float* out, int capacity) {
    if (!out || capacity < GEODE_REACT_ROUTE_FLOATS) return 0;
    const int n = std::min(count, capacity / GEODE_REACT_ROUTE_FLOATS);
    for (int i = 0; i < n; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        float* o = out + i * GEODE_REACT_ROUTE_FLOATS;
        o[0] = r.enabled ? 1.0f : 0.0f;
        o[1] = static_cast<float>(r.source);
        o[2] = static_cast<float>(r.target);
        o[3] = static_cast<float>(r.mode);
        o[4] = r.amount;
        o[5] = static_cast<float>(r.curve);
        o[6] = r.attackSeconds;
        o[7] = r.releaseSeconds;
    }
    return n;
}

StyleDna dnaOf(const std::string& id, int seed) { return buildStyleDna(id, seed, geode_viz::styleTraits(id)); }

HandRoutes handOf(const float* user, int user_floats, int has_user, unsigned mask) {
    HandRoutes h;
    h.user = has_user != 0;
    if (h.user) h.count = readRoutes(user, user_floats, h.routes);
    h.mask = h.user ? (static_cast<std::uint32_t>(mask) & hand::kAll) : 0u;
    return h;
}

// Before a render: what plays, worked out again only when the scene, seed, routes or mask changed.
void syncReactions(geode_viz* v) {
    RouteArray routes{};
    std::lock_guard<std::mutex> lock(v->reactLock);
    if (v->reactions.resolve(routes)) v->renderer.setReactionRoutes(routes);
}

static_assert(kMomentCount == GEODE_MUSIC_MOMENTS);
static_assert(kMusicActionCount == GEODE_MUSIC_ACTIONS);
static_assert(hand::kAll == GEODE_HAND_ALL && hand::kUnclaimed == GEODE_HAND_UNCLAIMED);

}  // namespace

extern "C" {

void geode_viz_set_user_reactions(geode_viz* v, const float* routes, int count, int has_user, unsigned mask) {
    if (!v) return;
    const HandRoutes h = handOf(routes, count, has_user, mask);
    std::lock_guard<std::mutex> lock(v->reactLock);
    v->reactions.setHand(h);
}

void geode_hand_set(unsigned mask, int has_user) { hand::setGlobal(static_cast<std::uint32_t>(mask), has_user != 0); }

int geode_style_reactions_hand(const char* style_id, int seed, const float* user, int user_floats, int has_user, unsigned mask,
                               float* out, int capacity) {
    if (!style_id) return 0;
    RouteArray playing;
    const int n = mergeHand(handOf(user, user_floats, has_user, mask), dnaOf(style_id, seed), playing);
    return writeRoutes(playing, n, out, capacity);
}

int geode_music_state(const char* style_id, int seed, const float* user, int user_floats, int has_user, unsigned mask, float* out,
                      int capacity) {
    if (!style_id || !out) return 0;
    const HandRoutes h = handOf(user, user_floats, has_user, mask);
    RouteArray playing;
    const int n = mergeHand(h, dnaOf(style_id, seed), playing);
    const int moments = std::min(kMomentCount, capacity / GEODE_MUSIC_STATE_FLOATS);
    for (int m = 0; m < moments; ++m) {
        const MomentState s = momentState(h, playing, n, static_cast<Moment>(m));
        float* o = out + m * GEODE_MUSIC_STATE_FLOATS;
        o[0] = s.mine ? 1.0f : 0.0f;
        o[1] = static_cast<float>(s.routes);
        o[2] = s.strength;
        o[3] = static_cast<float>(s.on);
        o[4] = static_cast<float>(s.fits);
    }
    return moments;
}

int geode_music_edit(const char* style_id, int seed, const float* user, int user_floats, int has_user, unsigned mask, int op,
                     int moment, int action, float value, float* out, int capacity, unsigned* out_mask, int* out_has_user) {
    const HandRoutes h = handOf(user, user_floats, has_user, mask);
    HandRoutes next = h;
    if (style_id && op >= 0 && op < kMusicEditCount && moment >= 0 && moment < kMomentCount && action >= 0 &&
        action < kMusicActionCount) {
        next = editMusic(h, dnaOf(style_id, seed), static_cast<MusicEdit>(op), static_cast<Moment>(moment),
                         static_cast<MusicAction>(action), value);
    }
    if (out_mask) *out_mask = next.user ? next.mask : 0u;
    if (out_has_user) *out_has_user = next.user ? 1 : 0;
    return next.user ? writeRoutes(next.routes, next.count, out, capacity) : 0;
}

unsigned geode_music_actions_for(const char* style_id) { return style_id ? modtargets::musicActionsFor(style_id) : 0u; }

const char* geode_mod_targets_text(void) { return modtargets::text().c_str(); }

static_assert(kEventRecipeCount == GEODE_EVENT_RECIPES);

int geode_event_recipe_routes(int recipe, const char* style_id, float* out, int capacity) {
    if (recipe < 0 || recipe >= kEventRecipeCount) return 0;
    const auto r = static_cast<EventRecipe>(recipe);
    const RecipeRoutes routes = style_id ? recipeRoutesFor(r, style_id) : recipeRoutes(r);
    RouteArray all{};
    for (int i = 0; i < routes.count; ++i) all[static_cast<size_t>(i)] = routes.routes[static_cast<size_t>(i)];
    return writeRoutes(all, routes.count, out, capacity);
}

unsigned geode_event_recipe_state(int recipe, const char* style_id, const float* current, int current_floats) {
    if (!style_id || recipe < 0 || recipe >= kEventRecipeCount) return 0u;
    const auto r = static_cast<EventRecipe>(recipe);
    const std::string id(style_id);
    RouteArray routes;
    const int n = readRoutes(current, current_floats, routes);
    return (recipeIsOn(r, routes, n, id) ? GEODE_RECIPE_ON : 0u) | (recipeFits(r, routes, n, id) ? GEODE_RECIPE_FITS : 0u);
}

int geode_event_recipe_toggle(int recipe, const char* style_id, const float* current, int current_floats, float* out, int capacity) {
    RouteArray routes;
    const int n = readRoutes(current, current_floats, routes);
    if (!style_id || recipe < 0 || recipe >= kEventRecipeCount) return writeRoutes(routes, n, out, capacity);
    RouteArray next;
    const int k = toggleRecipe(static_cast<EventRecipe>(recipe), routes, n, style_id, next);
    return writeRoutes(next, k, out, capacity);
}

int geode_react_source_catalog(char* out, int capacity) {
    std::string text;
    for (int i = 0; i < kReactSourceCount; ++i) {
        const ReactSourceInfo& s = reactSourceInfo(static_cast<ReactSource>(i));
        text += s.name;
        text += '|';
        text += s.label;
        text += '|';
        text += s.group;
        text += s.event ? "|1\n" : "|0\n";
    }
    const int need = static_cast<int>(text.size());
    if (out && capacity > 0) {
        const int n = std::min(need, capacity - 1);
        std::memcpy(out, text.data(), static_cast<size_t>(n));
        out[n] = '\0';
    }
    return need;
}

void geode_macros_set(const float* knobs, int count) { macros::setGlobal(macros::clamped(knobs, count)); }

void geode_director_set(int on, float amount, long long seed) {
    director::setGlobal(on != 0, amount, static_cast<uint64_t>(seed));
}

void geode_note_response_set(int enabled) { note_response::setGlobal(enabled != 0); }

void geode_layer_set(const char* scene_id, float mix, int blend_mode) {
    layer_state::set(layer_state::Layer{text(scene_id), mix, blend_mode});
}

void geode_macros_surprise(const float* knobs, int count, unsigned lock_mask, long long seed, float* out) {
    if (!out) return;
    const MacroKnobs k = macros::surprise(macros::clamped(knobs, count), lock_mask, static_cast<uint64_t>(seed));
    for (int i = 0; i < count && i < kMacroCount; ++i) out[i] = k[static_cast<size_t>(i)];
}

int geode_macro_starters(char* out, int capacity) {
    std::string text;
    for (const macros::Starter& s : macros::starters()) {
        text += s.name;
        for (int i = 0; i < kMacroCount; ++i) {
            text += i == 0 ? '|' : ',';
            text += std::to_string(s.knobs[static_cast<size_t>(i)]);
        }
        text += '\n';
    }
    const int need = static_cast<int>(text.size());
    if (out && capacity > 0) {
        const int n = std::min(need, capacity - 1);
        std::memcpy(out, text.data(), static_cast<size_t>(n));
        out[n] = '\0';
    }
    return need;
}

int geode_style_reactions(const char* style_id, int seed, float* out, int capacity) {
    if (!style_id) return 0;
    const StyleDna dna = dnaOf(style_id, seed);
    return writeRoutes(dna.routes, dna.count, out, capacity);
}

void geode_viz_gyro_sample(geode_viz* v, float azimuth, float pitch) {
    if (!v) return;
    const GyroLook::Look look = v->gyro.sample(azimuth, pitch);
    v->renderer.setLook(look.yaw, look.pitch);
}

void geode_viz_gyro_reset(geode_viz* v) {
    if (!v) return;
    const GyroLook::Look look = v->gyro.reset();
    v->renderer.setLook(look.yaw, look.pitch);
}

int geode_viz_frame_begin(geode_viz* v, float paced_fps, float render_scale, float march_scale, int post_fx,
                          int adaptive, int liquid_hot_policy, long long params_version, long long lfo_version,
                          long long adsr_version, long long tuning_version) {
    if (!v) return 0;
    Renderer& r = v->renderer;
    r.thermal().setPacedFps(paced_fps);
    const frame::UserQuality quality{render_scale, march_scale, post_fx != 0, adaptive != 0, liquid_hot_policy};
    if (v->cadence.offerQuality(quality)) {
        r.setUserQuality(quality.renderScale, quality.marchScale, quality.postFx, quality.adaptive);
        r.setLiquidHotPolicy(quality.liquidHotPolicy);
    }
    uint32_t due = v->cadence.step([] { return ThermalState::process().headroomDue(monotonicSeconds()); },
                                         [&r] { return r.takeLiquidMode(); });
    if (due & frame::kFrameClearOnly) {
        // The shell returns at once on this frame, so nothing it would push may count as applied yet.
        r.clearToBlack(0);
        return static_cast<int>(due);
    }
    // An error raised while a frame drew is told on the next one, so the shell asks for the text only then.
    if (v->cadence.offerError(r.lastError())) due |= frame::kFrameErrorChanged;
    const uint32_t want = v->pushes.offer({params_version, lfo_version, adsr_version, tuning_version});
    return static_cast<int>(due | want);
}

unsigned geode_viz_pin_engine_settings(geode_viz* v, int entitled, long long duration_ms) {
    return v ? static_cast<unsigned>(v->renderer.pinEngineSettings(entitled != 0, duration_ms)) : 0u;
}

void geode_viz_set_frame_rate(geode_viz* v, int fps) {
    if (v) v->renderer.setFixedFrameRate(fps);
}

void geode_viz_set_offscreen(geode_viz* v, int on) {
    if (!v) return;
    // An export never shows a half-built scene: each one is built whole on the frame that asks for it.
    v->renderer.setBlockingBuilds(on != 0);
    // Nor does it ever carry the app's sheet blur into the video.
    v->renderer.setOffscreen(on != 0);
    // And it keeps the Customize layer it started with: a live edit during a background render stays live.
    v->renderer.pinLayer(on != 0);
    if (on) {
        v->renderer.thermal().beginOffscreenRender();
    } else {
        v->renderer.thermal().endOffscreenRender();
    }
}

void geode_viz_set_ui_blur_region(geode_viz* v, float left, float top, float right, float bottom, float strength,
                                  float corner_px) {
    if (!v) return;
    geode::viz::UiBlurRegion region;
    region.left = left;
    region.top = top;
    region.right = right;
    region.bottom = bottom;
    region.strength = strength;
    region.cornerPx = corner_px;
    v->renderer.setUiBlurRegion(region);
}

void geode_viz_set_edge_glow(geode_viz* v, const float* opts, int count) {
    if (!v) return;
    v->renderer.setEdgeGlow(geode::viz::frame::edge_glow::parse(opts, count));
}

void geode_wallpaper_window(float surface_w, float surface_h, float display_w, float display_h,
                            const float* offsets, int count, float* out) {
    if (!out) return;
    namespace wp = geode::viz::frame::wallpaper;
    const wp::Window w = wp::window(surface_w, surface_h, display_w, display_h, wp::parseOffsets(offsets, count));
    out[0] = w.left;
    out[1] = w.top;
    out[2] = w.width;
    out[3] = w.height;
}

float geode_wallpaper_fps(float target_fps, float cap_fps) {
    return geode::viz::frame::wallpaper::frameRate(target_fps, cap_fps);
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

void geode_viz_set_underlay_rgba(geode_viz* v, const uint32_t* pixels, int width, int height, int blend, float amount) {
    if (v) v->renderer.setUnderlayRgba(pixels, width, height, blend, amount);
}

// Any thread (above the GL-thread divider). Renderer::lastError() copies the
// string out under its own lock, so this never reads a Renderer-owned buffer
// that Renderer::fail() (GL thread) could be mutating concurrently. The
// cache is thread_local (not per-`v`) so two threads calling this
// concurrently, even on the same `v`, never race each other's returned
// pointer: the returned pointer stays valid until the *calling thread's*
// next call to geode_viz_last_error (on any instance).
const char* geode_viz_last_error(geode_viz* v) {
    thread_local std::string cache;
    if (!v) return "";
    cache = v->renderer.lastError();
    return cache.c_str();
}

void geode_viz_surface_created(geode_viz* v) {
    if (!v) return;
    v->renderer.onSurfaceCreated();
    v->cadence.surfaceCreated();
    v->pushes.surfaceCreated();
}

void geode_viz_surface_changed(geode_viz* v, int width, int height) {
    if (v) v->renderer.surfaceChanged(std::max(width, 1), std::max(height, 1));
}

int geode_viz_set_scene(geode_viz* v, const char* scene_id) {
    if (!v || !scene_id) return 0;
    {
        // The routes follow the scene asked for, drawable or not, as the Kotlin cache did.
        std::lock_guard<std::mutex> lock(v->reactLock);
        v->reactions.setScene(scene_id);
    }
    return v->renderer.setScene(scene_id) ? 1 : 0;
}

void geode_viz_set_style_schedule(geode_viz* v, const double* at_ms, const char* const* scene_ids,
                                  const char* const* milk_paths, int count, const char* const* cut_ids, int cut_count,
                                  int every_beats, const char* const* locked_ids, int locked_count) {
    if (!v) return;
    std::vector<geode::viz::StyleCue> cues;
    for (int i = 0; at_ms && scene_ids && i < count; ++i) {
        if (!scene_ids[i]) continue;
        geode::viz::StyleCue cue;
        cue.atMs = at_ms[i];
        cue.sceneId = scene_ids[i];
        cue.milkPath = (milk_paths && milk_paths[i]) ? milk_paths[i] : "";
        cues.push_back(std::move(cue));
    }
    std::vector<std::string> cut;
    for (int i = 0; cut_ids && i < cut_count; ++i) {
        if (cut_ids[i]) cut.emplace_back(cut_ids[i]);
    }
    std::vector<std::string> locked;
    for (int i = 0; locked_ids && i < locked_count; ++i) {
        if (locked_ids[i]) locked.emplace_back(locked_ids[i]);
    }
    v->renderer.setStyleSchedule(std::move(cues), std::move(cut), every_beats, locked);
}

void geode_viz_warm_transition(geode_viz* v, const char* id) {
    if (v && id) v->renderer.warmTransition(id);
}

void geode_viz_cut(geode_viz* v) {
    if (v) v->renderer.cut();
}

void geode_viz_render(geode_viz* v, double time_seconds, uint32_t target_fbo) {
    if (!v) return;
    syncReactions(v);
    v->renderer.render(time_seconds, target_fbo);
}

void geode_viz_prepare_offscreen(geode_viz* v) {
    if (!v) return;
    // Pinned to the full tier: an export renders as fast as the encoder takes frames, so its wall-clock
    // frame time says nothing about the device.
    geode_viz_set_offscreen(v, 1);
    // A schedule's style switch lands on its frame: the CUT transition, which safety::transitionId turns
    // into a fade, at 0 ms (held to 1 ms by setTransition), exactly what the Kotlin shell used to ask for.
    v->renderer.setTransition(transitionStyleId(TransitionStyle::Cut), 0);
    v->offline.store(true, std::memory_order_relaxed);
}

void geode_viz_release_scenes(geode_viz* v) {
    if (v) v->renderer.releaseScenes();
}

void geode_viz_trim_spares(geode_viz* v) {
    // Without a current context (the surface already gone) every GL delete is a no-op: the scenes would be
    // forgotten while their objects stayed in the kept context. Then nothing is shed here.
    if (!v || eglGetCurrentContext() == EGL_NO_CONTEXT) return;
    v->renderer.trimSpares();
}

}
