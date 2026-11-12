#pragma once
#include <cstdint>

namespace geode::audio {

// Sample-clock maths on the export's way into the AAC encoder (#421): which rate the encoder gets, how long
// a run of s16 bytes lasts, where a start time falls in the source, and how a decoded buffer is trimmed to
// a start. Ported from the Kotlin AudioTranscoder and PcmResampler's companion; the MediaCodec calls and the
// buffer positions stay Kotlin, every number they move by comes from here. Pure, integer only, any thread.

// The highest rate every Android AAC-LC encoder accepts, and the 44.1 kHz family's base.
inline constexpr int kAacMaxRateHz = 48000;
inline constexpr int kCdRateHz = 44100;
// A mix's clips are all brought to this, so they can share one AAC track.
inline constexpr int kMixRateHz = kAacMaxRateHz;

// The AAC rate for a source: its own rate up to 48 kHz; above it 44.1 kHz for the 44.1k family (88.2,
// 176.4 kHz) and 48 kHz for the rest, so the step down is a whole number and the resampler's box filter is
// exact. A rate <= 48 kHz, a bad one included, comes back unchanged (the encoder refuses a bad one, which is
// the honest failure).
int aacRateFor(int sourceRateHz);

// Duration of `bytes` of interleaved s16 at `rateHz` (the rate the bytes are at, not the source's), floor.
// 0 for a rate or channel count <= 0.
int64_t s16BytesToUs(int64_t bytes, int rateHz, int channels);

// Interleaved samples (frames * channels) that lie before `us` at `rateHz`: floor(us * rate / 1e6) frames.
// The AIFF path reads and discards these to start at a time. 0 for a time <= 0 or a bad rate.
int64_t samplesBeforeUs(int64_t us, int rateHz, int channels);

// End of an AAC track: the last access unit's pts plus one AAC-LC frame (1024 samples) at the encoder's
// actual output rate (23.2 ms at 44.1 kHz, 21.3 ms at 48 kHz, not a fixed 24 ms), integer division.
int64_t aacEndUs(int64_t lastPtsUs, int sampleRateHz);

// One decoded buffer trimmed to the render's start.
struct PcmTrim {
    // Bytes to drop from the buffer's front, a whole number of frames; equal to the buffer's size when
    // nothing of it is kept (it ends before the start).
    int64_t skipBytes = 0;
    // The pts the kept part is queued at, on the render's clock (0 = the start).
    int64_t ptsUs = 0;
};

// A decoder seeks to the sync sample before the start, so its first buffers can begin before `startUs`.
// The frames before the start (rounded to the nearest frame) are dropped and the rest is stamped from 0;
// a buffer at or after the start keeps everything and is stamped pts - start. The Kotlin rule dropped a
// buffer that straddled the start whole (when it began more than 1 ms early), losing up to one decoder
// buffer of audio and leaving a gap at the head of the export; this keeps the part after the start.
// `bytes` is the buffer's s16 size at `channels` (after any downmix) and `rateHz` the decoded rate.
PcmTrim trimS16(int64_t startUs, int64_t bufferPtsUs, int64_t bytes, int rateHz, int channels);

}  // namespace geode::audio
