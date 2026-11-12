#include "api/geode_stage_api.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stage/LiveClient.hpp"
#include "stage/StageLifecycle.hpp"
#include "stage/android/StageThread.hpp"
#include "util/Log.hpp"
#include "viz/SurfaceRun.hpp"

namespace stage = geode::stage;
using geode::stage::platform::StageThread;

// The header's numbers are the lifecycle's; a drift here would make the shell read one bit and mean another.
static_assert(stage::kViewAttached == GEODE_STAGE_VIEW_ATTACHED && stage::kViewWindowVisible == GEODE_STAGE_VIEW_WINDOW_VISIBLE);
static_assert(stage::kViewShown == GEODE_STAGE_VIEW_SHOWN && stage::kViewOpaque == GEODE_STAGE_VIEW_OPAQUE);
static_assert(stage::kStateWindow == GEODE_STAGE_STATE_WINDOW && stage::kStateContext == GEODE_STAGE_STATE_CONTEXT);
static_assert(stage::kStateSurface == GEODE_STAGE_STATE_SURFACE && stage::kStateDrawable == GEODE_STAGE_STATE_DRAWABLE);
static_assert(stage::kStateVsync == GEODE_STAGE_STATE_VSYNC && stage::kStatePaused == GEODE_STAGE_STATE_PAUSED);
static_assert(stage::kStateRefused == GEODE_STAGE_STATE_REFUSED && stage::kStateSurfaceBad == GEODE_STAGE_STATE_SURFACE_BAD);
static_assert(stage::kStateReleased == GEODE_STAGE_STATE_RELEASED && stage::kStateModeShift == GEODE_STAGE_STATE_MODE_SHIFT);
static_assert(static_cast<int>(stage::Kind::View) == GEODE_STAGE_KIND_VIEW && static_cast<int>(stage::Kind::Surface) == GEODE_STAGE_KIND_SURFACE);
static_assert(stage::kFrameClearOnly == GEODE_FRAME_CLEAR_ONLY && stage::kFrameLiquidChanged == GEODE_FRAME_LIQUID_CHANGED);
static_assert(stage::kFrameErrorChanged == GEODE_FRAME_ERROR_CHANGED && stage::kFrameMilkLoaded == GEODE_FRAME_MILK_LOADED);
static_assert(stage::kFrameDirectorMoment == GEODE_FRAME_DIRECTOR_MOMENT && stage::kFrameStillReady == GEODE_FRAME_STILL_READY);

