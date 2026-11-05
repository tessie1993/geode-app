// Host-side checks for the export resampler (audio/dsp/PcmResampler). Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
// The resampler is a band-limited (windowed-sinc, polyphase) converter since #375; the box filter's
// pinned sample values are gone, and these check what the filter is for instead: a constant passes
// untouched, the band passes at unity, what lies above the output's Nyquist does not fold back, frame
// counts match the old converter's, and chunking never changes a sample.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/PcmResampler.hpp"

using geode::audio::PcmResampler;

namespace {

constexpr double kPi = 3.14159265358979323846;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<int16_t> run(PcmResampler& r, const std::vector<int16_t>& in) {
    const size_t frames = in.size() / static_cast<size_t>(r.channels());
    const size_t cap = r.maxOutFrames(frames);
    std::vector<int16_t> out(cap * static_cast<size_t>(r.channels()));
    const size_t got = r.process(in.data(), frames, out.data());
    check(got <= cap, "process stays within maxOutFrames");
    out.resize(got * static_cast<size_t>(r.channels()));
    return out;
}

// One second of mono fed in decoder-sized chunks; returns the output frames.
size_t oneSecondFrames(int from, int to) {
    PcmResampler r(from, to, 1);
    size_t out = 0;
    for (int left = from; left > 0;) {
        const int n = left < 1152 ? left : 1152;
        out += run(r, std::vector<int16_t>(static_cast<size_t>(n), 1000)).size();
        left -= n;
    }
    return out;
}

std::vector<int16_t> sine(int rate, double hz, double amplitude, size_t frames) {
    std::vector<int16_t> s(frames);
    for (size_t i = 0; i < frames; ++i) {
        s[i] = static_cast<int16_t>(std::lrint(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate)));
    }
    return s;
}

// Output level of a tone through from -> to, relative to its input level, in dB. Two seconds in; the
// first and last 4 * halfTaps output frames are skipped (the fade-in from silence, and margin).
double toneGainDb(int from, int to, double hz) {
    constexpr double amplitude = 16000.0;
    PcmResampler r(from, to, 1);
    const auto out = run(r, sine(from, hz, amplitude, static_cast<size_t>(from) * 2));
    const size_t skip = static_cast<size_t>(4 * r.halfTaps());
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = skip; i + skip < out.size(); ++i, ++n) sum += static_cast<double>(out[i]) * out[i];
    const double rms = std::sqrt(sum / static_cast<double>(n > 0 ? n : 1));
    return 20.0 * std::log10((rms + 1e-9) / (amplitude / std::sqrt(2.0)));
}

void constantPassesUntouched() {
    // Stereo, two levels; every output past the fade-in (it reads only real frames) is exact.
    const int pairs[][2] = {{96000, 48000}, {88200, 48000}, {176400, 48000}, {44100, 48000}, {24000, 48000}};
    for (const auto& p : pairs) {
        PcmResampler r(p[0], p[1], 2);
        std::vector<int16_t> in(static_cast<size_t>(p[0]) / 10 * 2);
        for (size_t i = 0; i < in.size(); i += 2) {
            in[i] = 1000;
            in[i + 1] = -20000;
        }
        const auto out = run(r, in);
        // Output k reads from input frame floor(k * in / out) - 2 * half + 1, which is inside the stream
        // once k * in / out >= 2 * half.
        const size_t first = static_cast<size_t>(2 * r.halfTaps()) * static_cast<size_t>(p[1]) / static_cast<size_t>(p[0]) + 1;
        bool exact = out.size() / 2 > 4 * first;
        for (size_t f = first; f < out.size() / 2; ++f) {
            exact = exact && out[2 * f] == 1000 && out[2 * f + 1] == -20000;
        }
        check(exact, "a constant passes untouched");
    }
}

void identityAtEqualRates() {
    PcmResampler r(48000, 48000, 2);
    const std::vector<int16_t> in{1, -2, 3, -4, 32767, -32768};
    check(run(r, in) == in, "48k->48k is a copy");
}

void bandPassesAtUnity() {
    check(std::fabs(toneGainDb(88200, 48000, 1000.0)) < 0.1, "88.2k->48k: 1 kHz within 0.1 dB");
    check(std::fabs(toneGainDb(88200, 48000, 15000.0)) < 0.1, "88.2k->48k: 15 kHz within 0.1 dB");
    check(std::fabs(toneGainDb(96000, 48000, 1000.0)) < 0.1, "96k->48k: 1 kHz within 0.1 dB");
    check(std::fabs(toneGainDb(176400, 48000, 1000.0)) < 0.1, "176.4k->48k: 1 kHz within 0.1 dB");
    check(std::fabs(toneGainDb(44100, 48000, 1000.0)) < 0.1, "44.1k->48k: 1 kHz within 0.1 dB");
    check(std::fabs(toneGainDb(44100, 48000, 15000.0)) < 0.1, "44.1k->48k: 15 kHz within 0.1 dB");
}

