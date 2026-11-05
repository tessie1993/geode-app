// Host-side checks for the export resampler (audio/dsp/PcmResampler). Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
// The resampler is a band-limited (windowed-sinc, polyphase) converter since #375; the box filter's
// pinned sample values are gone, and these check what the filter is for instead: a constant passes
// untouched, the band passes at unity, what lies above the output's Nyquist does not fold back, frame
// counts match the old converter's, and chunking never changes a sample.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    cAbiGuards();
    if (failures == 0) std::printf("resampler_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
