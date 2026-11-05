#pragma once
// Muxer budget (#774): the video bit rate that keeps a rendered MP4 inside what Android's MediaMuxer can
// write. MediaMuxer's MPEG-4 writer records 32-bit chunk offsets, so a file that grows past four gigabytes
// is indexed wrong and will not play (Kotlin's LoopExtend.MUXER_SAFE_BYTES). The quality presets fix the
// video rate (40 Mbit/s for 4K), and at that rate a track longer than about twelve minutes crosses the line;
// 1080p crosses it at about forty. The render is short of nothing but bytes, so rather than fail an hour of
// GPU work at the end, the encoder is asked up front for a rate just low enough that video plus audio fit
// under kMuxerSafeBytes.
//
// Ported unchanged from Kotlin's export/MuxerBudget: same int64 maths, same truncating division, same
// 32-bit narrowing. Where Kotlin's Long arithmetic would wrap (only for a limit or a duration no real render
// has), this wraps the same way instead of being undefined, so every input gives the old answer.
#include <cstdint>

namespace geode::output {

/**
 * The size a render aims under. Well below the 4 GiB ceiling (about 490 MB of headroom, 11%) because a
 * hardware encoder's bit rate is a target, not a promise: VBR encoders overshoot on busy frames, and the
 * container's own index and headers sit on top.
 */
inline constexpr int64_t kMuxerSafeBytes = 3'800'000'000LL;

/**
 * The least the cap goes to. Below this a 4K frame is mush, and a track long enough to need less (roughly
 * four hours) is better refused by the muxer than rendered unwatchable.
 */
inline constexpr int32_t kMuxerFloorBps = 2'000'000;

/** The AAC rate the export's AudioTranscoder encodes at, for callers that have not transcoded yet. */
inline constexpr int32_t kExportAacBps = 192'000;

namespace muxer_detail {
inline constexpr int64_t kUsPerSecond = 1'000'000;
inline constexpr int64_t kBitsPerByte = 8;
// Two's-complement wrapping, as the JVM's Long does; signed overflow would be undefined in C++.
constexpr int64_t wrapMul(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}
constexpr int64_t wrapSub(int64_t a, int64_t b) {
    return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
}  // namespace muxer_detail

/**
 * requestedBps, or less when durationUs of video at that rate plus audioBytes would pass limitBytes. Never
 * above requestedBps and never below kMuxerFloorBps (unless the request itself is lower). A duration that is
 * not known yet (zero or less), or a request of zero or less, leaves the request alone. The fitting rate is
 * truncated: (limit - audio) * 8 * 1e6 / durationUs, in int64, then narrowed to 32 bits.
 */
constexpr int32_t cappedVideoBps(int32_t requestedBps, int64_t durationUs, int64_t audioBytes,
                                 int64_t limitBytes = kMuxerSafeBytes) {
    using namespace muxer_detail;
    if (durationUs <= 0 || requestedBps <= 0) return requestedBps;
    const int64_t audio = audioBytes < 0 ? 0 : audioBytes;
    int64_t videoBytes = wrapSub(limitBytes, audio);
    if (videoBytes < 0) videoBytes = 0;
    const int64_t fittingBps = wrapMul(wrapMul(videoBytes, kBitsPerByte), kUsPerSecond) / durationUs;
    if (fittingBps >= requestedBps) return requestedBps;
    const auto narrowed = static_cast<int32_t>(fittingBps);   // modular since C++20, as Kotlin's toInt()
    const int32_t floor = kMuxerFloorBps < requestedBps ? kMuxerFloorBps : requestedBps;
    return narrowed < floor ? floor : narrowed;
}

/** What durationUs of audio at bps takes, truncated: max(durationUs, 0) * bps / 8e6. */
constexpr int64_t audioBytes(int64_t durationUs, int32_t bps = kExportAacBps) {
    using namespace muxer_detail;
    const int64_t d = durationUs < 0 ? 0 : durationUs;
    return wrapMul(d, bps) / (kBitsPerByte * kUsPerSecond);
}

}  // namespace geode::output