// #375: ultrasonic content in a hi-res source must not fold into the band. 30 kHz at 88.2 kHz would
// alias to 18 kHz at 48 kHz; the box filter let it through almost whole. The Kaiser window is designed
// for about 80 dB; 60 dB is asserted to leave room for the 16-bit output's own rounding.
void aboveOutputNyquistIsRemoved() {
    check(toneGainDb(88200, 48000, 30000.0) < -60.0, "88.2k->48k: 30 kHz down by more than 60 dB");
    check(toneGainDb(88200, 48000, 26000.0) < -60.0, "88.2k->48k: 26 kHz down by more than 60 dB");
    check(toneGainDb(96000, 48000, 40000.0) < -60.0, "96k->48k: 40 kHz down by more than 60 dB");
    check(toneGainDb(176400, 48000, 30000.0) < -60.0, "176.4k->48k: 30 kHz down by more than 60 dB");
    check(toneGainDb(192000, 48000, 60000.0) < -60.0, "192k->48k: 60 kHz down by more than 60 dB");
}

void oneSecondStaysOneSecondAtEveryRate() {
    // n frames in give ceil(n * out / in) out, however they were chunked: a second is exactly a second.
    check(oneSecondFrames(88200, 48000) == 48000, "88.2k->48k: 48000 frames");
    check(oneSecondFrames(96000, 48000) == 48000, "96k->48k: 48000 frames");
    check(oneSecondFrames(44100, 48000) == 48000, "44.1k->48k: 48000 frames");
    check(oneSecondFrames(176400, 48000) == 48000, "176.4k->48k: 48000 frames");
    check(oneSecondFrames(176400, 44100) == 44100, "176.4k->44.1k: 44100 frames");
    check(oneSecondFrames(88200, 44100) == 44100, "88.2k->44.1k: 44100 frames");
    check(oneSecondFrames(192000, 48000) == 48000, "192k->48k: 48000 frames");
    check(oneSecondFrames(48000, 48000) == 48000, "48k->48k: 48000 frames");
    // The encoder's bytes are timed at the output rate: a second of 96 kHz stereo is 48000 frames.
    PcmResampler r(96000, 48000, 2);
    check(run(r, std::vector<int16_t>(96000 * 2)).size() == 48000 * 2, "96k->48k stereo: 48000 frames");
}

void chunkedInputMatchesOneBuffer() {
    // A loud, busy signal (two tones and a ramp) so every tap matters.
    std::vector<int16_t> sig(3000);
    for (size_t i = 0; i < sig.size(); ++i) {
        const double t = static_cast<double>(i);
        sig[i] = static_cast<int16_t>(std::lrint(9000.0 * std::sin(t * 0.37) + 7000.0 * std::sin(t * 2.1) +
                                                 static_cast<double>(i % 500) * 20.0 - 5000.0));
    }
    const int pairs[][2] = {{96000, 48000}, {88200, 48000}, {176400, 48000}, {88200, 44100},
                            {44100, 48000}, {24000, 48000}, {192000, 48000}};
    for (const auto& p : pairs) {
        PcmResampler whole(p[0], p[1], 1);
        const auto expected = run(whole, sig);
        PcmResampler chunked(p[0], p[1], 1);
        std::vector<int16_t> pieces;
        size_t at = 0;
        for (size_t size = 1; at < sig.size(); size = size % 37 + 3) {
            const size_t n = size < sig.size() - at ? size : sig.size() - at;
            const auto part = run(chunked, std::vector<int16_t>(sig.begin() + at, sig.begin() + at + n));
            pieces.insert(pieces.end(), part.begin(), part.end());
            at += n;
        }
        check(!expected.empty() && pieces == expected, "chunked input matches one buffer");
    }
}

