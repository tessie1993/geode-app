#include "output/LoudnessScan.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>

#include "audio/dsp/PcmConvert.hpp"

namespace geode::output::loudness {

namespace {

using audio::LoudnessAnalyser;
using audio::ingest::DecodedChunk;

// Samples converted at a time: a whole number of frames, so a feed's trailing partial frame (which the meter
// drops) can only be the buffer's own.
constexpr size_t kBlock = 4096;

// One decoder buffer into the meter. Copied out first: a codec's buffer need not be aligned for its samples.
void feed(LoudnessAnalyser& meter, const DecodedChunk& chunk) {
    const size_t width = chunk.isFloat ? sizeof(float) : sizeof(int16_t);
    const size_t samples = chunk.bytes / width;
    const auto channels = static_cast<size_t>(meter.channels());
    const size_t step = kBlock - kBlock % channels;
    float block[kBlock];
    int16_t shorts[kBlock];
    for (size_t at = 0; at < samples; at += step) {
        const size_t n = samples - at < step ? samples - at : step;
        if (chunk.isFloat) {
            std::memcpy(block, chunk.data + at * width, n * width);
        } else {
            std::memcpy(shorts, chunk.data + at * width, n * width);
            audio::s16ToFloat(shorts, n, block);
        }
        meter.feed(block, n);
    }
}

Scan run(audio::ingest::DecodePort& port) {
    std::unique_ptr<LoudnessAnalyser> meter;
    int rate = 0;
    int channels = 0;
    bool inputDone = false;
    int stalled = 0;
    for (;;) {
        if (!inputDone) inputDone = port.feedInput();
        DecodedChunk chunk;
        if (!port.takeOutput(chunk)) {
            if (++stalled > kStallLimit) return {};
            continue;
        }
        stalled = 0;
        if (chunk.bytes > 0) {
            if (!meter) {
                if (chunk.sampleRate <= 0 || !LoudnessAnalyser::supports(chunk.channels)) {
                    port.releaseOutput();
                    return {};
                }
                rate = chunk.sampleRate;
                channels = chunk.channels;
                meter = std::make_unique<LoudnessAnalyser>(rate, channels);
            } else if (chunk.sampleRate != rate || chunk.channels != channels) {
                // Blocks measured at two rates would quietly bias the gated mean; a wrong number is worse than none.
                port.releaseOutput();
                return {};
            }
            feed(*meter, chunk);
        }
        port.releaseOutput();
        if (chunk.endOfStream) break;
    }
    if (!meter) return {};
    if (!meter->hasCompleteBlock()) return {ScanEnd::kTooShort, {}};
    return {ScanEnd::kMeasured, meter->finish()};
}

}  // namespace

Scan scan(audio::ingest::DecodePort& port) {
    try {
        return run(port);
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace geode::output::loudness
