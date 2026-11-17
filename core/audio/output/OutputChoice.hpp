#pragma once

#include <bit>

#include "audio/output/OutputFifo.hpp"

namespace geode::audio::output {

// Which of Media3's output configs the native output plays; the platform track plays every other. Values are
// Android's and Media3's own, passed through by the shell: AudioFormat.CHANNEL_OUT_*, C.USAGE_MEDIA and
// C.INDEX_UNSET. No Oboe here, so the host tests reach it.
struct OutputRequest {
    bool offload = false;
    bool tunneling = false;
    bool offloadGapless = false;
    bool playbackParameters = false;  // the config wants speed applied by the output
    int virtualDeviceId = -1;
    int usage = 0;
    int channelMask = 0;
    int encoding = 0;
    int sampleRateHz = 0;
};

inline constexpr int kChannelOutMono = 0x4;
inline constexpr int kChannelOutStereo = 0xC;
inline constexpr int kUsageMedia = 1;
inline constexpr int kIndexUnset = -1;

inline int channelsOfMask(int mask) {
    return std::popcount(static_cast<unsigned>(mask));
}

// Plain 16-bit or float PCM in mono or stereo at a rate the FIFO takes, as media, on no virtual device, with
// no offload, tunneling or speed of its own.
inline bool plays(const OutputRequest& r) {
    return !r.offload && !r.tunneling && !r.offloadGapless && !r.playbackParameters &&
           r.virtualDeviceId == kIndexUnset && r.usage == kUsageMedia &&
           (r.channelMask == kChannelOutMono || r.channelMask == kChannelOutStereo) &&
           OutputFifo::supports(r.encoding, channelsOfMask(r.channelMask), r.sampleRateHz);
}

}  // namespace geode::audio::output
