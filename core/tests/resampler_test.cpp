// Host-side checks for the export resampler (audio/dsp/PcmResampler). Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
// The expected values are the ones the Kotlin PcmResamplerTest pinned before the port.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "audio/dsp/PcmResampler.hpp"

using geode::audio::PcmResampler;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<int16_t> run(PcmResampler& r, const std::vector<int16_t>& in) {
    const size_t frames = in.size() / static_cast<size_t>(r.channels());
    std::vector<int16_t> out(r.maxOutFrames(frames) * static_cast<size_t>(r.channels()));
    out.resize(r.process(in.data(), frames, out.data()) * static_cast<size_t>(r.channels()));
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

void downsamplingByTwoAveragesPairs() {
    PcmResampler r(96000, 48000, 2);
    check(run(r, {0, 100, 10, 200, 20, 300, 30, 400}) == std::vector<int16_t>{5, 150, 25, 350}, "96k->48k averages pairs");
}

void upsamplingInterpolatesBetweenSamples() {
    PcmResampler r(24000, 48000, 1);
    check(run(r, {0, 100, 200, 300}) == std::vector<int16_t>{0, 50, 100, 150, 200, 250}, "24k->48k interpolates");
}

void identityAtEqualRates() {
    PcmResampler r(48000, 48000, 2);
    const std::vector<int16_t> in{1, -2, 3, -4, 32767, -32768};
    check(run(r, in) == in, "48k->48k is a copy");
}

void oneSecondStaysOneSecondAtEveryRate() {
    const int pairs[][2] = {{96000, 48000}, {192000, 48000}, {176400, 44100}, {44100, 48000}, {48000, 48000}};
    for (const auto& p : pairs) {
        const long out = static_cast<long>(oneSecondFrames(p[0], p[1]));
        check(std::labs(out - p[1]) <= 1, "one second stays one second");
    }
}

void wholeStepsDownAreExact() {
    check(oneSecondFrames(96000, 48000) == 48000, "96k->48k: 48000 frames");
    check(oneSecondFrames(88200, 44100) == 44100, "88.2k->44.1k: 44100 frames");
    check(oneSecondFrames(192000, 48000) == 48000, "192k->48k: 48000 frames");
    // The encoder's bytes are timed at the output rate: a second of 96 kHz stereo is 48000 frames.
    PcmResampler r(96000, 48000, 2);
    check(run(r, std::vector<int16_t>(96000 * 2)).size() == 48000 * 2, "96k->48k stereo: 48000 frames");
}

void chunkedInputMatchesOneBuffer() {
    std::vector<int16_t> ramp(1000);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<int16_t>(i * 7);
    const int pairs[][2] = {{96000, 48000}, {88200, 44100}, {44100, 48000}, {24000, 48000}, {192000, 48000}};
    for (const auto& p : pairs) {
        PcmResampler whole(p[0], p[1], 1);
        const auto expected = run(whole, ramp);
        PcmResampler chunked(p[0], p[1], 1);
        std::vector<int16_t> pieces;
        size_t at = 0;
        for (size_t size = 1; at < ramp.size(); size = size % 37 + 3) {
            const size_t n = size < ramp.size() - at ? size : ramp.size() - at;
            const auto part = run(chunked, std::vector<int16_t>(ramp.begin() + at, ramp.begin() + at + n));
            pieces.insert(pieces.end(), part.begin(), part.end());
            at += n;
        }
        check(pieces == expected, "chunked input matches one buffer");
    }
}

}  // namespace

int main() {
    downsamplingByTwoAveragesPairs();
    upsamplingInterpolatesBetweenSamples();
    identityAtEqualRates();
    oneSecondStaysOneSecondAtEveryRate();
    wholeStepsDownAreExact();
    chunkedInputMatchesOneBuffer();
    if (failures == 0) std::printf("resampler_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
