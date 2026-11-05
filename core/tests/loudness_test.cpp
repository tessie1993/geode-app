// Host-side checks for the BS.1770 meter (audio/dsp/Loudness). Built and run by CI (see
// .github/workflows/android.yml, "Native export DSP tests") with the system compiler; no device.
// References: EBU Tech 3341 (a 997 Hz stereo sine at -23 dBFS reads -23.0 LUFS) and BS.1770 itself
// (a full-scale mono sine near 1 kHz reads -3.01 LUFS). The loudness-target cases (audio/dsp/LoudnessTarget)
// pin the Kotlin LoudnessTargets.advise they replace (#422): which advice, the gain and the numbers quoted.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/Loudness.hpp"
#include "audio/dsp/LoudnessTarget.hpp"

using geode::audio::LoudnessAnalyser;
using geode::audio::LoudnessReport;
using geode::audio::AdviceKind;
using geode::audio::LoudnessTargetId;
using geode::audio::adviseLoudness;

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

LoudnessReport measured(double lufs, double truePeakDbtp) {
    LoudnessReport r;
    r.hasIntegrated = true;
    r.integratedLufs = lufs;
    r.truePeakDbtp = truePeakDbtp;
    return r;
}

// YouTube aims at -14 LUFS (band -15..-13) under a -1 dBTP ceiling; short-form at -11 (-12..-10), same ceiling.
void loudnessTargets() {
    const auto yt = LoudnessTargetId::YouTube;
    check(adviseLoudness(LoudnessReport{}, yt).kind == AdviceKind::NothingToMeasure, "advise: under the gate");
    check(adviseLoudness(LoudnessReport{}, LoudnessTargetId::AsIs).kind == AdviceKind::NothingToMeasure,
          "advise: under the gate, as-is too");

    auto a = adviseLoudness(measured(-14.5, -3.0), yt);
    check(a.kind == AdviceKind::OnTarget && a.gainDb == 0.0 && a.ceilingDbtp == -1.0, "advise: in band, peak safe");

    a = adviseLoudness(measured(-20.0, -10.0), yt);
    check(a.kind == AdviceKind::Raise && near(a.gainDb, 6.0, 1e-12) && near(a.gainForLevelDb, 6.0, 1e-12) &&
              near(a.resultingLufs, -14.0, 1e-12) && near(a.resultingTruePeakDbtp, -4.0, 1e-12),
          "advise: quiet with headroom is raised to the level");

    a = adviseLoudness(measured(-20.0, -3.0), yt);
    check(a.kind == AdviceKind::RaiseLimited && near(a.gainDb, 2.0, 1e-12) && near(a.gainForLevelDb, 6.0, 1e-12) &&
              near(a.resultingLufs, -18.0, 1e-12) && near(a.resultingTruePeakDbtp, -1.0, 1e-12),
          "advise: the ceiling stops a raise short");

    a = adviseLoudness(measured(-20.0, 0.5), yt);
    check(a.kind == AdviceKind::LowerClipping && near(a.gainDb, -1.5, 1e-12), "advise: quiet but clipping is lowered");

    a = adviseLoudness(measured(-8.0, -0.5), yt);
    check(a.kind == AdviceKind::LowerLoud && near(a.gainDb, -6.0, 1e-12) && near(a.resultingTruePeakDbtp, -6.5, 1e-12),
          "advise: loud is lowered to the level");

    a = adviseLoudness(measured(-10.0, 5.0), yt);
    check(a.kind == AdviceKind::LowerLoudLimited && near(a.gainDb, -6.0, 1e-12) && near(a.gainForLevelDb, -4.0, 1e-12) &&
              near(a.resultingLufs, -16.0, 1e-12),
          "advise: loud and peaky goes under the level to meet the ceiling");

    a = adviseLoudness(measured(-14.0, 0.0), yt);
    check(a.kind == AdviceKind::LowerPeak && near(a.gainDb, -1.0, 1e-12) && near(a.resultingLufs, -15.0, 1e-12),
          "advise: level fine, peak over: only the ceiling moves it");

    a = adviseLoudness(measured(-14.0, -0.98), yt);
    check(a.kind == AdviceKind::OnTarget, "advise: 0.02 dB over the ceiling is rounding");
    a = adviseLoudness(measured(-20.0, -0.93), yt);
    check(a.kind == AdviceKind::OnTarget && a.gainDb == 0.0, "advise: an inaudible gain is on target, even out of band");

    a = adviseLoudness(measured(-20.0, -10.0), LoudnessTargetId::Shorts);
    check(a.kind == AdviceKind::Raise && near(a.gainDb, 9.0, 1e-12) && near(a.resultingTruePeakDbtp, -1.0, 1e-12),
          "advise: short-form aims louder");
    a = adviseLoudness(measured(-11.5, -2.0), LoudnessTargetId::Shorts);
    check(a.kind == AdviceKind::OnTarget, "advise: short-form band");

    a = adviseLoudness(measured(-20.0, -0.5), LoudnessTargetId::AsIs);
    check(a.kind == AdviceKind::AsMixed && a.gainDb == 0.0 && near(a.quietUnderYouTubeLu, 6.0, 1e-12) && a.peakWarning,
          "advise: as-is says how far under YouTube and warns of the peak");
    a = adviseLoudness(measured(-14.5, -2.0), LoudnessTargetId::AsIs);
    check(a.kind == AdviceKind::AsMixed && a.quietUnderYouTubeLu == 0.0 && !a.peakWarning,
          "advise: half an LU under is not worth saying");
    check(adviseLoudness(measured(-20.0, -10.0), static_cast<LoudnessTargetId>(7)).kind == AdviceKind::AsMixed,
          "advise: an unknown target is as-is");

    geode_loudness_report report{};
    report.has_integrated = 1;
    report.integrated_lufs = -20.0;
    report.true_peak_dbtp = -3.0;
    geode_loudness_advice out{};
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_YOUTUBE, &out) == GEODE_LOUDNESS_ADVICE_RAISE_LIMITED &&
              out.kind == GEODE_LOUDNESS_ADVICE_RAISE_LIMITED && near(out.gain_db, 2.0, 1e-12) &&
              near(out.gain_for_level_db, 6.0, 1e-12) && out.ceiling_dbtp == -1.0,
          "abi: advise");
    check(geode_loudness_advise(nullptr, GEODE_LOUDNESS_TARGET_YOUTUBE, &out) == GEODE_LOUDNESS_ADVICE_NOTHING_TO_MEASURE,
          "abi: a null report is nothing to measure");
    report.true_peak_dbtp = 0.5;
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_AS_IS, nullptr) == GEODE_LOUDNESS_ADVICE_AS_MIXED,
          "abi: the kind alone");
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
    loudnessTargets();
    if (failures == 0) std::printf("loudness_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
