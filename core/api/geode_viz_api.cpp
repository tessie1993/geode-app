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
#include "api/geode_responder_api.h"
#include "api/geode_text_out.hpp"
#include "api/geode_record_handles.hpp"
#include "api/geode_viz_internal.hpp"
#include "sound/Tuning.hpp"
#include "sound/hop/SharedAnalysis.hpp"
#include "util/WorkHint.hpp"
#include "viz/styles/catalogue/Catalogue.hpp"
#include "audio/ingest/LayerMix.hpp"
#include "viz/director/AutoDraw.hpp"
#include "viz/director/AutoPlan.hpp"
#include "viz/director/Director.hpp"
#include "viz/director/DirectorReach.hpp"
#include "viz/director/DirectorStyle.hpp"
#include "viz/input/GyroLook.hpp"
#include "viz/input/TapAlong.hpp"
#include "viz/LayerPick.hpp"
#include "viz/LayerState.hpp"
#include "viz/looks/LookSnapshot.hpp"
#include "viz/modulation/MacroBank.hpp"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ModState.hpp"
#include "viz/modulation/ModTargets.hpp"
#include "viz/modulation/ReactionDna.hpp"
#include "viz/modulation/ReactionResolve.hpp"
#include "viz/modulation/ReactionText.hpp"
#include "viz/modulation/Responder.hpp"
#include "viz/Renderer.hpp"
#include "viz/styles/catalogue/SceneRegistry.hpp"
#include "viz/SceneResidency.hpp"
#include "viz/glsl/ShaderSource.hpp"
#include "viz/passes/Transition.hpp"
#include "viz/director/TransitionChoice.hpp"
#include "viz/passes/TransitionLibrary.hpp"
#include "viz/modulation/VisualSafety.hpp"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/LiveFrame.hpp"
#include "viz/frame/PresentTime.hpp"
#include "viz/frame/RecordTap.hpp"
#include "viz/frame/CastTee.hpp"
#include "viz/glsl/ShaderOverrides.hpp"
#include "viz/gl/StyleQuarantine.hpp"
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
        std::vector<uint8_t> pixels(row * static_cast<size_t>(height), 0);
        GLint previous = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous));
        std::vector<uint8_t> swap(row);
        for (int y = 0; y < height / 2; ++y) {
            uint8_t* a = pixels.data() + row * static_cast<size_t>(y);
            uint8_t* b = pixels.data() + row * static_cast<size_t>(height - 1 - y);
            std::memcpy(swap.data(), a, row);
            std::memcpy(a, b, row);
            std::memcpy(b, swap.data(), row);
        }
        {
            std::lock_guard<std::mutex> lock(stillLock);
            still = std::move(pixels);
            stillWidth = width;
            stillHeight = height;
        }
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
    // The time the last live frame was drawn at (seconds, CLOCK_MONOTONIC), so the next never runs back. GL thread only.
    double liveSeconds = 0.0;
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
    // When the display is expected to show the frame being drawn (CLOCK_MONOTONIC ns, 0: none), set by the live
    // frame before it feeds the ring. Render thread only.
    int64_t presentNs = 0;
    // The frame is for a stage that may leave the last buffer on screen (vizFrameAt's mayRepeat); the legacy
    // geode_viz_frame never sets it. Render thread only.
    bool mayRepeat = false;
    // The live feed (geode_viz_feed_ring): the ring this renderer hears and its place in it. The cursor is
    // declared after the ring it refers to. Render thread only.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedRing;
    std::unique_ptr<geode::audio::ingest::Cursor> feedCursor;
    // The ring layered over it (geode_pcm_ring_set_layer) and this renderer's mix of the two.
    std::shared_ptr<const geode::audio::ingest::PcmRing> feedLayerRing;
    std::unique_ptr<geode::audio::ingest::LayerMix> feedLayer;
    // This renderer listening to the ring's shared analysis; none while the mic is layered in, which only this
    // renderer hears (Renderer::writeAudio analyses it on its own thread). Declared after feedRing.
    geode::sound::SharedAnalysis::Listening feedListening;
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
    // The still and its size: written by the render thread, read by the shell's thread (stillLock).
    std::mutex stillLock;
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
static_assert(frame::kEventLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(frame::kEventErrorChanged == GEODE_FRAME_ERROR_CHANGED);
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

// The picture's clock: CLOCK_MONOTONIC, the clock the vsync's frame time and expected presentation time are on.
double monotonicSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
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

// The ring's shared analysis, made on the first ask; null without memory for it.
std::shared_ptr<geode::sound::SharedAnalysis> ringAnalysis(const geode_pcm_ring& ring) {
    std::lock_guard<std::mutex> lock(ring.analysisLock);
    if (!ring.analysis) {
        try {
            geode::sound::AnalysisThread::setWorkReporter(&geode::util::reportThreadWork);
            ring.analysis = std::make_shared<geode::sound::SharedAnalysis>(ring.ring);
        } catch (...) {
            return nullptr;
        }
    }
    return ring.analysis;
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
    // The tee outlives this renderer while a cast runs: its writer names go with this stage's context.
    if (owned && owned->castTee) owned->castTee->forgetWriter();
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
        v->renderer.hearAnalysis(nullptr);
        v->feedListening = {};
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
    // Read where the viewer sees the frame, not where it starts drawing: a frame waits in the swap queue.
    const int64_t limit = geode_playout_listen_limit(playout, geode::viz::frame::listenAtNs(nowNs, v->presentNs));
    // The ring is analysed once, on its own thread, ahead of the speaker, for every renderer hearing it; this
    // frame reads it at the speaker's sample. The mic layered over it is heard by this renderer alone.
    const bool shared = v->feedLayer == nullptr;
    const auto& listening = v->feedListening.shared();
    if (shared ? !(listening && listening->hears(*v->feedRing)) : listening != nullptr) {
        geode::sound::SharedAnalysis::Listening next(shared ? ringAnalysis(*ring) : nullptr);
        v->renderer.hearAnalysis(next.shared());
        v->feedListening = std::move(next);
    }
    v->renderer.setListenPosition(limit);
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

void geode_style_quarantine_deaths(const int* pids, const int64_t* times_ms, const int* verdicts, int count) {
    using geode::viz::StyleDeaths;
    std::vector<StyleDeaths::Death> deaths;
    for (int i = 0; pids && times_ms && verdicts && i < count; ++i) {
        const auto verdict = verdicts[i] == GEODE_DEATH_CRASH   ? StyleDeaths::Verdict::Crash
                             : verdicts[i] == GEODE_DEATH_OTHER ? StyleDeaths::Verdict::Other
                                                                : StyleDeaths::Verdict::Unknown;
        deaths.push_back({pids[i], static_cast<long long>(times_ms[i]), verdict});
    }
    StyleDeaths::process().tell(std::move(deaths));
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
    // Every rate, then Follow device.
    static_assert(geode::viz::frame::kLiveFpsMaxRates + 1 == GEODE_LIVE_FPS_MAX_CHOICES);
    return geode::viz::frame::liveFpsChoices(rates, std::max(count, 0), out, std::max(capacity, 0));
}

int geode_live_fps_cap(int stored_fps, const float* rates, int count) {
    return geode::viz::frame::liveFpsCap(stored_fps, rates, std::max(count, 0));
}

float geode_live_fps_refresh(int fps, const float* rates, int count) {
    return geode::viz::frame::refreshRateFor(fps, rates, std::max(count, 0));
}

void geode_memory_trim(int android_level) {
    const auto trim = geode::viz::SceneResidency::trimForAndroidLevel(android_level);
    geode::viz::SceneResidency::trimSignal().request(trim);
    // The signal is read inside a frame; a stage that draws none is told on its own thread.
    if (trim != geode::viz::MemoryTrim::None) geode::api::trimStages();
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

// The Event Responder's rows and responses in a saved reactions text. No text (or one saved before they existed) has
// none; the page's edits start a new style's from geode_resp_default_text.
responder::Config responderConfigOf(const char* reactions) {
    responder::Config config;
    if (reactions) reactiontext::decodeResponses(reactions, config);
    return config;
}

// The style's routes of the matrix that sit on `row`, as listed responses (GEODE_RESP_ORIGIN_STYLE), appended to `out`.
int appendStyleDefaults(const responder::Config& config, const RouteArray& playing, int count, int row, std::vector<float>& out) {
    int added = 0;
    for (int i = 0; i < count; ++i) {
        const ReactRoute& r = playing[static_cast<size_t>(i)];
        if (!r.styleDefault || responder::rowOfSource(r.source) != row) continue;
        responder::Response d;
        d.enabled = r.enabled && !responder::isMuted(config, static_cast<int>(r.source), static_cast<int>(r.target));
        d.row = row;
        d.target = r.target;
        d.amount = r.amount;
        d.strength = r.curve;
        d.fadeIn = r.attackSeconds;
        d.hold = 0.0f;
        d.fadeOut = r.releaseSeconds;
        d.drive = r.mode == ReactMode::Follow ? responder::DriveMode::Follow : responder::DriveMode::Once;
        const size_t at = out.size();
        out.resize(at + static_cast<size_t>(responder::kListValues));
        responder::fieldValues(d, out.data() + at);
        out[at + static_cast<size_t>(responder::kFieldCount)] = static_cast<float>(static_cast<int>(responder::Origin::StyleDefault));
        out[at + static_cast<size_t>(responder::kFieldCount) + 1] = static_cast<float>(i);
        out[at + static_cast<size_t>(responder::kFieldCount) + 2] = static_cast<float>(static_cast<int>(r.source));
        out[at + static_cast<size_t>(responder::kFieldCount) + 3] = static_cast<float>(static_cast<int>(r.target));
        ++added;
    }
    return added;
}

int copyFloats(const std::vector<float>& values, float* out, int capacity) {
    const int n = out ? std::min(static_cast<int>(values.size()), std::max(capacity, 0)) : 0;
    for (int i = 0; i < n; ++i) out[i] = values[static_cast<size_t>(i)];
    return n;
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
    VizStages(geode_viz& v, const geode_pcm_ring* ring, int64_t vsyncNs, int64_t presentNs, bool mayRepeat)
        : v_(v), ring_(ring), vsyncNs_(vsyncNs) {
        v_.presentNs = presentNs;
        v_.mayRepeat = mayRepeat;
    }

    void setPacedFps(float pacedFps, float capFps) override { v_.renderer.setPacedFps(pacedFps, capFps); }
    void applyQuality(const frame::UserQuality& q) override {
        v_.renderer.setUserQuality(q.renderScale, q.marchScale, q.postFx);
    }
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
    void render() override { geode_viz_render(&v_, frameSeconds(), 0); }
    std::string lastError() override { return v_.renderer.lastError(); }
    bool directorMomentsPending() override { return v_.renderer.directorStyleMomentsPending(); }
    bool stillReady() override { return std::exchange(v_.stillFresh, false); }
    bool frameRepeated() override { return v_.renderer.frameRepeated(); }

private:
    // The frame's time: when the display is expected to show it (the vsync's expected presentation), so the picture's
    // step is the presentations' own spacing and not the thread hop's jitter. Without one, the vsync it was asked for
    // at, and without that (the cast's fallback) the clock at the draw. Never earlier than the last frame's.
    double frameSeconds() {
        double seconds = monotonicSeconds();
        if (v_.presentNs > 0) {
            seconds = static_cast<double>(v_.presentNs) * 1e-9;
        } else if (vsyncNs_ > 0) {
            seconds = static_cast<double>(vsyncNs_) * 1e-9;
        }
        seconds = std::max(seconds, v_.liveSeconds);
        v_.liveSeconds = seconds;
        return seconds;
    }

    geode_viz& v_;
    const geode_pcm_ring* ring_;
    int64_t vsyncNs_;
};

static_assert(kPageMoments == GEODE_MUSIC_MOMENTS);
static_assert(kMusicActionCount == GEODE_MUSIC_ACTIONS);
static_assert(hand::kAll == GEODE_HAND_ALL && hand::kUnclaimed == GEODE_HAND_UNCLAIMED);

}  // namespace

// The two below are defined by qualified name, so each must match its declaration in api/geode_viz_internal.hpp:
// a drift is a compile error here, not a new overload that geode_stage_api.cpp fails to link against.

// geode_viz_frame for a frame a vsync asked for (declared in api/geode_viz_internal.hpp): vsyncNs, CLOCK_MONOTONIC,
// is the vsync's frame time, the picture's clock for this frame; 0 draws at the clock now. presentNs, CLOCK_MONOTONIC, is when
// the display is expected to show it (0: none); the sound is read there. Not part of the C ABI.
int geode::api::vizFrameAt(geode_viz* v, const geode_pcm_ring* ring, float pacedFps, float capFps, int64_t vsyncNs, int64_t presentNs, bool mayRepeat) {
    if (!v) return 0;
    try {
        VizStages stages(*v, ring, vsyncNs, presentNs, mayRepeat);
        return static_cast<int>(v->live.run(stages, pacedFps, capFps));
    } catch (...) {
        // Nothing to tell the shell this frame; the next one starts over.
        return 0;
    }
}

void geode::api::vizRestartClock(geode_viz* v) {
    if (v) v->renderer.restartClock();
}

// geode_viz_still_read without the copy (declared in api/geode_viz_internal.hpp): the still's buffer itself is
// handed over. False, nothing touched, when none is ready. Render thread. Not part of the C ABI.
bool geode::api::takeVizStill(geode_viz* v, int& width, int& height, std::vector<uint8_t>& rgba) {
    if (!v) return false;
    std::lock_guard<std::mutex> lock(v->stillLock);
    if (v->still.empty()) return false;
    width = v->stillWidth;
    height = v->stillHeight;
    rgba = std::move(v->still);
    v->still.clear();
    return true;
}

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
    {
        std::lock_guard<std::mutex> lock(v->reactLock);
        v->userHand = doc.hand;
    }
    // The Event Responder's rows and responses ride in the same text; the renderer latches them for its next frame.
    v->renderer.setResponses(responderConfigOf(reactions));
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
    const std::string routes = doc.reactionsText();
    // No routes of the user's own: every moment is the style's DNA again.
    if (out_mask) *out_mask = routes.empty() ? 0u : doc.handMask();
    return geode::api::textOut(routes, out, capacity);
}

void geode_mod_state_reactions_edit(const char* style_id, int seed, int op, int a, int b, float value) try {
    if (!style_id) return;
    const StyleDna dna = dnaOf(style_id, seed);
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.editReactions(dna, style_id, op, a, b, value);
    const modstate::Hand held = s.state.hand();
    hand::setGlobal(held.mask, held.user);
} catch (...) {
}

int geode_mod_music_state(const char* style_id, int seed, const char* reactions, unsigned mask, float* out, int capacity) {
    if (!style_id) return 0;
    return reactionsOf(reactions, mask).musicValues(dnaOf(style_id, seed), out, capacity);
}

unsigned geode_mod_recipe_state(int recipe, const char* style_id, int seed, const char* reactions, unsigned mask) {
    if (!style_id) return 0u;
    return reactionsOf(reactions, mask).recipeState(dnaOf(style_id, seed), style_id, recipe);
}

int geode_resp_rows(const char* reactions, const char* style_id, int seed, unsigned mask, float* out, int capacity) try {
    if (!style_id) return 0;
    const ModDocument doc = reactionsOf(reactions, mask);
    const responder::Config config = reactions ? responderConfigOf(reactions) : responder::newStyleConfig();
    RouteArray playing{};
    const int count = mergeHand(doc.hand, dnaOf(style_id, seed), playing);
    std::vector<float> values(static_cast<size_t>(responder::kRows * GEODE_RESP_ROW_VALUES));
    for (int row = 0; row < responder::kRows; ++row) {
        float* o = values.data() + row * GEODE_RESP_ROW_VALUES;
        std::vector<float> styled;
        o[0] = config.rowOn[static_cast<size_t>(row)] ? 1.0f : 0.0f;
        o[1] = static_cast<float>(responder::rowCount(config, row));
        o[2] = static_cast<float>(appendStyleDefaults(config, playing, count, row, styled));
        int muted = 0;
        for (size_t at = 0; at < styled.size(); at += static_cast<size_t>(responder::kListValues)) muted += styled[at] > 0.5f ? 0 : 1;
        o[3] = static_cast<float>(muted);
    }
    return copyFloats(values, out, capacity);
} catch (...) {
    return 0;
}

int geode_resp_values(const char* reactions, const char* style_id, int seed, unsigned mask, int row, float* out,
                      int capacity) try {
    if (!style_id || row < 0 || row >= responder::kRows) return 0;
    const ModDocument doc = reactionsOf(reactions, mask);
    const responder::Config config = reactions ? responderConfigOf(reactions) : responder::newStyleConfig();
    RouteArray playing{};
    const int count = mergeHand(doc.hand, dnaOf(style_id, seed), playing);
    std::vector<float> values;
    for (int i = 0; i < config.count; ++i) {
        const responder::Response& r = config.responses[static_cast<size_t>(i)];
        if (r.row != row) continue;
        const size_t at = values.size();
        values.resize(at + static_cast<size_t>(responder::kListValues));
        responder::fieldValues(r, values.data() + at);
        values[at + static_cast<size_t>(responder::kFieldCount)] = static_cast<float>(static_cast<int>(responder::Origin::User));
        values[at + static_cast<size_t>(responder::kFieldCount) + 1] = -1.0f;
        values[at + static_cast<size_t>(responder::kFieldCount) + 2] = -1.0f;
        values[at + static_cast<size_t>(responder::kFieldCount) + 3] = -1.0f;
    }
    appendStyleDefaults(config, playing, count, row, values);
    return copyFloats(values, out, capacity);
} catch (...) {
    return 0;
}

void geode_viz_response_preview(geode_viz* v, const float* values, int count) try {
    if (!v || !values || count < responder::kFieldCount) return;
    // The fields go through the edit's own checks, so a card's values land as the page's edits would.
    responder::Config card;
    responder::addResponse(card, responder::Response{});
    constexpr int kRowField = static_cast<int>(responder::Field::Row);
    responder::setField(card, 0, 0, kRowField, values[kRowField]);
    const int row = card.responses[0].row;
    for (int field = 0; field < responder::kFieldCount; ++field) {
        if (field != kRowField) responder::setField(card, row, 0, field, values[field]);
    }
    v->renderer.previewResponse(card.responses[0]);
} catch (...) {
}

int geode_viz_responder_status(geode_viz* v, float* out, int capacity) try {
    if (!v || !out) return 0;
    const responder::Status s = v->renderer.responderStatus();
    std::vector<float> values(static_cast<size_t>(GEODE_RESP_STATUS_VALUES));
    for (int row = 0; row < responder::kRows; ++row) values[static_cast<size_t>(row)] = s.lit[static_cast<size_t>(row)];
    values[GEODE_RESP_STATUS_COST] = static_cast<float>(s.cost);
    values[GEODE_RESP_STATUS_BUDGET] = static_cast<float>(s.budget);
    values[GEODE_RESP_STATUS_LEVEL] = static_cast<float>(s.level);
    values[GEODE_RESP_STATUS_ACTIVE] = static_cast<float>(s.active);
    values[GEODE_RESP_STATUS_DROPPED] = static_cast<float>(s.dropped);
    values[GEODE_RESP_STATUS_ROUTES] = static_cast<float>(s.enabledRoutes);
    return copyFloats(values, out, capacity);
} catch (...) {
    return 0;
}

unsigned geode_music_actions_for(const char* style_id) { return style_id ? modtargets::musicActionsFor(style_id) : 0u; }

void geode_director_set(int on, float amount, long long seed) {
    director::setGlobal(on != 0, amount, static_cast<uint64_t>(seed));
}

float geode_director_live_amount(int on, int entitled, long long position_ms, int playing) {
    return director::liveAmount(on != 0, entitled != 0, position_ms, playing != 0);
}

void geode_director_reach_set(int level) { director::setReach(level); }

void geode_director_keep_set(const char* keys) { director::setKept(text(keys)); }

void geode_director_tuning_set(const char* tuning) { director::setTuning(text(tuning)); }

int geode_director_tuning_count(void) { return director::kTunes; }

const char* geode_director_tuning_label(int index) {
    return index >= 0 && index < director::kTunes ? director::tuneSpecs()[static_cast<size_t>(index)].label : "";
}

const char* geode_director_tuning_help(int index) {
    return index >= 0 && index < director::kTunes ? director::tuneSpecs()[static_cast<size_t>(index)].help : "";
}

int geode_director_tuning_range(int index, float* out4) {
    if (index < 0 || index >= director::kTunes || !out4) return 0;
    const director::TuneSpec& s = director::tuneSpecs()[static_cast<size_t>(index)];
    out4[0] = s.min;
    out4[1] = s.max;
    out4[2] = s.step;
    out4[3] = director::isSwitch(s) ? 1.0f : 0.0f;
    return 1;
}

float geode_director_tuning_value(const char* tuning, int index) {
    if (index < 0 || index >= director::kTunes) return 0.0f;
    return director::parseTuning(text(tuning)).v[static_cast<size_t>(index)];
}

const char* geode_director_tuning_value_text(const char* tuning, int index) {
    return layerOut(director::valueText(director::parseTuning(text(tuning)), index));
}

const char* geode_director_tuning_with(const char* tuning, int index, float value) {
    return layerOut(director::tuningWith(text(tuning), index, value));
}

int geode_auto_pick_among(const char* ids, int on_screen, int director_moment) {
    const std::string pool = text(ids);
    if (director_moment != 0) {
        const int pick = director::pickStyle(pool, on_screen);
        if (pick != director::kNoIntent) return pick;
    }
    const int count = pool.empty() ? 0 : static_cast<int>(std::count(pool.begin(), pool.end(), '\n')) + 1;
    return auto_draw::pick(count, on_screen);
}

const char* geode_auto_draw(const char* locked_styles, const char* current_style, const char* preset_styles,
                            int preset_on_screen, int sources,
                            long long now_ms, int* out2) {
    thread_local std::string picked;
    auto_timer::restart(auto_timer::kRandom, now_ms);
    const auto lines = [](const char* s) {
        std::vector<std::string> out;
        const std::string all = text(s);
        if (all.empty()) return out;
        size_t at = 0;
        for (size_t nl; (nl = all.find('\n', at)) != std::string::npos; at = nl + 1) out.push_back(all.substr(at, nl - at));
        if (at < all.size()) out.push_back(all.substr(at));
        return out;
    };
    const auto locked = lines(locked_styles);
    const auto pool = auto_plan::pool(lines(geode_catalogue_style_ids(GEODE_STYLE_AUTO_DRAW)),
                                      std::set<std::string>(locked.begin(), locked.end()), text(current_style),
                                      lines(preset_styles), preset_on_screen, sources);
    const int pick = geode_auto_pick_among(pool.ids.c_str(), pool.onScreen, (sources & GEODE_AUTO_DRAW_KEEP_LOOK) ? 1 : 0);
    if (pick < 0 || pick >= static_cast<int>(pool.entries.size())) return nullptr;
    const auto [kind, index] = pool.entries[static_cast<size_t>(pick)];
    if (out2) {
        out2[0] = kind;
        out2[1] = index;
    }
    const std::string_view ids = pool.ids;
    size_t at = 0;
    for (int i = 0; i < pick; ++i) at = ids.find('\n', at) + 1;
    picked = std::string(ids.substr(at, ids.find('\n', at) - at));
    return picked.c_str();
}

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
        next.transitionMs = clampTransitionMs(look->transition_ms);
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

void geode_viz_set_quality(geode_viz* v, float render_scale, float march_scale, int post_fx) {
    if (v) v->live.setQuality(frame::UserQuality{render_scale, march_scale, post_fx != 0});
}


int geode_viz_frame(geode_viz* v, const geode_pcm_ring* ring, float paced_fps, float cap_fps) {
    return geode::api::vizFrameAt(v, ring, paced_fps, cap_fps, 0);
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
    // The tee's writer names belong to the context that just went; a reused handle value must not revive them.
    if (v->castTee) v->castTee->forgetWriter();
    v->live.surfaceCreated();
    // A new surface starts its engine on a full window, not the tail of the last: the next frame's cursor is a
    // new one.
    geode_viz_feed_ring(v, nullptr);
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
                                  int count, const char* const* cut_ids, int cut_count,
                                  int every_beats, const char* const* locked_ids, int locked_count) {
    if (!v) return;
    std::vector<geode::viz::StyleCue> cues;
    for (int i = 0; at_ms && scene_ids && i < count; ++i) {
        if (!scene_ids[i]) continue;
        geode::viz::StyleCue cue;
        cue.atMs = at_ms[i];
        cue.sceneId = scene_ids[i];
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
        ? v->recorder->frame(geode_record_now_ns(), v->renderer.width(), v->renderer.height())
        : -1;
    // A recording, the cast tee and a still each take every frame from the composite: they get a frame each.
    v->renderer.setRepeatAllowed(v->mayRepeat && !v->recorder && !v->castSource.load(std::memory_order_relaxed) &&
                                 !v->stillWanted.load(std::memory_order_relaxed));
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
    if (!v) return 0;
    std::lock_guard<std::mutex> lock(v->stillLock);
    if (v->still.empty()) return 0;
    if (width) *width = v->stillWidth;
    if (height) *height = v->stillHeight;
    return 1;
}

int geode_viz_still_read(geode_viz* v, void* out, size_t capacity) {
    if (!v || !out) return 0;
    std::lock_guard<std::mutex> lock(v->stillLock);
    if (v->still.empty() || capacity < v->still.size()) return 0;
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
    if (!v) return;
    v->renderer.pauseQuarantine();
    // Without a current context (the surface already gone) every GL delete is a no-op: the scenes would be
    // forgotten while their objects stayed in the kept context. Then nothing is shed here.
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) return;
    v->renderer.trimSpares();
}

}
