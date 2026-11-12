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

void run(Pass& pass, const PassInputs& inputs, const PassTarget& target, double seconds = 0.0) {
    const FrameCtx ctx{target.width, target.height, seconds, 0.0f, Surface::Export, nullptr};
    if (!pass.wants(ctx)) return;
    pass.prepare(ctx);
    pass.draw(inputs, target);
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

int geode_export_watermark_placement(int required, int pro_adds, int placement, int use) {
    return exportmath::watermark::placement(required != 0, pro_adds != 0, placement, use);
}

float geode_export_watermark_text_px(int short_side, int placement) { return exportmath::watermark::textPx(short_side, placement); }

float geode_export_watermark_angle(int placement) { return exportmath::watermark::angleDegrees(placement); }

int geode_export_watermark_alpha(int placement) { return exportmath::watermark::textAlpha(placement); }

int geode_export_watermark_tile_width(float text_width, float text_px, int placement) {
    return exportmath::watermark::tileWidth(text_width, text_px, placement);
}

int geode_export_watermark_tile_height(int tile_width, float text_px, int placement) {
    return exportmath::watermark::tileHeight(tile_width, text_px, placement);
}

geode_export_pass* geode_export_watermark_create(AAssetManager* assets, const uint8_t* rgba, int tile_w, int tile_h, int placement,
                                                 int corner, int64_t length_ms) {
    auto* h = make<WatermarkPass>(assets);
    if (h && !as<WatermarkPass>(h)->create(h->source, rgba, tile_w, tile_h, placement, corner, length_ms, &h->error) &&
        h->error.empty()) {
        h->error = "watermark pass failed to build";
    }
    return h;
}

void geode_export_watermark_draw(geode_export_pass* h, int frame_w, int frame_h, int64_t time_ms) {
    if (auto* p = as<WatermarkPass>(h)) run(*p, {}, {0u, frame_w, frame_h}, static_cast<double>(time_ms) / 1000.0);
}

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

}  // extern "C"