namespace {

constexpr const char* kTag = "GeodeStage";

// The C client as a StageClient: plain function pointers, no exception can come out of them.
class CClient final : public stage::StageClient {
public:
    explicit CClient(const geode_stage_client& c) : c_(c) {}
    void surfaceCreated() override { c_.surface_created(c_.user); }
    void surfaceChanged(int width, int height) override { c_.surface_changed(c_.user, width, height); }
    uint32_t frame(float pacedFps, float capFps) override { return c_.frame(c_.user, pacedFps, capFps); }
    void trimSpares() override { c_.trim_spares(c_.user); }
    void contextLost() override { c_.context_lost(c_.user); }
    void release(bool glCurrent) override { c_.release(c_.user, glCurrent ? 1 : 0); }

private:
    geode_stage_client c_;
};

bool complete(const geode_stage_client* c) {
    return c && c->surface_created && c->surface_changed && c->frame && c->trim_spares && c->context_lost && c->release;
}

// A live stage's renderer: the geode_viz it owns, and the mark of the tee open on it.
class Viz final : public stage::LiveViz {
public:
    Viz(geode_viz* viz, AAssetManager* assets) : v_(viz), assets_(assets) {}
    ~Viz() override {
        if (mark_) geode_export_pass_destroy(mark_);
        geode_viz_destroy(v_);
    }
    void surfaceCreated() override { geode_viz_surface_created(v_); }
    void surfaceChanged(int width, int height) override { geode_viz_surface_changed(v_, width, height); }
    uint32_t frame(const void* ring, float pacedFps, float capFps) override {
        return static_cast<uint32_t>(geode_viz_frame(v_, static_cast<const geode_pcm_ring*>(ring), pacedFps, capFps));
    }
    void trimSpares() override { geode_viz_trim_spares(v_); }
    void releaseScenes() override { geode_viz_release_scenes(v_); }
    void warmTransition(const std::string& id) override { geode_viz_warm_transition(v_, id.c_str()); }
    bool recordStart(const stage::RecordAsk& ask) override {
        dropMark();
        // A recording that owes a mark never rolls without it.
        if (ask.placement != 0) {
            if (ask.tile.empty()) return false;
            mark_ = geode_export_watermark_create(assets_, ask.tile.data(), ask.tileWidth, ask.tileHeight, ask.placement,
                                                  ask.corner, 0);
            const char* problem = geode_export_pass_error(mark_);
            if (!mark_ || (problem && problem[0] != '\0')) {
                dropMark();
                return false;
            }
        }
        return geode_record_session_tee_start(static_cast<geode_record_session*>(ask.session), v_, mark_) != 0;
    }
    void recordFailed(const stage::RecordAsk& ask) override {
        geode_recorder_stop(static_cast<geode_recorder*>(ask.recorder), GEODE_RECORD_STOP_FAILED, geode_record_now_ns());
    }
    void recordStop(const stage::RecordAsk& ask) override {
        geode_viz_record_stop(v_);
        dropMark();
        geode_record_session_tee_closed(static_cast<geode_record_session*>(ask.session));
    }
    std::string error() override {
        const char* text = geode_viz_last_error(v_);
        return text ? text : "";
    }
    std::string takeMilkLoaded() override {
        // One call: the take hands the path out once. A path longer than PATH_MAX is no file the renderer loaded.
        char path[4096];
        const size_t length = geode_viz_take_milk_preset_loaded(v_, path, sizeof(path));
        return length > 0 && length < sizeof(path) ? std::string(path, length) : std::string();
    }
    int takeDirectorMoments() override { return geode_viz_take_director_style_moments(v_); }
    int liquidMode() override { return geode_viz_liquid_mode(v_); }
    bool readStill(stage::Still& out) override {
        int width = 0, height = 0;
        if (!geode_viz_still_size(v_, &width, &height) || width <= 0 || height <= 0) return false;
        out.width = width;
        out.height = height;
        out.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        return geode_viz_still_read(v_, out.rgba.data(), out.rgba.size()) != 0;
    }
    void requestStill() override { geode_viz_still_request(v_); }

private:
    void dropMark() {
        if (mark_) geode_export_pass_destroy(mark_);
        mark_ = nullptr;
    }

