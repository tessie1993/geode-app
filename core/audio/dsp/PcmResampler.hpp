#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geode::audio {

// Streaming rate converter for interleaved 16-bit PCM on its way into the AAC encoder (export only).
//
// Downsampling averages every input frame that falls inside an output frame (a box filter: exact for
// the 2x and 4x steps hi-res sources take, and enough of a low-pass that 96 kHz content does not alias
// audibly). Upsampling interpolates linearly. Neither is a mastering resampler; both beat an encoder that
// refuses to configure or a soundtrack that plays at the wrong speed.
//
// State carries across process() calls, so a decoder's buffers can be fed as they arrive. Ported
// bit-for-bit from the Kotlin PcmResampler: export audio did not change with the port, which is why the
// upsampler keeps its float maths and the file turns FP contraction off (an FMA rounds differently).
// The playback Resampler (player/, Hermite on float) is a different filter and must not replace it.
class PcmResampler {
public:
    PcmResampler(int inRateHz, int outRateHz, int channels);

    int inRateHz() const { return inRate_; }
    int outRateHz() const { return outRate_; }
    int channels() const { return channels_; }

    // Output frames process() may write for `frames` input frames; size `out` for this.
    size_t maxOutFrames(size_t frames) const;

    // Converts `frames` interleaved frames; returns the frames written to `out`, which must hold
    // maxOutFrames(frames) * channels samples.
    size_t process(const int16_t* in, size_t frames, int16_t* out);

private:
    size_t down(const int16_t* in, size_t frames, int16_t* out);
    size_t up(const int16_t* in, size_t frames, int16_t* out);

    int inRate_;
    int outRate_;
    int channels_;
    int64_t inFrames_ = 0;
    int64_t outFrames_ = 0;
    std::vector<int64_t> sums_;  // downsampling: running sums for the output frame being built
    int summed_ = 0;
    std::vector<int16_t> last_;  // upsampling: the previous call's last input frame
};

}  // namespace geode::audio
