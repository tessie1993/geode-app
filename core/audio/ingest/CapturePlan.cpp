#include "audio/ingest/CapturePlan.hpp"

#include <cstdint>
#include <initializer_list>
#include <limits>

namespace geode::audio::ingest {

int capturePlan(int deviceRateHz, bool stereo, CaptureFormat* out, int capacity) {
    int rates[1 + sizeof kCaptureFallbackRates / sizeof kCaptureFallbackRates[0]];
    int rateCount = 0;
    if (deviceRateHz > 0) rates[rateCount++] = deviceRateHz;
    for (int fallback : kCaptureFallbackRates) {
        if (fallback != deviceRateHz) rates[rateCount++] = fallback;
    }
    const int channelCounts[] = {2, 1};
    const int firstChannels = stereo ? 0 : 1;
    int count = 0;
    for (int r = 0; r < rateCount; ++r) {
        for (int c = firstChannels; c < 2; ++c) {
            for (bool asFloat : {true, false}) {
                if (out && count < capacity) out[count] = CaptureFormat{rates[r], channelCounts[c], asFloat};
                ++count;
            }
        }
    }
    return count;
}

int captureBufferBytes(int minBufferBytes, int channels) {
    if (minBufferBytes <= 0 || channels <= 0) return 0;
    const int64_t scaled = static_cast<int64_t>(minBufferBytes) * kCaptureBufferMinimums;
    const int64_t oneRead = static_cast<int64_t>(kCaptureReadFrames) * channels * 4;
    const int64_t bytes = scaled > oneRead ? scaled : oneRead;
    constexpr int64_t kMax = std::numeric_limits<int>::max();
    return static_cast<int>(bytes < kMax ? bytes : kMax);
}

int captureReadBytes(int encoding, int channels) {
    if (channels <= 0) return 0;
    int width = 0;
    switch (encoding) {
        case 2: width = 2; break;  // 16-bit
        case 3: width = 1; break;  // 8-bit
        case 4: width = 4; break;  // float
        default: return 0;
    }
    return kCaptureReadFrames * channels * width;
}

}  // namespace geode::audio::ingest
