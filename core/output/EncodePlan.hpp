#pragma once
#include <cstdint>

namespace geode::output {

// What an export asks the encoder for (ADR 0006, core/output): the frame size, the frame-rate retry and
// the bit rate that keeps the file inside what MediaMuxer can write. Ported from the Kotlin MuxerBudget,
// ExportAspect.of and fallbackExportFps; MediaCodec configuration stays Kotlin and calls this.
namespace encode {

// MediaMuxer writes 32-bit chunk offsets: past 4 GiB a file is indexed wrong and does not play. Two numbers
// guard that, for two jobs, kept side by side here so they cannot drift apart:
//   kMuxerHardLimitBytes: where a long-form loop copy stops writing (LoopExtend), 64 MiB under the line for
//     the moov index and headers written after the last sample.
//   kTargetBytes: what a render's bit rate aims under (MuxerBudget), 11 % lower again, because a hardware
//     encoder's rate is a target, not a promise, and VBR overshoots on busy frames.
constexpr int64_t kMuxerHardLimitBytes = 4LL * 1024 * 1024 * 1024 - 64LL * 1024 * 1024;
constexpr int64_t kTargetBytes = 3'800'000'000LL;
static_assert(kTargetBytes < kMuxerHardLimitBytes, "the bit-rate target must sit under the hard stop");

// The least the cap goes to; below it a 4K frame is mush.
constexpr int kFloorBps = 2'000'000;
// The AAC rate the export transcodes at.
constexpr int kAacBps = 192'000;
// The longest side H.264 level 5.1 encoders take.
constexpr int kMaxAvcDim = 4096;
constexpr int kMinFps = 24;
constexpr int kMaxFps = 60;

// requestedBps, or less when durationUs of video at that rate plus audioBytes would pass limitBytes. Never
// above the request, never below kFloorBps (unless the request is lower). An unknown duration (<= 0) or a
// non-positive request is returned unchanged.
int cappedVideoBps(int requestedBps, int64_t durationUs, int64_t audioBytes, int64_t limitBytes = kTargetBytes);

// The bytes durationUs of audio at bps takes, for estimating before the audio exists.
int64_t audioBytes(int64_t durationUs, int bps = kAacBps);

struct FrameSize {
    int width = 0;
    int height = 0;
};

// The encoded frame for a quality's short side and an aspect ratio: the long side follows the ratio, both
// shrink together when the long side passes kMaxAvcDim, and each is rounded up to even.
FrameSize frameSize(int shortSide, int wRatio, int hRatio);

// The rate an export retries at when the encoder refuses the request: half of a 50 or 60, so the retry
// stays in the request's cadence family (50 -> 25), the request itself at 30 and below.
int fallbackFps(int requestedFps);

// A requested rate clamped into what the export offers.
int clampFps(int requestedFps);

}  // namespace encode
}  // namespace geode::output
