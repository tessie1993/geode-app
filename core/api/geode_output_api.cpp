#include <algorithm>

#include "api/geode_api.h"
#include "output/EncoderLadder.hpp"

extern "C" {

int geode_encoder_attempts(int requested_fps, int bit_rate, const int* ceilings, int codec_count, int* out_codec,
                           int* out_fps, int* out_bit_rate, int capacity) {
    namespace out = geode::output;
    if (!ceilings || !out_codec || !out_fps || !out_bit_rate || capacity <= 0) return 0;
    out::EncoderAttempt attempts[out::kAttemptMaxCount];
    const int count =
        std::min(out::encoderAttempts(requested_fps, bit_rate, ceilings, codec_count, attempts), capacity);
    for (int i = 0; i < count; ++i) {
        out_codec[i] = attempts[i].codec;
        out_fps[i] = attempts[i].rung.fps;
        out_bit_rate[i] = attempts[i].rung.bitRate;
    }
    return count;
}

}  // extern "C"
