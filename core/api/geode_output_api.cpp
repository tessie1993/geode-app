#include <algorithm>

#include "api/geode_api.h"
#include "output/EncoderLadder.hpp"

extern "C" {

int geode_encoder_ladder(int requested_fps, int bit_rate, int ceiling_fps, int* out_fps, int* out_bit_rate,
                         int capacity) {
    if (!out_fps || !out_bit_rate || capacity <= 0) return 0;
    geode::output::EncoderRung rungs[geode::output::kLadderMaxRungs];
    const int count = std::min(geode::output::encoderLadder(requested_fps, bit_rate, ceiling_fps, rungs), capacity);
    for (int i = 0; i < count; ++i) {
        out_fps[i] = rungs[i].fps;
        out_bit_rate[i] = rungs[i].bitRate;
    }
    return count;
}

}  // extern "C"
