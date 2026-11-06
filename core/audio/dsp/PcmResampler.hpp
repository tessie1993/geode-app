#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geode::audio {

// Streaming rate converter for interleaved 16-bit PCM on its way into the AAC encoder (export only).
//
// Band-limited interpolation (Smith and Gossett, https://ccrma.stanford.edu/~jos/resample/): each output
// frame is a Kaiser-windowed sinc sum over the input frames around its position. The sinc's cutoff is
// kRolloff of the lower Nyquist rate, so going down (88.2 or 176.4 kHz into a 48 kHz mix, #375) nothing
// above the output's Nyquist folds back into the band, and going up no images are left above the input's.
// The old box filter (an average of the inputs inside each output frame) did neither at a fractional
// ratio: at 88.2 -> 48 kHz some output frames averaged a single input, so ultrasonic content aliased.
//
// The taps for every fractional position are precomputed in one polyphase table of out / gcd(in, out)
// rows, which is exact for every pair of standard rates (80 rows for 88.2 -> 48 kHz, 160 for 44.1 -> 48
// kHz, one for the 2x and 4x steps); a pair that would need more rows rounds each position to the nearest
// of a capped set. Every row is normalised to unity gain at DC.
//
// The filter is causal: output frame k sits halfTaps() input frames before position k * in / out, so it
// is written as soon as input frame floor(k * in / out) has arrived. The output therefore counts frames
// like the box filter did (ceil(n * out / in) frames after n in, a second in is a second out, however the
// input was chunked); the price is a delay of halfTaps() input frames (0.5 to 1 ms) and a fade-in from
// the silence before the first frame. State (the last 2 * halfTaps() input frames) carries across
// process() calls, so a decoder's buffers can be fed as they arrive and chunking never changes a sample.
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

    // Input frames on each side of an output frame's position: the filter is 2 * halfTaps() long, and
    // the output lags the input by halfTaps() input frames. 0 at equal rates (a plain copy).
    int halfTaps() const { return half_; }

    // Zero crossings of the sinc on each side, counted at the lower rate: sets the transition band
    // (about 4.6 kHz wide around the cutoff for a 48 kHz output).
    static constexpr int kZeroCrossings = 24;
    // Cutoff as a share of the lower Nyquist rate (22.08 kHz for a 48 kHz output).
    static constexpr double kRolloff = 0.92;
    // Kaiser beta for about 80 dB of stopband (0.1102 * (80 - 8.7)).
    static constexpr double kKaiserBeta = 7.857;
    // Most polyphase rows a table may hold before positions are rounded to the nearest row.
    static constexpr int64_t kMaxPhases = 4096;

private:
    int inRate_;
    int outRate_;
    int channels_;
    int half_ = 0;            // input frames on each side of an output position
    int64_t phases_ = 1;      // table_ holds phases_ + 1 rows: row p is the fractional position p / phases_
    int64_t phaseDiv_ = 1;    // exact table: (k * in) % out / phaseDiv_ is output k's row
    bool exact_ = true;
    std::vector<float> table_;  // (phases_ + 1) rows of 2 * half_ taps, oldest input first
    int64_t inFrames_ = 0;
    int64_t outFrames_ = 0;
    std::vector<float> work_;   // the last 2 * half_ input frames, then this call's input, as float
};

}  // namespace geode::audio
