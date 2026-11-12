#include "api/geode_stage_api.h"

#include <memory>
#include <new>

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

struct geode_stage {
    std::shared_ptr<StageThread> thread;
};

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

}  // namespace

extern "C" {

geode_stage* geode_stage_create(int kind, const geode_stage_client* client, float cap_fps) {
    if (!complete(client) || (kind != GEODE_STAGE_KIND_VIEW && kind != GEODE_STAGE_KIND_SURFACE)) return nullptr;
    try {
        auto handle = std::make_unique<geode_stage>();
        handle->thread = StageThread::start(static_cast<stage::Kind>(kind), std::make_unique<CClient>(*client), cap_fps);
        return handle.release();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_create failed");
        return nullptr;
    }
}

void geode_stage_set_window(geode_stage* s, struct ANativeWindow* window, int width, int height) {
    if (!s) return;
    try {
        s->thread->setWindow(window, width, height);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_window failed");
    }
}

void geode_stage_set_size(geode_stage* s, int width, int height) {
    if (!s) return;
    try {
        s->thread->setSize(width, height);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_size failed");
    }
}

void geode_stage_set_view(geode_stage* s, unsigned view_flags) {
    if (!s) return;
    try {
        s->thread->setMode(stage::viewMode(view_flags));
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_view failed");
    }
}

int geode_stage_set_surface(geode_stage* s, unsigned surface_flags, long long animate_until_ms, long long now_ms) {
    const geode::viz::surface::RunMode mode = geode::viz::surface::mode(surface_flags, animate_until_ms, now_ms);
    if (s) {
        try {
            s->thread->setMode(mode);
        } catch (...) {
            GEODE_LOGE(kTag, "geode_stage_set_surface failed");
        }
    }
    return static_cast<int>(mode);
}

void geode_stage_pause(geode_stage* s) {
    if (!s) return;
    try {
        s->thread->setPaused(true);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_pause failed");
    }
}

void geode_stage_resume(geode_stage* s) {
    if (!s) return;
    try {
        s->thread->setPaused(false);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_resume failed");
    }
}

void geode_stage_redraw(geode_stage* s) {
    if (!s) return;
    try {
        s->thread->redraw();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_redraw failed");
    }
}

void geode_stage_set_cap(geode_stage* s, float cap_fps) {
    if (!s) return;
    try {
        s->thread->setCap(cap_fps);
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_set_cap failed");
    }
}

unsigned geode_stage_state(const geode_stage* s) { return s ? s->thread->state() : 0u; }

float geode_stage_paced_fps(const geode_stage* s) { return s ? s->thread->pacedFps() : 0.0f; }

unsigned geode_stage_take_events(geode_stage* s) { return s ? s->thread->takeEvents() : 0u; }

void geode_stage_destroy(geode_stage* s) {
    if (!s) return;
    try {
        s->thread->quitAndWait();
    } catch (...) {
        GEODE_LOGE(kTag, "geode_stage_destroy failed");
    }
    delete s;
}

}  // extern "C"