    geode_viz* v_;
    AAssetManager* assets_;
    geode_export_pass* mark_ = nullptr;  // stage thread
};

struct Stage {
    std::shared_ptr<StageThread> thread;
    std::shared_ptr<stage::LiveClient> live;  // none on a stage made with a C client
    geode_viz* viz = nullptr;                 // the live client's; valid while its lockViz holds
    std::mutex lookLock;                      // held across a look's compare and apply, so two never interleave
    stage::LookLatch look;
    std::mutex stillLock;
    std::optional<stage::Still> still;
};

// Handles are ids, never addresses: a call on a stage already destroyed finds nothing and does nothing.
std::mutex gLock;
std::unordered_map<uintptr_t, std::shared_ptr<Stage>> gStages;
uintptr_t gLastId = 0;

geode_stage* enlist(std::shared_ptr<Stage> stage) {
    std::lock_guard<std::mutex> guard(gLock);
    const uintptr_t id = ++gLastId;
    gStages.emplace(id, std::move(stage));
    return reinterpret_cast<geode_stage*>(id);
}

std::shared_ptr<Stage> find(const geode_stage* handle) {
    std::lock_guard<std::mutex> guard(gLock);
    const auto it = gStages.find(reinterpret_cast<uintptr_t>(handle));
    return it == gStages.end() ? nullptr : it->second;
}

std::shared_ptr<Stage> delist(const geode_stage* handle) {
    std::lock_guard<std::mutex> guard(gLock);
    const auto it = gStages.find(reinterpret_cast<uintptr_t>(handle));
    if (it == gStages.end()) return nullptr;
    std::shared_ptr<Stage> stage = std::move(it->second);
    gStages.erase(it);
    return stage;
}

std::shared_ptr<stage::LiveClient> liveOf(const geode_stage* handle) {
    const std::shared_ptr<Stage> s = find(handle);
    return s ? s->live : nullptr;
}

// The clients whose renderer this thread holds, innermost last, so unlock needs no handle that may be gone.
thread_local std::vector<std::shared_ptr<stage::LiveClient>> tHeld;

std::vector<float> floats(const float* data, int count) {
    return data && count > 0 ? std::vector<float>(data, data + count) : std::vector<float>();
}

// A slot list as Look carries it: the slot count, then every slot's floats.
std::vector<float> slots(const float* data, int slotCount, int count) {
    std::vector<float> out;
    if (!data || slotCount <= 0 || count < slotCount) return out;
    out.reserve(static_cast<size_t>(count) + 1);
    out.push_back(static_cast<float>(slotCount));
    out.insert(out.end(), data, data + count);
    return out;
}

template <typename Set>
void eachSlot(const std::vector<float>& list, int limit, Set set) {
    if (list.empty()) return;
    const int count = std::min(static_cast<int>(list[0]), limit);
    const int each = static_cast<int>(list.size() - 1) / static_cast<int>(list[0]);
    for (int slot = 0; slot < count; ++slot) set(slot, list.data() + 1 + slot * each, each);
}

void applyLook(geode_viz* v, const stage::Look& l, uint32_t parts) {
    if ((parts & stage::kLookScene) != 0) geode_viz_request_scene(v, l.sceneId.c_str());
    if ((parts & stage::kLookParams) != 0 && !l.params.empty()) geode_viz_set_params(v, l.params.data(), static_cast<int>(l.params.size()));
    if ((parts & stage::kLookLfos) != 0) {
        eachSlot(l.lfos, GEODE_LFO_SLOTS, [v](int slot, const float* config, int count) { geode_viz_set_lfo(v, slot, config, count); });
    }
    if ((parts & stage::kLookAdsrs) != 0) {
        eachSlot(l.adsrs, GEODE_ADSR_SLOTS, [v](int slot, const float* config, int count) { geode_viz_set_adsr(v, slot, config, count); });
    }
    if ((parts & stage::kLookTuning) != 0 && l.tuning.size() >= static_cast<size_t>(GEODE_SOUND_TUNING_FLOATS)) {
        GeodeSoundTuning tuning{};
        std::memcpy(&tuning, l.tuning.data(), sizeof(float) * GEODE_SOUND_TUNING_FLOATS);
        geode_viz_set_sound_tuning(v, &tuning);
    }
    if ((parts & stage::kLookReactions) != 0) {
        // An empty list of the user's own is still theirs: the pointer must not be null for it.
        static const float kNone = 0.0f;
        const float* routes = l.reactions.empty() ? &kNone : l.reactions.data();
        geode_viz_set_user_reactions(v, l.hasReactions ? routes : nullptr, l.hasReactions ? static_cast<int>(l.reactions.size()) : 0,
                                     l.hasReactions ? 1 : 0);
    }
    if ((parts & stage::kLookReducedMotion) != 0) geode_viz_set_reduced_motion(v, l.reducedMotion ? 1 : 0);
    if ((parts & stage::kLookTransition) != 0) geode_viz_set_transition(v, l.transitionId.c_str(), l.transitionMs);
}

size_t copyText(const std::string& text, char* out, size_t capacity) {
    if (out && capacity > 0) {
        const size_t n = std::min(text.size(), capacity - 1);
        std::memcpy(out, text.data(), n);
        out[n] = '\0';
    }
    return text.size();
}

// Runs `call` on the stage's thread object; a handle that is gone runs nothing.
template <typename Call>
void onThread(geode_stage* handle, const char* what, Call call) {
    try {
        if (const std::shared_ptr<Stage> s = find(handle)) call(*s->thread);
    } catch (...) {
        GEODE_LOGE(kTag, "%s failed", what);
    }
}

}  // namespace

