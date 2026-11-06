#include "audio/dsp/PcmTiming.hpp"

namespace geode::audio {

namespace {
constexpr int64_t kMicrosPerSecond = 1000000;
constexpr int64_t kBytesPerSample = 2;
constexpr int64_t kAacFrameSamples = 1024;
}  // namespace

int aacRateFor(int sourceRateHz) {
    if (sourceRateHz <= kAacMaxRateHz) return sourceRateHz;
    return sourceRateHz % kCdRateHz == 0 ? kCdRateHz : kAacMaxRateHz;
}

int64_t s16BytesToUs(int64_t bytes, int rateHz, int channels) {
    if (rateHz <= 0 || channels <= 0) return 0;
    return bytes * kMicrosPerSecond / (static_cast<int64_t>(rateHz) * channels * kBytesPerSample);
}

int64_t samplesBeforeUs(int64_t us, int rateHz, int channels) {
    if (us <= 0 || rateHz <= 0 || channels <= 0) return 0;
    return us * rateHz / kMicrosPerSecond * channels;
}

int64_t aacEndUs(int64_t lastPtsUs, int sampleRateHz) {
    if (sampleRateHz <= 0) return lastPtsUs;
    return lastPtsUs + kAacFrameSamples * kMicrosPerSecond / sampleRateHz;
}

PcmTrim trimS16(int64_t startUs, int64_t bufferPtsUs, int64_t bytes, int rateHz, int channels) {
    PcmTrim t;
    if (bytes < 0) bytes = 0;
    const int64_t lead = startUs - bufferPtsUs;
    if (lead <= 0) {
        t.ptsUs = -lead;
        return t;
    }
    // The buffer starts early: whatever is kept begins at the start itself, to the nearest frame, so ptsUs
    // stays 0. Without a usable layout there is no frame to cut at, so nothing is kept.
    if (rateHz <= 0 || channels <= 0) {
        t.skipBytes = bytes;
        return t;
    }
    const int64_t frameBytes = kBytesPerSample * channels;
    const int64_t frames = bytes / frameBytes;
    const int64_t skipFrames = (lead * rateHz + kMicrosPerSecond / 2) / kMicrosPerSecond;
    t.skipBytes = skipFrames >= frames ? bytes : skipFrames * frameBytes;
    return t;
}

}  // namespace geode::audio
