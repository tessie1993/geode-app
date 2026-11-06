#include "audio/dsp/PcmConvert.hpp"

#include <cmath>

namespace geode::audio {

namespace {
int16_t saturate(int v) { return static_cast<int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }
}  // namespace

size_t downmixS16(const int16_t* in, size_t frames, int srcChannels, int dstChannels, int16_t* out) {
    if (!in || !out || srcChannels <= dstChannels || (dstChannels != 1 && dstChannels != 2)) return 0;
    const size_t src = static_cast<size_t>(srcChannels);
    for (size_t f = 0; f < frames; ++f) {
        const int16_t* frame = in + f * src;
        if (dstChannels == 1) {
            int acc = 0;
            for (size_t c = 0; c < src; ++c) acc += frame[c];
            out[f] = static_cast<int16_t>(acc / srcChannels);
        } else {
            int rest = 0;
            for (size_t c = 2; c < src; ++c) rest += frame[c];
            const int fold = srcChannels > 2 ? rest / (srcChannels - 2) / 2 : 0;
            out[f * 2] = saturate(frame[0] + fold);
            out[f * 2 + 1] = saturate(frame[1] + fold);
        }
    }
    return frames;
}

void floatToS16(const float* in, size_t samples, int16_t* out) {
    if (!in || !out) return;
    for (size_t i = 0; i < samples; ++i) {
        const float v = in[i];
        if (std::isnan(v)) {
            out[i] = 0;
            continue;
        }
        const float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
        out[i] = static_cast<int16_t>(static_cast<int>(c * 32767.0f));
    }
}

void s16ToFloat(const int16_t* in, size_t samples, float* out) {
    if (!in || !out) return;
    for (size_t i = 0; i < samples; ++i) out[i] = static_cast<float>(in[i]) / 32768.0f;
}

}  // namespace geode::audio