extern "C" {

geode_stage* geode_stage_create(int kind, const geode_stage_client* client, float cap_fps) {
    if (!complete(client) || (kind != GEODE_STAGE_KIND_VIEW && kind != GEODE_STAGE_KIND_SURFACE)) return nullptr;
    try {
        auto s = std::make_shared<Stage>();
        s->thread = StageThread::start(static_cast<stage::Kind>(kind), std::make_shared<CClient>(*client), cap_fps);
        return enlist(std::move(s));
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_create failed");
        return nullptr;
    }
}

geode_stage* geode_stage_create_live(int kind, AAssetManager* assets, const char* cache_dir, const char* milk_texture_dir,
                                     float cap_fps) {
    if (kind != GEODE_STAGE_KIND_VIEW && kind != GEODE_STAGE_KIND_SURFACE) return nullptr;
    geode_viz* viz = geode_viz_create(assets, cache_dir);
    if (!viz) return nullptr;
    std::shared_ptr<stage::LiveClient> live;
    try {
        geode_viz_set_milk_texture_dir(viz, milk_texture_dir);
        auto s = std::make_shared<Stage>();
        s->viz = viz;
        std::unique_ptr<stage::LiveViz> bound = std::make_unique<Viz>(viz, assets);
        viz = nullptr;  // the binding frees it from here
        live = std::make_shared<stage::LiveClient>(std::move(bound));
        s->live = live;
        s->thread = StageThread::start(static_cast<stage::Kind>(kind), live, cap_fps);
        return enlist(std::move(s));
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_create_live failed");
        // No thread took the client: its release, here, frees the renderer.
        if (live) live->release(false);
        if (viz) geode_viz_destroy(viz);
        return nullptr;
    }
}

void geode_stage_set_window(geode_stage* s, struct ANativeWindow* window, int width, int height) {
    onThread(s, "geode_stage_set_window", [&](StageThread& t) { t.setWindow(window, width, height); });
}

void geode_stage_set_size(geode_stage* s, int width, int height) {
    onThread(s, "geode_stage_set_size", [&](StageThread& t) { t.setSize(width, height); });
}

void geode_stage_set_view(geode_stage* s, unsigned view_flags) {
    onThread(s, "geode_stage_set_view", [&](StageThread& t) { t.setMode(stage::viewMode(view_flags)); });
}

int geode_stage_set_surface(geode_stage* s, unsigned surface_flags, long long animate_until_ms, long long now_ms) {
    const geode::viz::surface::RunMode mode = geode::viz::surface::mode(surface_flags, animate_until_ms, now_ms);
    onThread(s, "geode_stage_set_surface", [&](StageThread& t) { t.setMode(mode); });
    return static_cast<int>(mode);
}

void geode_stage_pause(geode_stage* s) {
    onThread(s, "geode_stage_pause", [](StageThread& t) { t.setPaused(true); });
}

void geode_stage_resume(geode_stage* s) {
    onThread(s, "geode_stage_resume", [](StageThread& t) { t.setPaused(false); });
}

void geode_stage_redraw(geode_stage* s) {
    onThread(s, "geode_stage_redraw", [](StageThread& t) { t.redraw(); });
}

void geode_stage_set_cap(geode_stage* s, float cap_fps) {
    onThread(s, "geode_stage_set_cap", [&](StageThread& t) { t.setCap(cap_fps); });
}

unsigned geode_stage_state(const geode_stage* s) {
    const std::shared_ptr<Stage> stage = find(s);
    return stage ? stage->thread->state() : 0u;
}

float geode_stage_paced_fps(const geode_stage* s) {
    const std::shared_ptr<Stage> stage = find(s);
    return stage ? stage->thread->pacedFps() : 0.0f;
}

unsigned geode_stage_take_events(geode_stage* s) {
    const std::shared_ptr<Stage> stage = find(s);
    return stage ? stage->thread->takeEvents() : 0u;
}

void geode_stage_destroy(geode_stage* s) {
    try {
        if (const std::shared_ptr<Stage> stage = delist(s)) stage->thread->quitAndWait();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_destroy failed");
    }
}

geode_viz* geode_stage_viz_lock(geode_stage* s) {
    try {
        const std::shared_ptr<Stage> stage = find(s);
        if (!stage || !stage->live || !stage->viz) return nullptr;
        tHeld.push_back(stage->live);
        if (stage->live->lockViz()) return stage->viz;
        tHeld.pop_back();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_viz_lock failed");
    }
    return nullptr;
}

void geode_stage_viz_unlock(void) {
    if (tHeld.empty()) return;
    tHeld.back()->unlockViz();
    tHeld.pop_back();
}

void geode_stage_set_sound(geode_stage* s, const geode_pcm_ring* ring) {
    try {
        if (const std::shared_ptr<stage::LiveClient> live = liveOf(s)) live->setSound(ring);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_sound failed");
    }
}

void geode_stage_set_look(geode_stage* s, const geode_stage_look* look) {
    if (!look) return;
    try {
        const std::shared_ptr<Stage> stage = find(s);
        if (!stage || !stage->live || !stage->viz) return;
        stage::Look next;
        next.sceneId = look->scene_id ? look->scene_id : "";
        next.params = floats(look->params, look->param_count);
        next.lfos = slots(look->lfos, look->lfo_slots, look->lfo_floats);
        next.adsrs = slots(look->adsrs, look->adsr_slots, look->adsr_floats);
        next.tuning = floats(look->tuning, look->tuning_count);
        next.hasReactions = look->has_reactions != 0;
        if (next.hasReactions) next.reactions = floats(look->reactions, look->reaction_count);
        next.reducedMotion = look->reduced_motion != 0;
        next.transitionId = look->transition_id ? look->transition_id : "";
        next.transitionMs = look->transition_ms;
        std::lock_guard<std::mutex> one(stage->lookLock);
        if (!stage->live->lockViz()) return;
        const uint32_t parts = stage->look.take(std::move(next));
        try {
            applyLook(stage->viz, stage->look.look(), parts);
        } catch (...) {
            GEODE_LOGE(kTag, "geode_stage_set_look failed");
        }
        stage->live->unlockViz();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_look failed");
    }
}

void geode_stage_warm_transition(geode_stage* s, const char* id) {
    if (!id) return;
    try {
        const std::shared_ptr<Stage> stage = find(s);
        if (stage && stage->live && stage->live->warm(id)) stage->thread->service();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_warm_transition failed");
    }
}

int geode_stage_record_start(geode_stage* s, geode_recorder* recorder, geode_record_session* session, const uint8_t* tile,
                             int tile_width, int tile_height, int placement, int corner) {
    bool taken = false;
    try {
        const std::shared_ptr<Stage> stage = find(s);
        if (stage && stage->live) {
            stage::RecordAsk ask;
            ask.recorder = recorder;
            ask.session = session;
            if (tile && tile_width > 0 && tile_height > 0) {
                ask.tile.assign(tile, tile + static_cast<size_t>(tile_width) * static_cast<size_t>(tile_height) * 4);
            }
            ask.tileWidth = tile_width;
            ask.tileHeight = tile_height;
            ask.placement = placement;
            ask.corner = corner;
            taken = stage->live->recordStart(std::move(ask));
            if (taken) stage->thread->service();
        }
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_record_start failed");
    }
    if (!taken) {
        // No stage will open or close this tee: the recording ends here, and its session is told nothing draws.
        geode_recorder_stop(recorder, GEODE_RECORD_STOP_FAILED, geode_record_now_ns());
        geode_record_session_tee_closed(session);
    }
    return taken ? 1 : 0;
}

void geode_stage_record_stop(geode_stage* s) {
    try {
        const std::shared_ptr<Stage> stage = find(s);
        if (stage && stage->live && stage->live->recordStop()) stage->thread->service();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_record_stop failed");
    }
}

int geode_stage_record_closed(const geode_stage* s) {
    const std::shared_ptr<stage::LiveClient> live = liveOf(s);
    return !live || live->recordClosed() ? 1 : 0;
}

size_t geode_stage_error(const geode_stage* s, char* out, size_t capacity) {
    try {
        const std::shared_ptr<stage::LiveClient> live = liveOf(s);
        return copyText(live ? live->error() : std::string(), out, capacity);
    } catch (...) {
        return 0;
    }
}

size_t geode_stage_take_milk_loaded(geode_stage* s, char* out, size_t capacity) {
    try {
        const std::shared_ptr<stage::LiveClient> live = liveOf(s);
        return copyText(live ? live->takeMilkLoaded() : std::string(), out, capacity);
    } catch (...) {
        return 0;
    }
}

int geode_stage_take_director_moments(geode_stage* s) {
    const std::shared_ptr<stage::LiveClient> live = liveOf(s);
    return live ? live->takeDirectorMoments() : 0;
}

int geode_stage_liquid_mode(const geode_stage* s) {
    const std::shared_ptr<stage::LiveClient> live = liveOf(s);
    return live ? live->liquidMode() : 0;
}

int geode_stage_still_size(geode_stage* s, int* width, int* height) {
    const std::shared_ptr<Stage> stage = find(s);
    if (!stage || !stage->live || !width || !height) return 0;
    std::lock_guard<std::mutex> one(stage->stillLock);
    stage::Still next;
    if (stage->live->takeStill(next)) stage->still = std::move(next);
    if (!stage->still) return 0;
    *width = stage->still->width;
    *height = stage->still->height;
    return 1;
}

int geode_stage_still_read(geode_stage* s, void* out, size_t capacity) {
    const std::shared_ptr<Stage> stage = find(s);
    if (!stage || !out) return 0;
    std::lock_guard<std::mutex> one(stage->stillLock);
    if (!stage->still || capacity < stage->still->rgba.size()) return 0;
    std::memcpy(out, stage->still->rgba.data(), stage->still->rgba.size());
    stage->still.reset();
    return 1;
}

}  // extern "C"
