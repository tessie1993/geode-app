#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace geode::audio {

// Sample-format and channel conversions on the export's way into the AAC encoder and the loudness meter
// (ADR 0006: int16 <-> float belongs in native ingest). Ported from the Kotlin AudioTranscoder (downmix,
// float decoder output to s16, written twice there, once per decoder path) and LoudnessMeter.readSigned16.
// MediaCodec buffers stay Kotlin; the loops over their samples move here. Pure, any thread.

// Interleaved s16 from srcChannels to dstChannels (1 or 2), writing frames * dstChannels samples to out:
//   mono: the integer mean of every channel (truncating, as Kotlin's Int division does);
//   stereo: front left and right plus half the mean of the remaining channels (centre, LFE, surrounds),
//   saturated to s16. Returns the frames written, 0 when the layout is not a downmix (src <= dst, dst not
//   1 or 2) or a pointer is null.
size_t downmixS16(const int16_t* in, size_t frames, int srcChannels, int dstChannels, int16_t* out);

// Float PCM to s16: clamp to -1..1, scale by 32767, truncate toward zero; NaN becomes 0 (as Kotlin's toInt).
void floatToS16(const float* in, size_t samples, int16_t* out);

// One float sample to s16 for Cast's encoder: clamp to -1..1, scale by 32767 and round (floatToS16
// truncates); NaN becomes 0. The ring's 16-bit reads (geode_pcm_cursor_take_s16) and Cast's pump share it.
inline int16_t roundToS16(float v) {
    if (std::isnan(v)) return 0;
    const float clamped = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int16_t>(std::lrintf(clamped * 32767.0f));
}

// s16 to float by 1/32768, the loudness meter's scale.
void s16ToFloat(const int16_t* in, size_t samples, float* out);

}  // namespace geode::audio