// Amplitude of the component at hz in y[start, start + n) sampled at rate: one bin of a plain DFT. With a
// whole number of cycles of every tone in the window there is no leakage between them.
double toneAmplitude(const std::vector<int16_t>& y, size_t start, size_t n, int rate, double hz) {
    double re = 0.0;
    double im = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / static_cast<double>(rate);
        re += static_cast<double>(y[start + i]) * std::cos(w);
        im -= static_cast<double>(y[start + i]) * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// Kills: x = m - half_ (dropped +1), x = ... + frac (sign flip), the work_ offset off by one frame, a row
// one phase off (rem / phaseDiv_ + 1), frac = p / (phases_ + 1), a rounded-table index from the wrong rate.
// A band-limited interpolator is linear phase: every tone in the band comes out with one delay, the
// halfTaps() input frames the ABI promises, and otherwise untouched. The expected wave is the input sine
// evaluated at the delayed time, so the oracle is the tone, not the filter.
void everyToneLagsByHalfTapsInputFrames() {
    // 44101 and 96001 share no useful factor with 48000: they take the rounded (capped) phase table.
    const int pairs[][2] = {{96000, 48000}, {88200, 44100}, {192000, 48000}, {88200, 48000}, {176400, 48000},
                            {44100, 48000}, {24000, 48000}, {44101, 48000}, {96001, 48000}};
    constexpr double amplitude = 16000.0;
    for (const auto& p : pairs) {
        const int from = p[0];
        const int to = p[1];
        // 60 % of the lower Nyquist rate: inside the band, below the transition at every pair.
        const double top = 0.6 * 0.5 * static_cast<double>(std::min(from, to));
        for (const double hz : {1000.0, top}) {
            PcmResampler r(from, to, 1);
            const auto out = run(r, sine(from, hz, amplitude, static_cast<size_t>(from / 4)));
            const double delaySeconds = static_cast<double>(r.halfTaps()) / static_cast<double>(from);
            const size_t skip = static_cast<size_t>(8 * r.halfTaps());
            double err = 0.0;
            size_t n = 0;
            for (size_t k = skip; k < out.size(); ++k, ++n) {
                const double t = static_cast<double>(k) / static_cast<double>(to) - delaySeconds;
                const double e = static_cast<double>(out[k]) - amplitude * std::sin(2.0 * kPi * hz * t);
                err += e * e;
            }
            const double rel = n > 0 ? std::sqrt(err / static_cast<double>(n)) / (amplitude / std::sqrt(2.0)) : 1.0;
            // -54 dB of error allowed: the Kaiser design leaves about -80 dB, a one-frame shift about 0 dB.
            check(n > 1000 && rel < 2e-3, "a tone comes out delayed by halfTaps() input frames, otherwise intact");
        }
    }
}

// Kills: half_ from the higher rate's Nyquist, or doubled (the delay leaves the documented 0.5 to 1.1 ms).
void delayIsAboutOneMillisecondInto48k() {
    const int froms[] = {24000, 44100, 88200, 96000, 176400, 192000, 44101};
    for (const int from : froms) {
        PcmResampler r(from, 48000, 2);
        const double ms = 1000.0 * static_cast<double>(r.halfTaps()) / static_cast<double>(from);
        check(ms > 0.4 && ms < 1.2, "into 48 kHz the delay is about 0.5 to 1.1 ms");
    }
    PcmResampler same(48000, 48000, 2);
    check(same.halfTaps() == 0, "equal rates have no delay");
}

// Kills: scale = out / in without the min (going up, a cutoff above the input's Nyquist leaves the spectral
// images), scale = in / out (going up, the band itself is cut). Upsampling a tone at f from rate fs leaves
// an image at fs - f unless the filter removes it.
void upsamplingLeavesNoImages() {
    struct Case {
        int from;
        double hz;
        double image;
    };
    const Case cases[] = {{24000, 8000.0, 16000.0}, {16000, 5000.0, 11000.0}};
    constexpr double amplitude = 16000.0;
    for (const auto& c : cases) {
        PcmResampler r(c.from, 48000, 1);
        const auto out = run(r, sine(c.from, c.hz, amplitude, static_cast<size_t>(c.from)));
        // Half a second at 48 kHz: a whole number of cycles of the tone and of its image.
        const size_t start = static_cast<size_t>(8 * r.halfTaps());
        const size_t n = 24000;
        const bool fits = out.size() >= start + n;
        const double tone = fits ? toneAmplitude(out, start, n, 48000, c.hz) : 0.0;
        const double image = fits ? toneAmplitude(out, start, n, 48000, c.image) : amplitude;
        check(fits && std::fabs(20.0 * std::log10((tone + 1e-9) / amplitude)) < 0.1,
              "upsampling: the tone passes within 0.1 dB");
        check(fits && 20.0 * std::log10((image + 1e-9) / amplitude) < -60.0,
              "upsampling: its image is down more than 60 dB");
    }
}

// Kills: the clamp in toPcm16 removed (a band-limited full-scale square overshoots the rails by several per
// cent, Gibbs, and 34000 would wrap to a negative sample), or narrowed to +-32767 or 32766.
void fullScaleSquareClipsAtTheRails() {
    struct Case {
        int from;
        int to;
        int halfPeriod;  // input frames per half cycle
    };
    const Case cases[] = {{96000, 48000, 96}, {88200, 48000, 100}, {24000, 48000, 60}};
    for (const auto& c : cases) {
        PcmResampler r(c.from, c.to, 1);
        std::vector<int16_t> in(static_cast<size_t>(c.from));
        for (size_t i = 0; i < in.size(); ++i) {
            in[i] = (i / static_cast<size_t>(c.halfPeriod)) % 2 == 0 ? int16_t{32767} : int16_t{-32768};
        }
        const auto out = run(r, in);
        int16_t hi = 0;
        int16_t lo = 0;
        for (const int16_t v : out) {
            hi = std::max(hi, v);
            lo = std::min(lo, v);
        }
        check(hi == 32767 && lo == -32768, "a full-scale square reaches both rails");
        // Ringing stays near the rail, so the sign changes only at the square's edges: every whole run of one
        // sign lasts about a half period of output frames. A wrapped overshoot makes a one-frame run.
        const size_t minRun = static_cast<size_t>(0.8 * c.halfPeriod * c.to / c.from);
        std::vector<size_t> runs;
        int sign = 0;
        size_t length = 0;
        for (size_t k = static_cast<size_t>(4 * r.halfTaps()); k < out.size(); ++k) {
            const int s = out[k] > 0 ? 1 : (out[k] < 0 ? -1 : 0);
            if (s == 0 || s == sign) {
                ++length;
                continue;
            }
            if (sign != 0) runs.push_back(length);
            sign = s;
            length = 1;
        }
        bool longRuns = runs.size() > 20;
        // The first run is cut by where the count started; the last is still open and not in the list.
        for (size_t i = 1; i < runs.size(); ++i) longRuns = longRuns && runs[i] >= minRun;
        check(longRuns, "a full-scale square clips instead of wrapping");
    }
}

// Kills: the channel offset (x + c) dropped, a stride of 1 instead of the channel count, history kept per
// sample instead of per frame. Each channel is filtered alone: silence beside a tone stays silence, and the
// tone's channel is what a mono resampler makes of it.
void stereoChannelsAreIndependent() {
    const int pairs[][2] = {{88200, 48000}, {44100, 48000}, {44101, 48000}};
    for (const auto& p : pairs) {
        const auto tone = sine(p[0], 1000.0, 16000.0, static_cast<size_t>(p[0] / 10));
        PcmResampler mono(p[0], p[1], 1);
        const auto expected = run(mono, tone);
        for (size_t side = 0; side < 2; ++side) {
            std::vector<int16_t> in(tone.size() * 2, 0);
            for (size_t i = 0; i < tone.size(); ++i) in[2 * i + side] = tone[i];
            PcmResampler stereo(p[0], p[1], 2);
            const auto out = run(stereo, in);
            bool same = !expected.empty() && out.size() == expected.size() * 2;
            for (size_t f = 0; same && f < expected.size(); ++f) {
                same = out[2 * f + side] == expected[f] && out[2 * f + (1 - side)] == 0;
            }
            check(same, "stereo: each channel is resampled alone");
        }
    }
}

// Kills: bookkeeping that depends on call boundaries (the wrong tail kept as history, inFrames_ or outFrames_
// moved by the wrong amount, newest > end), in the rounded table too. Full-scale noise in random chunks, empty
// ones included, against one buffer and against the ABI's frame count ceil(n * out / in). Raw mt19937 words
// only, which every standard library produces alike.
void randomChunksMatchOneBufferAndCountFrames() {
    std::mt19937 gen(375u);
    const int pairs[][2] = {{88200, 48000}, {176400, 44100}, {44100, 48000}, {8000, 48000},
                            {48000, 8000}, {44101, 48000}, {96001, 48000}};
    for (const auto& p : pairs) {
        const size_t n = 5000 + static_cast<size_t>(gen() % 20000u);
        std::vector<int16_t> sig(n * 2);
        for (auto& v : sig) v = static_cast<int16_t>(gen() & 0xffffu);
        PcmResampler whole(p[0], p[1], 2);
        const auto expected = run(whole, sig);
        PcmResampler chunked(p[0], p[1], 2);
        std::vector<int16_t> pieces;
        for (size_t at = 0; at < n;) {
            const size_t size = std::min(static_cast<size_t>(gen() % 3001u), n - at);
            const auto first = sig.begin() + static_cast<std::ptrdiff_t>(2 * at);
            const auto part = run(chunked, std::vector<int16_t>(first, first + static_cast<std::ptrdiff_t>(2 * size)));
            pieces.insert(pieces.end(), part.begin(), part.end());
            at += size;
        }
        const uint64_t inRate = static_cast<uint64_t>(p[0]);
        const uint64_t outRate = static_cast<uint64_t>(p[1]);
        const size_t frames = static_cast<size_t>((static_cast<uint64_t>(n) * outRate + inRate - 1) / inRate);
        check(expected.size() == frames * 2, "n frames in give ceil(n * out / in) out");
        check(pieces == expected, "random chunks give the same samples as one buffer");
    }
}

// Kills: a row left unnormalised (or only some rows normalised), likeliest to show in the 4096-row rounded
// table and at the extreme ratios. DC gain is one: every constant, the rails included, comes out unchanged.
void dcGainIsOneAtEveryRatio() {
    const int pairs[][2] = {{192000, 48000}, {48000, 8000}, {8000, 48000}, {88200, 44100},
                            {44101, 48000}, {96001, 48000}};
    const int16_t levels[] = {32767, -32768, 12345, -1};
    for (const auto& p : pairs) {
        for (const int16_t level : levels) {
            PcmResampler r(p[0], p[1], 1);
            const auto out = run(r, std::vector<int16_t>(static_cast<size_t>(p[0]), level));
            // Past the fade-in every tap reads a real frame (as in constantPassesUntouched).
            const size_t first =
                static_cast<size_t>(2 * r.halfTaps()) * static_cast<size_t>(p[1]) / static_cast<size_t>(p[0]) + 1;
            bool exact = out.size() > 4 * first;
            for (size_t k = first; exact && k < out.size(); ++k) exact = out[k] == level;
            check(exact, "DC gain is exactly one");
        }
    }
}

// The C ABI the JNI layer goes through refuses what would overrun a buffer instead of writing it.
void cAbiGuards() {
    check(geode_resampler_create(0, 48000, 2) == nullptr, "abi: zero rate refused");
    check(geode_resampler_create(96000, 48000, 0) == nullptr, "abi: zero channels refused");
    check(geode_resampler_channels(nullptr) == 0, "abi: null has no channels");
    geode_resampler* r = geode_resampler_create(96000, 48000, 2);
    check(r != nullptr && geode_resampler_channels(r) == 2, "abi: channels as configured");
    std::vector<int16_t> in(400 * 2);
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<int16_t>(static_cast<int>(i % 97) * 300 - 14000);
    std::vector<int16_t> out(400 * 2, 0);
    const size_t need = geode_resampler_max_out_frames(r, 400);
    check(need == 202, "abi: 400 frames at 2:1 may give up to 202 out");
    check(geode_resampler_process(r, in.data(), 400, out.data(), need - 1) == -1, "abi: short output buffer refused");
    bool untouched = true;
    for (int16_t v : out) untouched = untouched && v == 0;
    check(untouched, "abi: refused call wrote nothing");
    // The refused call consumed nothing, so the stream is still where a fresh one starts.
    PcmResampler fresh(96000, 48000, 2);
    const auto expected = run(fresh, in);
    check(geode_resampler_process(r, in.data(), 400, out.data(), need) == 200 &&
              std::vector<int16_t>(out.begin(), out.begin() + 400) == expected,
          "abi: state untouched by refusal");
    check(geode_resampler_process(nullptr, in.data(), 400, out.data(), need) == 0, "abi: null handle writes nothing");
    geode_resampler_destroy(r);
    geode_resampler_destroy(nullptr);
}

}  // namespace

int main() {
    constantPassesUntouched();
    identityAtEqualRates();
    bandPassesAtUnity();
    aboveOutputNyquistIsRemoved();
    oneSecondStaysOneSecondAtEveryRate();
    chunkedInputMatchesOneBuffer();
    everyToneLagsByHalfTapsInputFrames();
    delayIsAboutOneMillisecondInto48k();
    upsamplingLeavesNoImages();
    fullScaleSquareClipsAtTheRails();
    stereoChannelsAreIndependent();
    randomChunksMatchOneBufferAndCountFrames();
    dcGainIsOneAtEveryRatio();
    cAbiGuards();
    if (failures == 0) std::printf("resampler_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
