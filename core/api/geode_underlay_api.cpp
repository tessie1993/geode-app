#include "api/geode_api.h"
#include "viz/UnderlayPrep.hpp"

extern "C" {

int geode_underlay_sample_size(int src_w, int src_h, int target_w, int target_h) {
    return geode::viz::underlay::sampleSize(src_w, src_h, target_w, target_h);
}

int geode_underlay_prepare(const uint32_t* src, int w, int h, int target_w, int target_h, int blur_radius, float dim,
                           uint32_t* out, int out_capacity) {
    return geode::viz::underlay::prepare(src, w, h, target_w, target_h, blur_radius, dim, out, out_capacity);
}

}  // extern "C"
