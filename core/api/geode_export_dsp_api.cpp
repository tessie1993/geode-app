#include <cstdint>
#include <cstring>
#include <memory>

#include "api/geode_api.h"
#include "audio/dsp/Loudness.hpp"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmResampler.hpp"

struct geode_resampler {
    geode::audio::PcmResampler r;
};

struct geode_loudness {
    geode::audio::LoudnessAnalyser a;
};

namespace {

// The decoder's PCM to float for the meter, a block at a time on the stack (#490: this was a per-sample
// Kotlin loop). The scalings are the ones LoudnessMeter used: s16 / 32768, float as is; 8-bit as the
// ingest reads it. Native byte order, as MediaCodec hands it.
constexpr size_t kBlockSamples = 2048;

int bytesOf(int encoding) {
    switch (encoding) {
        case 2: return 2;   // ENCODING_PCM_16BIT
        case 3: return 1;   // ENCODING_PCM_8BIT
        case 4: return 4;   // ENCODING_PCM_FLOAT
        default: return 0;
    }
}

void toFloat(const unsigned char* in, size_t samples, int encoding, float* out) {
    switch (encoding) {
        case 2:
            for (size_t i = 0; i < samples; ++i) {
                int16_t v;
                std::memcpy(&v, in + i * 2, sizeof v);
                out[i] = static_cast<float>(v) / 32768.0f;
            }
            return;
        case 3:
            for (size_t i = 0; i < samples; ++i) out[i] = static_cast<float>(static_cast<int>(in[i]) - 128) / 128.0f;
            return;
        default:
            std::memcpy(out, in, samples * sizeof(float));
            return;
    }
}

}  // namespace

extern "C" {

geode_resampler* geode_resampler_create(int in_rate, int out_rate, int channels) {
    if (in_rate <= 0 || out_rate <= 0 || channels <= 0) return nullptr;
    return new geode_resampler{geode::audio::PcmResampler(in_rate, out_rate, channels)};
}

void geode_resampler_destroy(geode_resampler* r) {
    std::unique_ptr<geode_resampler> owned(r);
}

int geode_resampler_channels(geode_resampler* r) {
    return r ? r->r.channels() : 0;
}

size_t geode_resampler_max_out_frames(geode_resampler* r, size_t in_frames) {
    return r ? r->r.maxOutFrames(in_frames) : 0;
}

int64_t geode_resampler_process(geode_resampler* r, const int16_t* in, size_t in_frames, int16_t* out,
                                size_t out_capacity_frames) {
    if (!r || in_frames == 0) return 0;
    if (!in || !out || out_capacity_frames < r->r.maxOutFrames(in_frames)) return -1;
    return static_cast<int64_t>(r->r.process(in, in_frames, out));
}

int geode_loudness_supports(int channels) {
    return geode::audio::LoudnessAnalyser::supports(channels) ? 1 : 0;
}

geode_loudness* geode_loudness_create(int sample_rate, int channels) {
    if (sample_rate <= 0 || !geode::audio::LoudnessAnalyser::supports(channels)) return nullptr;
    return new geode_loudness{geode::audio::LoudnessAnalyser(sample_rate, channels)};
}

void geode_loudness_destroy(geode_loudness* l) {
    std::unique_ptr<geode_loudness> owned(l);
}

void geode_loudness_feed(geode_loudness* l, const float* interleaved, size_t samples) {
    if (l && interleaved && samples > 0) l->a.feed(interleaved, samples);
}

int geode_loudness_feed_pcm(geode_loudness* l, const void* bytes, size_t byte_count, int encoding) {
    const int bps = bytesOf(encoding);
    if (bps == 0) return 0;
    if (!l || !bytes) return 1;
    const auto* in = static_cast<const unsigned char*>(bytes);
    size_t left = byte_count / static_cast<size_t>(bps);
    float block[kBlockSamples];
    // Whole frames per block: the meter ignores a trailing partial frame, so a split frame would be lost.
    const size_t step = kBlockSamples / static_cast<size_t>(l->a.channels()) * static_cast<size_t>(l->a.channels());
    while (left > 0) {
        const size_t n = left < step ? left : step;
        toFloat(in, n, encoding, block);
        l->a.feed(block, n);
        in += n * static_cast<size_t>(bps);
        left -= n;
    }
    return 1;
}

void geode_loudness_feed_s16(geode_loudness* l, const int16_t* interleaved, size_t samples) {
    if (!l || !interleaved || samples == 0) return;
    // Converted through a stack block a whole number of frames long, so the meter's "a trailing partial
    // frame is ignored" only ever applies to the end of the whole feed, exactly as one float feed would.
    constexpr size_t kBlock = 4096;
    float block[kBlock];
    const size_t channels = static_cast<size_t>(l->a.channels());
    const size_t step = kBlock - kBlock % channels;
    for (size_t at = 0; at < samples; at += step) {
        const size_t n = samples - at < step ? samples - at : step;
        geode::audio::s16ToFloat(interleaved + at, n, block);
        l->a.feed(block, n);
    }
}

int geode_loudness_has_block(geode_loudness* l) {
    return l && l->a.hasCompleteBlock() ? 1 : 0;
}

void geode_loudness_finish(geode_loudness* l, geode_loudness_report* out) {
    if (!out) return;
    *out = {};
    if (!l) return;
    const auto r = l->a.finish();
    out->has_integrated = r.hasIntegrated ? 1 : 0;
    out->integrated_lufs = r.integratedLufs;
    out->true_peak_dbtp = r.truePeakDbtp;
    out->sample_peak_dbfs = r.samplePeakDbfs;
    out->measured_ms = r.measuredMs;
    out->gated_block_count = r.gatedBlockCount;
}

}
