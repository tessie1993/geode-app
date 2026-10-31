// Host-side checks for the BS.1770 meter (audio/dsp/Loudness). Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
// References: EBU Tech 3341 (a 997 Hz stereo sine at -23 dBFS reads -23.0 LUFS) and BS.1770 itself
// (a full-scale mono sine near 1 kHz reads -3.01 LUFS).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/Loudness.hpp"

using geode::audio::LoudnessAnalyser;
using geode::audio::LoudnessReport;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

constexpr double kPi = 3.141592653589793;

// `seconds` of an interleaved sine at `dbfs` peak on every channel.
std::vector<float> sine(int rate, int channels, double hz, double dbfs, double seconds, double phase = 0.0) {
    const double amplitude = std::pow(10.0, dbfs / 20.0);
    const auto frames = static_cast<size_t>(rate * seconds);
    std::vector<float> out(frames * static_cast<size_t>(channels));
    for (size_t f = 0; f < frames; ++f) {
        const auto v = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(f) / rate + phase));
        for (int c = 0; c < channels; ++c) out[f * channels + c] = v;
    }
    return out;
}

LoudnessReport measure(int rate, int channels, const std::vector<float>& pcm) {
    LoudnessAnalyser a(rate, channels);
    a.feed(pcm.data(), pcm.size());
    return a.finish();
}

void silenceHasNoIntegratedLoudness() {
    LoudnessAnalyser a(48000, 2);
    const std::vector<float> zeros(48000 * 2 * 2);
    a.feed(zeros.data(), zeros.size());
    // Digital silence records no block at all (its loudness is minus infinity), so the meter reports
    // the file as too short to gate, and finish() has no integrated value.
    check(!a.hasCompleteBlock(), "silence: no gateable block");
    const auto r = a.finish();
    check(!r.hasIntegrated, "silence: no integrated loudness");
    check(r.samplePeakDbfs == -120.0 && r.truePeakDbtp == -120.0, "silence: peaks floor at -120 dB");
    check(r.measuredMs == 2000, "silence: two seconds measured");
}

void quietSignalIsBelowTheAbsoluteGate() {
    LoudnessAnalyser a(48000, 1);
    const auto pcm = sine(48000, 1, 997.0, -80.0, 2.0);
    a.feed(pcm.data(), pcm.size());
    check(a.hasCompleteBlock(), "-80 dBFS: blocks exist");
    const auto r = a.finish();
    check(!r.hasIntegrated && r.gatedBlockCount == 0, "-80 dBFS: every block under -70 LUFS");
}

void ebuReferenceSines() {
    for (const int rate : {48000, 44100}) {
        const auto stereo = measure(rate, 2, sine(rate, 2, 997.0, -23.0, 10.0));
        check(stereo.hasIntegrated && near(stereo.integratedLufs, -23.0, 0.1), "stereo -23 dBFS sine reads -23 LUFS");
        const auto mono = measure(rate, 1, sine(rate, 1, 997.0, 0.0, 10.0));
        check(mono.hasIntegrated && near(mono.integratedLufs, -3.01, 0.1), "mono full-scale sine reads -3.01 LUFS");
        check(near(mono.samplePeakDbfs, 0.0, 0.01), "mono full-scale sine: sample peak 0 dBFS");
        check(stereo.measuredMs == 10000, "ten seconds measured");
    }
}

void truePeakFindsInterSamplePeaks() {
    // fs/4 at 45 degrees: every stored sample is +-0.707, but the waveform between them reaches 1.0.
    const auto r = measure(48000, 1, sine(48000, 1, 12000.0, 0.0, 1.0, kPi / 4.0));
    check(near(r.samplePeakDbfs, -3.01, 0.05), "fs/4 sine: sample peak -3.01 dBFS");
    check(r.truePeakDbtp >= r.samplePeakDbfs, "true peak never under sample peak");
    check(near(r.truePeakDbtp, 0.0, 0.5), "fs/4 sine: true peak near 0 dBTP");
}

