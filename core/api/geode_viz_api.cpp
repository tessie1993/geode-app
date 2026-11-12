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
#include <utility>
#include <vector>

#include <time.h>

#include <EGL/egl.h>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "api/geode_modulation_api.h"
#include "api/geode_text_out.hpp"
#include "api/geode_record_handles.hpp"
#include "sound/Tuning.hpp"
#include "viz/Catalogue.hpp"
#include "audio/ingest/LayerMix.hpp"
#include "viz/Director.hpp"
#include "viz/DirectorReach.hpp"
#include "viz/GyroLook.hpp"
#include "viz/TapAlong.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/LayerPick.hpp"
#include "viz/LayerState.hpp"
#include "viz/LookSnapshot.hpp"
#include "viz/MacroBank.hpp"
#include "viz/ModDocument.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/ReactionResolve.hpp"
#include "viz/Renderer.hpp"
#include "viz/SceneRegistry.hpp"
#include "viz/SceneResidency.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/Transition.hpp"
#include "viz/TransitionChoice.hpp"
#include "viz/TransitionLibrary.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/LiveFrame.hpp"
#include "viz/frame/Pacer.hpp"
#include "viz/frame/Thermal.hpp"
#include "viz/frame/ThermalSource.hpp"
#include "viz/frame/RecordTap.hpp"
#include "viz/frame/CastTee.hpp"
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
        renderer.setCompositeHook([this](GLuint fbo, int width, int height) {
            recordFrame(fbo, width, height);
            castFrame(fbo, width, height);
            stillFrame(fbo, width, height);
        });
    }

    // Cast's tee (geode_viz_set_cast_source): the stage's finished visual into the running cast's tee, at most once
    // per cast frame (the tee decides). A tee that ended, or a renderer no longer the source, frees its
    // textures here, in this context. Render thread only, apart from the flag.
    void castFrame(GLuint fbo, int width, int height) {
        std::shared_ptr<geode::viz::frame::CastTee> now =
            castSource.load(std::memory_order_relaxed) ? geode::viz::frame::CastTee::current() : nullptr;
        if (now != castTee) {
            if (castTee) castTee->releaseWriter();
            castTee = std::move(now);
        }
        if (!castTee) return;
        const auto since = std::chrono::steady_clock::now().time_since_epoch();
        castTee->publish(fbo, width, height, std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
    }

    // Shake to screenshot (geode_viz_still_request): the finished visual read back once, flipped to rows
    // top first. Render thread only, apart from the request flag.
    void stillFrame(GLuint fbo, int width, int height) {
        if (!stillWanted.exchange(false) || width <= 0 || height <= 0) return;
        const size_t row = static_cast<size_t>(width) * 4u;
        still.assign(row * static_cast<size_t>(height), 0);
        GLint previous = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, still.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));
        std::vector<uint8_t> swap(row);
        for (int y = 0; y < height / 2; ++y) {
            uint8_t* a = still.data() + row * static_cast<size_t>(y);
            uint8_t* b = still.data() + row * static_cast<size_t>(height - 1 - y);
            std::memcpy(swap.data(), a, row);
            std::memcpy(a, b, row);
            std::memcpy(b, swap.data(), row);
        }
        stillWidth = width;
        stillHeight = height;
        stillFresh = true;
    }

    // Record's tee (geode_viz_record_start): the finished visual into the encoder's surface, when the
    // recorder's clock kept this frame (recordPtsNs, asked in geode_viz_render). A frame the driver will not
    // take ends the recording. Render thread only.
    void recordFrame(GLuint fbo, int width, int height) {
        if (!recorder || recordPtsNs < 0) return;
        bool ok = recordTap.begin(fbo, width, height);
        if (ok) {
            if (recordWatermark) geode_export_watermark_draw(recordWatermark, recordTap.width(), recordTap.height(), 0);
            ok = recordTap.present(recordPtsNs, fbo);
        }
        if (!ok) recorder->stop(geode::output::record::Stop::Failed, geode_record_now_ns());
    }

    geode::viz::Renderer renderer;
    geode::viz::GyroLook gyro;  // GL thread only (geode_viz_gyro_*)
    std::mutex configLock;
    std::array<geode::viz::LfoConfig, geode::viz::LfoEngine::kSlots> lfo{};
    std::array<geode::viz::AdsrConfig, geode::viz::AdsrEngine::kCount> adsr{};
    // Set by geode_viz_prepare_offscreen: every set_params then carries the export's pinned params.
    std::atomic<bool> offline{false};
    // The live frame (geode_viz_frame): its order, and the scene, quality and gyro latched for it from any
    // thread. Everything else of the look is latched by the renderer itself, and a new surface keeps it all
    // (Renderer::onSurfaceCreated forgets GL objects and the stages' motion, never what the shell set), so the
    // shell never sends the look again for a new surface.
    geode::viz::frame::LiveFrame live;
    // The shell's whole look as set last (geode_viz_set_look), so only the parts that differ reach the renderer; a new
    // handle starts with none, so it gets the whole look. lookLock: the compare and its sets never interleave.
    std::mutex lookLock;
    geode::viz::LookLatch shellLook;
    // What the reaction matrix plays (#750): scene, reactSeed, the user's routes and the hand mask in,
    // resolved on the render thread only when one changed. The mask is read before each render (the
    // process's, or an export's pin), so a Customize › Music edit reaches every renderer with no push of its
    // own. reactLock guards both: set_params and set_user_reactions may come from any thread.
    std::mutex reactLock;
    geode::viz::ReactionResolve reactions{&styleTraits};
    geode::viz::HandRoutes userHand;
    // The live feed (geode_viz_feed_ring): the ring this renderer hears and its place in it. The cursor is
    // declared after the ring it refers to. Render thread only.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedRing;
    std::unique_ptr<geode::audio::ingest::Cursor> feedCursor;
    // The ring layered over it (geode_pcm_ring_set_layer) and this renderer's mix of the two.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedLayerRing;
    std::unique_ptr<geode::audio::ingest::LayerMix> feedLayer;
    // Record (geode_viz_record_start): the recording's clock, shared with the encoder thread's handle, the
    // GL copy into the encoder and the frame's PTS, -1 for a frame the clock left out. Render thread only.
    std::shared_ptr<geode::output::record::Recorder> recorder;
    geode::viz::frame::RecordTap recordTap;
    geode_export_pass* recordWatermark = nullptr;
    int64_t recordPtsNs = -1;
    // Cast's tee (geode_viz_set_cast_source): whether this renderer is the stage the cast carries, and the tee it
    // last copied into (render thread only).
    std::atomic<bool> castSource{false};
    std::shared_ptr<geode::viz::frame::CastTee> castTee;
    std::atomic<bool> stillWanted{false};
    std::vector<uint8_t> still;
    int stillWidth = 0;
    int stillHeight = 0;
    // Read back since the last live frame told the shell (GEODE_FRAME_STILL_READY). Render thread only.
    bool stillFresh = false;
    // Tap along: the beat as the last frame heard it, against the shell's taps. tapLock: taps come from the UI thread.
    std::mutex tapLock;
    geode::viz::TapAlong tapAlong;

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
static_assert(frame::kEventClearOnly == GEODE_FRAME_CLEAR_ONLY);
static_assert(frame::kEventLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(frame::kEventErrorChanged == GEODE_FRAME_ERROR_CHANGED);
static_assert(frame::kEventMilkLoaded == GEODE_FRAME_MILK_LOADED);
static_assert(frame::kEventDirectorMoment == GEODE_FRAME_DIRECTOR_MOMENT);
static_assert(frame::kEventStillReady == GEODE_FRAME_STILL_READY);
static_assert(static_cast<int>(BlendMode::Screen) == GEODE_LAYER_BLEND_DEFAULT);
static_assert(layer_state::kBlends == kBlendModes && layer_state::kDefaultBlend == GEODE_LAYER_BLEND_DEFAULT);
static_assert(static_cast<int>(layer_state::Field::On) == GEODE_LAYER_ON);
static_assert(static_cast<int>(layer_state::Field::Source) == GEODE_LAYER_SOURCE);
static_assert(static_cast<int>(layer_state::Field::Knob) == GEODE_LAYER_KNOB);
static_assert(static_cast<int>(layer_state::Field::Blend) == GEODE_LAYER_BLEND);
static_assert(static_cast<int>(layer_state::Field::Mix) == GEODE_LAYER_MIX);
static_assert(static_cast<int>(layer_state::Field::Style) == GEODE_LAYER_STYLE);
static_assert(static_cast<int>(layer_state::Source::User) == GEODE_LAYER_SOURCE_USER);
static_assert(static_cast<int>(layer_state::Source::Director) == GEODE_LAYER_SOURCE_DIRECTOR);
static_assert(static_cast<int>(layer_state::Source::Macro) == GEODE_LAYER_SOURCE_MACRO);
static_assert(layer_state::kSources == 3);

// The thermal state's clock: CLOCK_MONOTONIC on Android, the same base as System.nanoTime. Only the gaps
// between calls matter, and steady_clock never jumps with the wall clock.
double monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A live frame's time: CLOCK_BOOTTIME, since boot with deep sleep counted, the clock SystemClock.elapsedRealtimeNanos
// reads, so the picture's clock runs on as it did when the shell stamped each frame.
double bootSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

// Customize › Layers' texts: valid until this thread's next layer call, like the macro bank's.
const char* layerOut(std::string out) {
    thread_local std::string held;
    held = std::move(out);
    return held.c_str();
}

// The Macro source's choice: the knobs on screen, as the bank every live renderer reads has them.
std::vector<std::pair<int, std::string>> layerKnobs() {
    std::vector<std::pair<int, std::string>> knobs;
    if (const auto bank = macrobank::global()) {
        for (const macrobank::Knob& knob : bank->knobs) {
            if (knob.shown) knobs.emplace_back(knob.id, knob.name);
        }
    }
    return knobs;
}

// Customize › Layers' page over the style on screen, for the layer native keeps.
layer_pick::Editor layerPage(const char* active_style, int family) {
    std::vector<int> ids;
    for (const auto& knob : layerKnobs()) ids.push_back(knob.first);
    return layer_pick::editor(layer_state::get(), text(active_style), layer_state::shown(), family, ids);
}

// One entry of a list that crosses the ABI a line each: no line break gets in from a name.
void addLine(std::string& out, std::string entry) {
    std::replace(entry.begin(), entry.end(), '\n', ' ');
    std::replace(entry.begin(), entry.end(), '\r', ' ');
    out += entry;
    out += '\n';
}

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

void geode_viz_set_touch_smear(geode_viz* v, int on, float strength) {
    if (v) v->renderer.setTouchSmear(on != 0, strength);
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

int geode_viz_hear_track(geode_viz* v, geode_track_feed* feed) {
    // With no renderer the frames are still handed out (to nobody), so the feed keeps its place.
    const geode_track_sink sink = [](void* context, const float* interleaved, int frames, int channels,
                                     int sample_rate) {
        geode_viz_write_audio(static_cast<geode_viz*>(context), interleaved, frames, channels, sample_rate);
    };
    return geode_track_feed_deliver(feed, v ? sink : nullptr, v);
}

int geode_viz_end_pre_roll(geode_viz* v, int64_t range_start_ms) {
    return v && v->renderer.endPreRoll(range_start_ms) ? 1 : 0;
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
    // Speaker sync: behind the player's sink the engine hears up to the sample the speaker plays now.
    // steady_clock is CLOCK_MONOTONIC on Android, the clock System.nanoTime stamps the sink's reports with.
    const auto sinceBoot = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(sinceBoot).count();
    const geode_playout* playout = ring->playout.load(std::memory_order_acquire);
    // A ring no sink plays is not the player's: its engine never takes the player's source, fade or boundary.
    v->renderer.setFedPlayersRing(playout != nullptr);
    const int64_t limit = geode_playout_listen_limit(playout, nowNs);
    // A take ends where the rate changes, so a frame that straddles one takes twice; the bound keeps a
    // writer that outruns this loop from holding the frame.
    for (int run = 0; run < kFeedRunsPerFrame; ++run) {
        const geode::audio::ingest::Cursor::Take take = v->feedCursor->take(limit);
        if (take.restarted) {
            v->renderer.resetAudio();
            if (v->recorder) v->recorder->restarted();
        }
        if (take.frames <= 0) break;
        const int channels = v->feedCursor->channels();
        const float* block = v->feedCursor->interleaved();
        if (v->feedLayer) block = v->feedLayer->mixed(block, take.frames, channels, take.sampleRateHz);
        v->renderer.writeAudio(block, take.frames, channels, take.sampleRateHz);
        // Record's sound is exactly this: what the engine just heard, the mic layer included.
        if (v->recorder) v->recorder->hear(block, take.frames, channels, take.sampleRateHz, geode_record_now_ns());
    }
    if (v->feedLayer) {
        // With the ring's own source stopped, the layer is what this renderer hears.
        const geode::audio::ingest::LayerMix::Alone alone = v->feedLayer->takeAlone();
        v->renderer.writeAudio(alone.interleaved, alone.frames, alone.channels, alone.sampleRateHz);
        if (v->recorder && alone.frames > 0) {
            v->recorder->hear(alone.interleaved, alone.frames, alone.channels, alone.sampleRateHz, geode_record_now_ns());
        }
    }
    v->renderer.setLayerAlone(v->feedLayer && v->feedLayer->heardAlone());
}

void geode_viz_set_sound_tuning(geode_viz* v, const float* saved, int count) {
    if (!v) return;
    float slots[GEODE_SOUND_TUNING_FLOATS];
    geode::sound::tuning::resolve(saved, count, slots);
    GeodeSoundTuning t;
    std::memcpy(&t, slots, sizeof t);
    v->renderer.setSoundTuning(t);
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

const char* geode_transition_picker(AAssetManager* assets) {
    namespace viz = geode::viz;
    static const std::vector<std::string> builtIns = viz::transition_choice::pickerIds(viz::TransitionCatalog::builtInIds());
    // Without the assets the built-ins alone; each string is written once, so a pointer handed out stays valid.
    static const std::string withoutLibrary = viz::encodeTransitionPicker(builtIns, {});
    static std::mutex lock;
    static std::string table;
    static bool loaded = false;
    std::lock_guard<std::mutex> guard(lock);
    if (!loaded && assets) {
        loaded = true;
        const auto text = viz::ShaderSource(assets).readAsset("gl_transitions.json");
        table = viz::encodeTransitionPicker(builtIns, text ? viz::parseTransitionLibrary(*text) : std::vector<viz::TransitionDef>());
    }
    return loaded ? table.c_str() : withoutLibrary.c_str();
}

int geode_live_fps_choices(const float* rates, int count, int* out, int capacity) {
    static_assert(geode::viz::frame::kLiveFpsDefault == GEODE_LIVE_FPS_DEFAULT);
    static_assert(geode::viz::frame::kLiveFpsFollow == GEODE_LIVE_FPS_FOLLOW);
    // Every rung, then Follow device.
    static_assert(static_cast<int>(geode::viz::frame::kLiveFpsRungs.size()) + 1 == GEODE_LIVE_FPS_MAX_CHOICES);
    return geode::viz::frame::liveFpsChoices(rates, std::max(count, 0), out, std::max(capacity, 0));
}

int geode_live_fps_cap(int stored_fps, const float* rates, int count) {
    return geode::viz::frame::liveFpsCap(stored_fps, rates, std::max(count, 0));
}

double geode_pacer_on_vsync(long long* state, long long frame_time_ns, float cap_fps) {
    namespace frame = geode::viz::frame;
    static_assert(frame::kPacerStateLongs == GEODE_PACER_STATE_LONGS);
    static_assert(sizeof(long long) == sizeof(int64_t));
    auto* wire = reinterpret_cast<int64_t*>(state);
    static_assert(frame::kPacerCapSlot == GEODE_PACER_CAP_SLOT);
    static_assert(frame::kPacerPacedSlot == GEODE_PACER_PACED_SLOT);
    frame::PacerState s = frame::unpack(wire);
    // The thermal cap is applied here, once, for every surface the shell paces (ADR 0008); Follow device
    // takes its cap from the panel the state has measured (frame::followCapFps).
    const float cap = frame::pacerCapFps(s, cap_fps, geode::viz::ThermalState::process().fpsRungsDown());
    const double dt = frame::Pacer::onVsync(s, frame_time_ns, cap);
    frame::pack(s, wire);
    return dt;
}

float geode_pacer_effective_cap(float cap_fps) {
    // Follow device states no rate, hot or cool: the system keeps choosing the panel's mode (setFrameRate(0)
    // is the platform's "no preference"), and a hot phone's step is the pacer's divide of whatever it chose.
    if (geode::viz::frame::isLiveFpsFollow(cap_fps)) return 0.0f;
    return geode::viz::ThermalState::process().capFor(cap_fps);
}

int geode_thermal_take_warm_note(float cap_fps, float paced_fps) {
    return geode::viz::ThermalState::process().takeWarmNote(cap_fps, paced_fps);
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

StyleDna dnaOf(const std::string& id, int seed) { return buildStyleDna(id, seed, geode_viz::styleTraits(id)); }

// The user's reactions as saved: no text, or text that is no route list, is Auto.
ModDocument reactionsOf(const char* reactions, unsigned mask) {
    ModDocument doc;
    if (reactions) doc.readReactions(reactions, static_cast<std::uint32_t>(mask));
    return doc;
}

// Before a render: what plays, worked out again only when the scene, seed, routes or mask changed.
void syncReactions(geode_viz* v) {
    RouteArray routes{};
    const std::uint32_t mask = v->renderer.handMask();
    std::lock_guard<std::mutex> lock(v->reactLock);
    HandRoutes user = v->userHand;
    user.mask = user.user ? (mask & hand::kAll) : 0u;
    v->reactions.setHand(user);
    if (v->reactions.resolve(routes)) v->renderer.setReactionRoutes(routes);
}

// The live frame's stages, bound to one renderer and this frame's ring (frame::LiveFrame keeps the order).
class VizStages final : public frame::LiveStages {
public:
    VizStages(geode_viz& v, const geode_pcm_ring* ring) : v_(v), ring_(ring) {}

    void setPacedFps(float pacedFps, float capFps) override { v_.renderer.thermal().setPacedFps(pacedFps, capFps); }
    void applyQuality(const frame::UserQuality& q) override {
        v_.renderer.setUserQuality(q.renderScale, q.marchScale, q.postFx, q.adaptive);
        v_.renderer.setLiquidHotPolicy(q.liquidHotPolicy);
    }
    void clearToBlack() override { v_.renderer.clearToBlack(0); }
    bool claimThermal() override { return ThermalState::process().headroomDue(monotonicSeconds()); }
    void sampleThermal() override { thermal_source::sampleHeadroom(monotonicSeconds()); }
    int takeLiquidMode() override { return v_.renderer.takeLiquidMode(); }
    void gyroReset() override {
        const GyroLook::Look look = v_.gyro.reset();
        v_.renderer.setLook(look.yaw, look.pitch);
    }
    void gyroSample(const frame::GyroReading& r) override {
        const GyroLook::Look look = v_.gyro.sampleScreen(r.x, r.y, r.z, r.w, r.rotation);
        v_.renderer.setLook(look.yaw, look.pitch);
    }
    bool setScene(const std::string& id) override { return geode_viz_set_scene(&v_, id.c_str()) != 0; }
    void feedRing() override { geode_viz_feed_ring(&v_, ring_); }
    void render() override { geode_viz_render(&v_, bootSeconds(), 0); }
    std::string lastError() override { return v_.renderer.lastError(); }
    bool milkPresetLoaded() override { return v_.renderer.milkPresetLoadedPending(); }
    bool directorMomentsPending() override { return v_.renderer.directorStyleMomentsPending(); }
    bool stillReady() override { return std::exchange(v_.stillFresh, false); }

private:
    geode_viz& v_;
    const geode_pcm_ring* ring_;
};

static_assert(kMomentCount == GEODE_MUSIC_MOMENTS);
static_assert(kMusicActionCount == GEODE_MUSIC_ACTIONS);
static_assert(hand::kAll == GEODE_HAND_ALL && hand::kUnclaimed == GEODE_HAND_UNCLAIMED);

}  // namespace

extern "C" {

void geode_viz_set_lfos(geode_viz* v, const char* lfos) {
    if (!v) return;
    ModDocument doc;
    doc.readLfos(text(lfos));
    std::lock_guard<std::mutex> lock(v->configLock);
    v->lfo = doc.lfos;
    v->renderer.setLfoConfigs(v->lfo);
}

void geode_viz_set_adsrs(geode_viz* v, const char* adsrs) {
    if (!v) return;
    ModDocument doc;
    doc.readAdsrs(text(adsrs));
    std::lock_guard<std::mutex> lock(v->configLock);
    v->adsr = std::move(doc.adsrs);
    v->renderer.setAdsrConfigs(v->adsr);
}

void geode_viz_set_reactions(geode_viz* v, const char* reactions) {
    if (!v) return;
    // The mask is filled in before each render (syncReactions).
    const ModDocument doc = reactionsOf(reactions, 0u);
    std::lock_guard<std::mutex> lock(v->reactLock);
    v->userHand = doc.hand;
}

void geode_hand_set(unsigned mask, int has_user) { hand::setGlobal(static_cast<std::uint32_t>(mask), has_user != 0); }

int geode_mod_routes_playing(const char* style_id, int seed, const char* reactions, unsigned mask, float* out, int capacity) {
    if (!style_id) return 0;
    return reactionsOf(reactions, mask).playingValues(dnaOf(style_id, seed), style_id, out, capacity);
}

int geode_mod_reactions_edit(const char* style_id, int seed, const char* reactions, unsigned mask, int op, int a, int b,
                             float value, char* out, int capacity, unsigned* out_mask) {
    ModDocument doc = reactionsOf(reactions, mask);
    if (style_id) doc.editReactions(dnaOf(style_id, seed), style_id, op, a, b, value);
    if (out_mask) *out_mask = doc.handMask();
    return geode::api::textOut(doc.reactionsText(), out, capacity);
}

int geode_mod_music_state(const char* style_id, int seed, const char* reactions, unsigned mask, float* out, int capacity) {
    if (!style_id) return 0;
    return reactionsOf(reactions, mask).musicValues(dnaOf(style_id, seed), out, capacity);
}

unsigned geode_mod_recipe_state(int recipe, const char* style_id, int seed, const char* reactions, unsigned mask) {
    if (!style_id) return 0u;
    return reactionsOf(reactions, mask).recipeState(dnaOf(style_id, seed), style_id, recipe);
}

unsigned geode_music_actions_for(const char* style_id) { return style_id ? modtargets::musicActionsFor(style_id) : 0u; }

void geode_director_set(int on, float amount, long long seed) {
    director::setGlobal(on != 0, amount, static_cast<uint64_t>(seed));
}

void geode_director_reach_set(int level) { director::setReach(level); }

void geode_director_keep_set(const char* keys) { director::setKept(text(keys)); }

void geode_note_response_set(int enabled) { note_response::setGlobal(enabled != 0); }

void geode_layer_set(const char* layer) { layer_state::set(layer_state::decode(text(layer))); }

const char* geode_layer_edit(int field, float value, const char* style_id) {
    try {
        if (field < GEODE_LAYER_ON || field > GEODE_LAYER_STYLE) return "";
        if (field == GEODE_LAYER_KNOB) {
            // The page hands back a place among the knobs it was shown; the layer keeps that knob's id.
            const auto knobs = layerKnobs();
            if (!std::isfinite(value) || value < 0.0f || value >= static_cast<float>(knobs.size())) return "";
            value = static_cast<float>(knobs[static_cast<size_t>(value)].first);
        }
        if (!layer_state::change(static_cast<layer_state::Field>(field), value, text(style_id))) return "";
        return layerOut(layer_state::encode(layer_state::get()));
    } catch (...) {
        return "";
    }
}

int geode_layer_values(const char* active_style, int family, float* out, int capacity) {
    if (!out || capacity < GEODE_LAYER_VALUES) return 0;
    try {
        const layer_pick::Editor page = layerPage(active_style, family);
        out[GEODE_LAYER_ON] = page.layer.on ? 1.0f : 0.0f;
        out[GEODE_LAYER_SOURCE] = static_cast<float>(page.layer.source);
        out[GEODE_LAYER_KNOB] = static_cast<float>(page.knob);
        out[GEODE_LAYER_BLEND] = static_cast<float>(page.layer.blend);
        out[GEODE_LAYER_MIX] = page.layer.mix;
        out[GEODE_LAYER_STYLE] = static_cast<float>(page.style);
        out[GEODE_LAYER_FAMILY] = static_cast<float>(page.family);
        out[GEODE_LAYER_PICKED] = page.layer.sceneId.empty() ? 0.0f : 1.0f;
        return GEODE_LAYER_VALUES;
    } catch (...) {
        return 0;
    }
}

const char* geode_layer_list(int list, const char* active_style, int family) {
    try {
        std::string out;
        switch (list) {
            case GEODE_LAYER_LIST_SOURCES:
                for (int i = 0; i < layer_state::kSources; ++i) addLine(out, layer_pick::sourceLabel(static_cast<layer_state::Source>(i)));
                break;
            case GEODE_LAYER_LIST_BLENDS:
                for (int i = 0; i < kBlendModes; ++i) addLine(out, blendModeLabel(static_cast<BlendMode>(i)));
                break;
            case GEODE_LAYER_LIST_FAMILIES: {
                const layer_pick::Editor page = layerPage(active_style, family);
                for (const std::string& name : page.families) addLine(out, name);
                break;
            }
            case GEODE_LAYER_LIST_STYLES: {
                const layer_pick::Editor page = layerPage(active_style, family);
                for (const std::string& id : page.styles) addLine(out, id);
                break;
            }
            case GEODE_LAYER_LIST_KNOBS: {
                const auto knobs = layerKnobs();
                for (const auto& knob : knobs) addLine(out, knob.second);
                break;
            }
            default:
                break;
        }
        return layerOut(std::move(out));
    } catch (...) {
        return "";
    }
}

const char* geode_layer_shown(const char* active_style) {
    try {
        return layerOut(layerPage(active_style, -1).shown);
    } catch (...) {
        return "";
    }
}

void geode_viz_gyro_push(geode_viz* v, float x, float y, float z, float w, int display_rotation) {
    if (v) v->live.gyroPush(frame::GyroReading{x, y, z, w, display_rotation});
}

void geode_viz_gyro_off(geode_viz* v) {
    if (v) v->live.gyroOff();
}

void geode_viz_request_scene(geode_viz* v, const char* scene_id) {
    if (!v || !scene_id) return;
    try {
        v->live.requestScene(scene_id);
    } catch (...) {
        // No memory for the id: the scene drawn stays until the next request.
    }
}

void geode_viz_set_look(geode_viz* v, const geode_viz_look* look) {
    if (!v || !look) return;
    try {
        LookSnapshot next;
        next.sceneId = text(look->scene_id);
        if (look->params && look->param_count > 0) next.params.assign(look->params, look->params + look->param_count);
        next.lfos = text(look->lfos);
        next.adsrs = text(look->adsrs);
        if (look->tuning && look->tuning_count > 0) next.tuning.assign(look->tuning, look->tuning + look->tuning_count);
        next.hasReactions = look->reactions != nullptr;
        next.reactions = text(look->reactions);
        next.reducedMotion = look->reduced_motion != 0;
        next.transitionId = text(look->transition_id);
        next.transitionMs = look->transition_ms;
        std::lock_guard<std::mutex> one(v->lookLock);
        const uint32_t parts = v->shellLook.take(std::move(next));
        const LookSnapshot& l = v->shellLook.look();
        if ((parts & kLookScene) != 0) geode_viz_request_scene(v, l.sceneId.c_str());
        if ((parts & kLookParams) != 0 && !l.params.empty()) geode_viz_set_params(v, l.params.data(), static_cast<int>(l.params.size()));
        if ((parts & kLookLfos) != 0) geode_viz_set_lfos(v, l.lfos.c_str());
        if ((parts & kLookAdsrs) != 0) geode_viz_set_adsrs(v, l.adsrs.c_str());
        if ((parts & kLookTuning) != 0) {
            geode_viz_set_sound_tuning(v, l.tuning.empty() ? nullptr : l.tuning.data(), static_cast<int>(l.tuning.size()));
        }
        if ((parts & kLookReactions) != 0) geode_viz_set_reactions(v, l.hasReactions ? l.reactions.c_str() : nullptr);
        if ((parts & kLookReducedMotion) != 0) geode_viz_set_reduced_motion(v, l.reducedMotion ? 1 : 0);
        if ((parts & kLookTransition) != 0) geode_viz_set_transition(v, l.transitionId.c_str(), l.transitionMs);
    } catch (...) {
        // No memory for the look: the renderer keeps the one it has.
    }
}

const char* geode_viz_look_defaults(void) {
    static const std::string defaults = lookDefaultsText();
    return defaults.c_str();
}

void geode_viz_set_quality(geode_viz* v, float render_scale, float march_scale, int post_fx, int adaptive,
                           int liquid_hot_policy) {
    if (v) v->live.setQuality(frame::UserQuality{render_scale, march_scale, post_fx != 0, adaptive != 0, liquid_hot_policy});
}

int geode_viz_frame(geode_viz* v, const geode_pcm_ring* ring, float paced_fps, float cap_fps) {
    if (!v) return 0;
    try {
        VizStages stages(*v, ring);
        return static_cast<int>(v->live.run(stages, paced_fps, cap_fps));
    } catch (...) {
        // Nothing to tell the shell this frame; the next one starts over.
        return 0;
    }
}

int geode_viz_liquid_mode(geode_viz* v) {
    return v ? v->live.liquidMode() : 0;
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
    v->live.surfaceCreated();
    // A new surface starts its engine on a full window, not the tail of the last: the next frame's cursor is a
    // new one.
    geode_viz_feed_ring(v, nullptr);
    // The platform's status listener, once for the process (the first surface to come up).
    geode::viz::thermal_source::attach();
}

void geode_viz_surface_changed(geode_viz* v, int width, int height) {
    if (v) v->renderer.surfaceChanged(std::max(width, 1), std::max(height, 1));
}

int geode_viz_set_scene(geode_viz* v, const char* scene_id) {
    if (!v || !scene_id) return 0;
    {
        std::lock_guard<std::mutex> lock(v->reactLock);
        v->reactions.setScene(geode::viz::catalogue::resolveId(scene_id));
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
    v->recordPtsNs = v->recorder
        ? v->recorder->frame(geode_record_now_ns(), v->renderer.width(), v->renderer.height(),
                             static_cast<int>(geode::viz::ThermalState::process().tier()))
        : -1;
    v->renderer.render(time_seconds, target_fbo);
    const GeodeSoundFrame& heard = v->renderer.soundFrame();
    std::lock_guard<std::mutex> lock(v->tapLock);
    v->tapAlong.frame(static_cast<double>(geode_record_now_ns()) * 1e-9, heard.beatPhase, heard.bpm, heard.beatConf,
                      heard.presence);
}

void geode_viz_still_request(geode_viz* v) {
    if (v) v->stillWanted.store(true);
}

int geode_viz_still_size(geode_viz* v, int* width, int* height) {
    if (!v || v->still.empty()) return 0;
    if (width) *width = v->stillWidth;
    if (height) *height = v->stillHeight;
    return 1;
}

int geode_viz_still_read(geode_viz* v, void* out, size_t capacity) {
    if (!v || !out || v->still.empty() || capacity < v->still.size()) return 0;
    std::memcpy(out, v->still.data(), v->still.size());
    std::vector<uint8_t>().swap(v->still);
    return 1;
}

int geode_viz_tap_along(geode_viz* v, int64_t uptime_ns) {
    if (!v) return 0;
    std::lock_guard<std::mutex> lock(v->tapLock);
    return v->tapAlong.tap(static_cast<double>(uptime_ns) * 1e-9) ? 1 : 0;
}

int geode_viz_record_start(geode_viz* v, const geode_recorder* recorder, void* native_window, int width, int height,
                           geode_export_pass* watermark) {
    if (!v || !recorder || !recorder->recorder) return 0;
    if (!v->recordTap.start(native_window, width, height)) return 0;
    v->recorder = recorder->recorder;
    v->recordWatermark = watermark;
    v->recordPtsNs = -1;
    return 1;
}

void geode_viz_set_cast_source(geode_viz* v, int on) {
    if (v) v->castSource.store(on != 0, std::memory_order_relaxed);
}

void geode_viz_record_stop(geode_viz* v) {
    if (!v) return;
    v->recordTap.stop();
    v->recorder.reset();
    v->recordWatermark = nullptr;
    v->recordPtsNs = -1;
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
