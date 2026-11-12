// The artwork palette's C ABI (viz/ArtPalette.hpp). Pure, any thread; marshalled by geode_viz_jni.cpp.
#include "api/geode_api.h"
#include "viz/ArtPalette.hpp"

extern "C" {

int geode_art_palette_extract(const int32_t* argb, int n, float* out) {
    const auto got = geode::viz::art::extract(argb, n);
    if (!got || !out) return 0;
    out[0] = got->baseHue;
    out[1] = got->span;
    out[2] = got->confidence;
    return 1;
}

}  // extern "C"
