// The cast's render thread (core/output/android/CastRenderHost): the C ABI. Android build only (EGL, MediaCodec).
#include <cstdint>
#include <memory>
#include <utility>

#include "api/geode_api.h"
#include "output/android/CastRenderHost.hpp"
#include "util/Log.hpp"

using geode::output::CastRender;
using geode::output::CastRenderSetup;

struct geode_cast_render {
    CastRender render;
    explicit geode_cast_render(CastRenderSetup setup) : render(std::move(setup)) {}
};

geode_cast_render* geode_cast_render_start(int64_t server, int64_t webrtc, const geode_pcm_ring* music,
                                           AAssetManager* assets, const char* cache_dir) {
    if (!assets || !cache_dir) return nullptr;
    try {
        CastRenderSetup setup;
        setup.server = server;
        setup.webrtc = webrtc;
        setup.music = music;
        setup.assets = assets;
        setup.cacheDir = cache_dir;
        auto made = std::make_unique<geode_cast_render>(std::move(setup));
        made->render.start();
        return made.release();
    } catch (...) {
        GEODE_LOGE("CastRender", "geode_cast_render_start failed");
        return nullptr;
    }
}

void geode_cast_render_set_look(geode_cast_render* r, const geode_viz_look* look) {
    if (!r || !look) return;
    try {
        r->render.setLook(*look);
    } catch (...) {
    }
}

int geode_cast_render_stop(geode_cast_render* r, int64_t timeout_ms) {
    return r && r->render.stop(timeout_ms) ? 1 : 0;
}

int geode_cast_render_failed(const geode_cast_render* r) { return r && r->render.failed() ? 1 : 0; }

int geode_cast_render_take_director_moments(geode_cast_render* r) { return r ? r->render.takeDirectorMoments() : 0; }

void geode_cast_render_free(geode_cast_render* r) { delete r; }

void geode_cast_render_set_stage_flags(unsigned flags) { CastRender::setStageFlags(flags); }

void geode_cast_render_set_device_ring(const geode_pcm_ring* ring) { CastRender::setDeviceRing(ring); }
