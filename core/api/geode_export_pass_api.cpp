// geode_export_* (core/api/geode_api.h): the export post-passes of core/viz/frame/ExportPasses behind one
// opaque handle. Each call builds the frame's FrameCtx and PassTarget and runs the pass the way the frame
// graph will (wants, prepare, draw), so when FrameGraph::run owns the export frame these calls become its
// pass list and the passes do not change.
#include <GLES3/gl3.h>

#include <memory>
#include <new>
#include <string>

#include "api/geode_api.h"
#include "viz/ShaderSource.hpp"
#include "viz/frame/ExportPassMath.hpp"
#include "viz/frame/ExportPasses.hpp"

using namespace geode::viz;
using namespace geode::viz::frame;

struct geode_export_pass {
    explicit geode_export_pass(AAssetManager* assets) : source(assets) {}

    ShaderSource source;
    std::unique_ptr<Pass> pass;
    // Which pass `pass` is: a tag rather than dynamic_cast, so nothing here depends on RTTI.
    const void* kind = nullptr;
    std::string error;
    // The frame the encoder-surface passes draw into (watermark, seam): set at create or per draw.
    int width = 0;
    int height = 0;
};

namespace {

// One address per pass type, the handle's kind tag.
template <typename P>
const void* kindOf() {
    static const char tag = 0;
    return &tag;
}

template <typename P>
P* as(geode_export_pass* h) {
    // Every handle holds exactly the pass its create call made; a call for another kind is a no-op.
    return h && h->kind == kindOf<P>() ? static_cast<P*>(h->pass.get()) : nullptr;
}

void run(Pass& pass, const PassInputs& inputs, const PassTarget& target) {
    const FrameCtx ctx{target.width, target.height, 0.0, 0.0f, Surface::Export, nullptr};
    if (!pass.wants(ctx)) return;
    pass.prepare(ctx);
    pass.draw(inputs, target);
}

// Media3 binds each program's output framebuffer and viewport before it calls drawFrame.
PassTarget boundTarget() {
    GLint fbo = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    return {static_cast<unsigned>(fbo), viewport[2], viewport[3]};
}

template <typename P>
geode_export_pass* make(AAssetManager* assets) {
    auto* h = new (std::nothrow) geode_export_pass(assets);
    if (!h) return nullptr;
    h->pass = std::make_unique<P>();
    h->kind = kindOf<P>();
    return h;
}

}  // namespace

extern "C" {

const char* geode_export_pass_error(const geode_export_pass* h) { return h ? h->error.c_str() : ""; }

void geode_export_pass_destroy(geode_export_pass* h) {
    if (!h) return;
    if (h->pass) h->pass->release();
    delete h;
}

float geode_export_watermark_text_px(int short_side) { return exportmath::watermark::textPx(short_side); }

int geode_export_watermark_tile_width(float text_width) { return exportmath::watermark::tileWidth(text_width); }

int geode_export_watermark_tile_height(int tile_width) { return exportmath::watermark::tileHeight(tile_width); }

geode_export_pass* geode_export_watermark_create(AAssetManager* assets, const uint8_t* rgba, int tile_w, int tile_h) {
    auto* h = make<WatermarkPass>(assets);
    if (h && !as<WatermarkPass>(h)->create(h->source, rgba, tile_w, tile_h, &h->error) && h->error.empty()) {
        h->error = "watermark pass failed to build";
    }
    return h;
}

void geode_export_watermark_draw(geode_export_pass* h, int frame_w, int frame_h) {
    if (auto* p = as<WatermarkPass>(h)) run(*p, {}, {0u, frame_w, frame_h});
}

float geode_export_seam_weight(int tail, int seam_frames) { return exportmath::seam::weight(tail, seam_frames); }

geode_export_pass* geode_export_seam_create(AAssetManager* assets, int width, int height) {
    auto* h = make<SeamPass>(assets);
    if (!h) return nullptr;
    h->width = width;
    h->height = height;
    if (!as<SeamPass>(h)->create(h->source, width, height, &h->error) && h->error.empty()) h->error = "seam pass failed to build";
    return h;
}

int geode_export_seam_allocate(geode_export_pass* h, int requested) {
    auto* p = as<SeamPass>(h);
    return p ? p->allocate(requested) : 0;
}

void geode_export_seam_capture(geode_export_pass* h, int index) {
    if (auto* p = as<SeamPass>(h)) {
        p->stageCapture(index);
        run(*p, {}, {0u, h->width, h->height});
    }
}

void geode_export_seam_blend(geode_export_pass* h, int index, int tail, int seam_frames) {
    if (auto* p = as<SeamPass>(h)) {
        p->stageBlend(index, tail, seam_frames);
        run(*p, {}, {0u, h->width, h->height});
    }
}

geode_export_pass* geode_export_transition_create(AAssetManager* assets, const char* id, int64_t duration_us) {
    auto* h = make<TransitionPass>(assets);
    if (h && !as<TransitionPass>(h)->create(h->source, id ? id : "", duration_us, &h->error) && h->error.empty()) {
        h->error = "transition pass failed to build";
    }
    return h;
}

void geode_export_transition_configure(geode_export_pass* h, int width, int height) {
    if (auto* p = as<TransitionPass>(h)) p->configure(width, height);
}

void geode_export_transition_set_from(geode_export_pass* h, const uint8_t* rgba, int width, int height) {
    if (auto* p = as<TransitionPass>(h)) p->setFrom(rgba, width, height);
}

void geode_export_transition_draw(geode_export_pass* h, unsigned input_tex, int64_t pts_us) {
    if (auto* p = as<TransitionPass>(h)) {
        p->setPresentationUs(pts_us);
        PassInputs inputs;
        inputs.sceneTex = input_tex;
        run(*p, inputs, boundTarget());
    }
}

geode_export_pass* geode_export_capture_create(AAssetManager* assets) {
    auto* h = make<CapturePass>(assets);
    if (h && !as<CapturePass>(h)->create(h->source, &h->error) && h->error.empty()) h->error = "capture pass failed to build";
    return h;
}

void geode_export_capture_configure(geode_export_pass* h, int width, int height) {
    if (auto* p = as<CapturePass>(h)) p->configure(width, height);
}

void geode_export_capture_draw(geode_export_pass* h, unsigned input_tex) {
    if (auto* p = as<CapturePass>(h)) {
        PassInputs inputs;
        inputs.sceneTex = input_tex;
        run(*p, inputs, boundTarget());
    }
}

int geode_export_capture_read(geode_export_pass* h, uint8_t* out, size_t capacity) {
    const auto* p = as<CapturePass>(h);
    return p && p->readBack(out, capacity) ? 1 : 0;
}

}  // extern "C"
