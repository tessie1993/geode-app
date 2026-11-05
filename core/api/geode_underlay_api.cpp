#include "api/geode_api.h"
#include "viz/Underlay.hpp"

namespace underlay = geode::viz::underlay;

extern "C" {

void geode_underlay_crop_rect(int src_w, int src_h, int target_w, int target_h, int* out_xywh) {
    if (!out_xywh) return;
    const underlay::Rect r = underlay::cropRect(src_w, src_h, target_w, target_h);
    out_xywh[0] = r.x;
    out_xywh[1] = r.y;
    out_xywh[2] = r.width;
    out_xywh[3] = r.height;
}

void geode_underlay_finish(uint32_t* pixels, int width, int height, int blur_radius, float dim) {
    underlay::finish(pixels, width, height, blur_radius, dim);
}

float geode_underlay_clamp_amount(float amount) { return underlay::clampAmount(amount); }

float geode_underlay_clamp_dim(float dim) { return underlay::clampDim(dim); }

int geode_underlay_clamp_blur(int radius) { return underlay::clampBlur(radius); }

int geode_underlay_blur_max(void) { return underlay::kBlurMax; }

}  // extern "C"
