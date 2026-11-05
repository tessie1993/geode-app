#include "api/geode_api.h"
#include "viz/ArtPalette.hpp"

extern "C" {

int geode_art_palette(const uint32_t* argb, int count, float* out) {
    geode::viz::art_palette::Palette p;
    if (!out || !geode::viz::art_palette::extract(argb, count, p)) return 0;
    out[0] = p.baseHue;
    out[1] = p.span;
    out[2] = p.confidence;
    return 1;
}

}  // extern "C"