void relativeGateDropsTheQuietTail() {
    auto pcm = sine(48000, 2, 997.0, -23.0, 10.0);
    const auto tail = sine(48000, 2, 997.0, -50.0, 10.0);
    pcm.insert(pcm.end(), tail.begin(), tail.end());
    const auto r = measure(48000, 2, pcm);
    // Ungated, the mean would sit near -26 LUFS; the tail falls under the -36 relative gate.
    check(r.hasIntegrated && near(r.integratedLufs, -23.0, 0.1), "relative gate keeps the loud part");
    check(r.gatedBlockCount > 90 && r.gatedBlockCount < 110, "relative gate drops the quiet blocks");
}

void oddChunksMatchOneChunk() {
    const auto pcm = sine(44100, 2, 440.0, -12.0, 3.0);
    const auto whole = measure(44100, 2, pcm);
    LoudnessAnalyser a(44100, 2);
    size_t at = 0;
    // Odd frame counts, so blocks and the oversampler's history straddle calls. Whole frames only:
    // feed() drops a trailing half frame (the decoder never hands one over).
    for (size_t frames = 1; at < pcm.size(); frames = ((frames * 7 + 4) % 2001) | 1) {
        const size_t n = frames * 2 < pcm.size() - at ? frames * 2 : pcm.size() - at;
        a.feed(pcm.data() + at, n);
        at += n;
    }
    const auto chunked = a.finish();
    check(chunked.hasIntegrated == whole.hasIntegrated && chunked.integratedLufs == whole.integratedLufs &&
              chunked.truePeakDbtp == whole.truePeakDbtp && chunked.samplePeakDbfs == whole.samplePeakDbfs &&
              chunked.gatedBlockCount == whole.gatedBlockCount && chunked.measuredMs == whole.measuredMs,
          "chunked feed matches one feed exactly");
}

void surroundSkipsTheLfe() {
    // 5.1 with signal only on the LFE has nothing BS.1770 weights.
    std::vector<float> pcm(48000 * 6);
    for (size_t f = 0; f < 48000; ++f) pcm[f * 6 + 3] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 60.0 * f / 48000));
    const auto r = measure(48000, 6, pcm);
    check(!r.hasIntegrated, "LFE alone has no loudness");
    check(LoudnessAnalyser::supports(1) && LoudnessAnalyser::supports(2) && LoudnessAnalyser::supports(6) &&
              !LoudnessAnalyser::supports(4),
          "channel layouts: mono, stereo, 5.1");
}

void cAbiGuards() {
    check(geode_loudness_create(48000, 4) == nullptr, "abi: quad refused");
    check(geode_loudness_create(0, 2) == nullptr, "abi: zero rate refused");
    geode_loudness_report report;
    report.has_integrated = 7;
    report.integrated_lufs = 99.0;
    geode_loudness_finish(nullptr, &report);
    check(report.has_integrated == 0 && report.integrated_lufs == 0.0 && report.gated_block_count == 0,
          "abi: finish on null zeroes the report");
    geode_loudness* l = geode_loudness_create(48000, 2);
    const auto pcm = sine(48000, 2, 997.0, -23.0, 1.0);
    geode_loudness_feed(l, pcm.data(), pcm.size());
    geode_loudness_finish(l, &report);
    check(geode_loudness_has_block(l) == 1 && report.has_integrated == 1 && report.measured_ms == 1000, "abi: feed and finish");
    geode_loudness_destroy(l);
    geode_loudness_destroy(nullptr);
}

}  // namespace

int main() {
    silenceHasNoIntegratedLoudness();
    quietSignalIsBelowTheAbsoluteGate();
    ebuReferenceSines();
    truePeakFindsInterSamplePeaks();
    relativeGateDropsTheQuietTail();
    oddChunksMatchOneChunk();
    surroundSkipsTheLfe();
    cAbiGuards();
    if (failures == 0) std::printf("loudness_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
