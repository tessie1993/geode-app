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

// #532 tests: the band edges, the tolerances and the ABI's numbers, each named by the mutant it kills. Expected
// values are hand-worked from the Kotlin LoudnessTargets windows (YouTube -14 LUFS, the streaming-video norm, band
// -15..-13 under -1 dBTP; short-form -11, band -12..-10) and its tolerances (gain 0.1 dB, peak 0.05 dB).
void loudnessWindowsMatchThePlatforms() {
    // Kills a window constant mutated or the switch cases swapped / falling through.
    const auto* yt = geode::audio::loudnessWindow(LoudnessTargetId::YouTube);
    const auto* shorts = geode::audio::loudnessWindow(LoudnessTargetId::Shorts);
    check(yt && yt->aimLufs == -14.0 && yt->lowLufs == -15.0 && yt->highLufs == -13.0 && yt->ceilingDbtp == -1.0,
          "window: YouTube plays at -14 LUFS (band -15..-13) under -1 dBTP");
    check(shorts && shorts->aimLufs == -11.0 && shorts->lowLufs == -12.0 && shorts->highLufs == -10.0 &&
              shorts->ceilingDbtp == -1.0,
          "window: short-form plays at -11 LUFS (band -12..-10) under -1 dBTP");
    check(geode::audio::loudnessWindow(LoudnessTargetId::AsIs) == nullptr &&
              geode::audio::loudnessWindow(static_cast<LoudnessTargetId>(-1)) == nullptr,
          "window: leave as-is (and a negative id) has none");
}

void loudnessBandEdgesAreOnTarget() {
    const auto yt = LoudnessTargetId::YouTube;
    const auto sh = LoudnessTargetId::Shorts;
    // Kills inBand `lufs >= low` -> `>` (would raise 1 dB).
    check(adviseLoudness(measured(-15.0, -3.0), yt).kind == AdviceKind::OnTarget, "band: -15 LUFS is YouTube's floor");
    // Kills inBand `lufs <= high` -> `<` (would lower 1 dB).
    check(adviseLoudness(measured(-13.0, -3.0), yt).kind == AdviceKind::OnTarget, "band: -13 LUFS is YouTube's top");
    check(adviseLoudness(measured(-12.0, -3.0), sh).kind == AdviceKind::OnTarget &&
              adviseLoudness(measured(-10.0, -3.0), sh).kind == AdviceKind::OnTarget,
          "band: -12 and -10 LUFS are short-form's edges");
    // Kills a band widened by a constant: just outside each edge is a real, audible gain.
    auto a = adviseLoudness(measured(-15.2, -3.0), yt);
    check(a.kind == AdviceKind::Raise && near(a.gainDb, 1.2, 1e-9) && near(a.resultingLufs, -14.0, 1e-9),
          "band: 0.2 LU under the floor is raised to -14");
    a = adviseLoudness(measured(-12.8, -3.0), yt);
    check(a.kind == AdviceKind::LowerLoud && near(a.gainDb, -1.2, 1e-9) && near(a.resultingLufs, -14.0, 1e-9),
          "band: 0.2 LU over the top is lowered to -14");
    a = adviseLoudness(measured(-12.2, -3.0), sh);
    check(a.kind == AdviceKind::Raise && near(a.gainDb, 1.2, 1e-9), "band: short-form floor");
    a = adviseLoudness(measured(-9.8, -3.0), sh);
    check(a.kind == AdviceKind::LowerLoud && near(a.gainDb, -1.2, 1e-9), "band: short-form top");
}

void loudnessPeakToleranceIsRoundingOnly() {
    const auto yt = LoudnessTargetId::YouTube;
    // At the top of the band the level allows -1 dB, so a peak judged unsafe there lowers a whole dB.
    // Kills dropping `+ kPeakToleranceDb` or flipping its sign (0.02 dB over would lower 1 dB).
    check(adviseLoudness(measured(-13.0, -0.98), yt).kind == AdviceKind::OnTarget,
          "peak: 0.02 dB over at the band's top is still rounding");
    // Kills peakSafe `<=` -> `<`: exactly the 0.05 dB tolerance is still safe.
    check(adviseLoudness(measured(-13.0, -0.95), yt).kind == AdviceKind::OnTarget,
          "peak: exactly 0.05 dB over is rounding");
    // Kills a tolerance widened to 0.1 dB or more: 0.1 dB over is clipping, and the level allows a full dB down.
    const auto a = adviseLoudness(measured(-13.0, -0.9), yt);
    check(a.kind == AdviceKind::LowerPeak && near(a.gainDb, -1.0, 1e-9) && near(a.resultingTruePeakDbtp, -1.9, 1e-9),
          "peak: 0.1 dB over is lowered");
}

void loudnessLoweringSaysWhy() {
    const auto yt = LoudnessTargetId::YouTube;
    // Kills `lufs < low` -> `<=`: at the floor, clipping is a peak problem, not a quiet one.
    auto a = adviseLoudness(measured(-15.0, 0.5), yt);
    check(a.kind == AdviceKind::LowerPeak && near(a.gainDb, -1.5, 1e-9), "lower: at the floor and clipping is the peak");
    // Kills `lufs > high` -> `>=`: at the top, clipping is a peak problem, not a loud one.
    a = adviseLoudness(measured(-13.0, 0.5), yt);
    check(a.kind == AdviceKind::LowerPeak && near(a.gainDb, -1.5, 1e-9), "lower: at the top and clipping is the peak");
    a = adviseLoudness(measured(-15.5, 0.5), yt);
    check(a.kind == AdviceKind::LowerClipping && near(a.gainDb, -1.5, 1e-9) && near(a.gainForLevelDb, 1.5, 1e-9),
          "lower: under the floor and clipping");
    // Kills ceilingLimited losing its tolerance: a ceiling that takes 0.05 dB more than the level is not "limited".
    a = adviseLoudness(measured(-10.0, 3.05), yt);
    check(a.kind == AdviceKind::LowerLoud && near(a.gainDb, -4.05, 1e-9), "lower: 0.05 dB past the level is not limited");
    a = adviseLoudness(measured(-10.0, 3.2), yt);
    check(a.kind == AdviceKind::LowerLoudLimited && near(a.gainDb, -4.2, 1e-9), "lower: 0.2 dB past the level is limited");
    // The same tolerance on the way up: a ceiling 0.05 dB short of the level is a plain raise.
    a = adviseLoudness(measured(-20.0, -6.95), yt);
    check(a.kind == AdviceKind::Raise && near(a.gainDb, 5.95, 1e-9) && near(a.gainForLevelDb, 6.0, 1e-9),
          "raise: 0.05 dB short of the level is not limited");
}

void asMixedHintsOnlyWhenWorthSaying() {
    const auto asIs = LoudnessTargetId::AsIs;
    // Kills `underYouTube > kQuietHintLu` -> `>=`: exactly 1 LU under is not worth saying.
    auto a = adviseLoudness(measured(-15.0, -6.0), asIs);
    check(a.kind == AdviceKind::AsMixed && a.quietUnderYouTubeLu == 0.0, "as-is: exactly 1 LU under says nothing");
    a = adviseLoudness(measured(-15.5, -6.0), asIs);
    check(near(a.quietUnderYouTubeLu, 1.5, 1e-9), "as-is: 1.5 LU under is said");
    // Kills a fabs() on the difference: louder than YouTube is never "quieter by" anything.
    a = adviseLoudness(measured(-8.0, -6.0), asIs);
    check(a.quietUnderYouTubeLu == 0.0, "as-is: louder than YouTube has no quiet hint");
    // Kills peakWarning `>` -> `>=`: a peak exactly on -1 dBTP is not over it.
    check(!adviseLoudness(measured(-14.0, -1.0), asIs).peakWarning, "as-is: a peak on -1 dBTP is no warning");
    check(adviseLoudness(measured(-14.0, -0.99), asIs).peakWarning, "as-is: a peak over -1 dBTP warns");
    // Kills as-is applying a gain or quoting a ceiling.
    a = adviseLoudness(measured(-30.0, 2.0), asIs);
    check(a.gainDb == 0.0 && a.ceilingDbtp == 0.0 && a.lufs == -30.0 && a.truePeakDbtp == 2.0,
          "as-is: never moves the level, quotes what was measured");
}

void loudnessAdviceAbiCarriesEveryNumber() {
    // Kills any dropped or swapped field copy in geode_loudness_advise (the PR's ABI case checks four of ten).
    geode_loudness_report report{};
    report.has_integrated = 1;
    report.integrated_lufs = -20.0;
    report.true_peak_dbtp = -10.0;
    geode_loudness_advice out{};
    out.quiet_under_youtube_lu = 99.0;
    out.peak_warning = 1;
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_YOUTUBE, &out) == GEODE_LOUDNESS_ADVICE_RAISE &&
              near(out.gain_db, 6.0, 1e-12) && near(out.gain_for_level_db, 6.0, 1e-12) && out.lufs == -20.0 &&
              out.true_peak_dbtp == -10.0 && near(out.resulting_lufs, -14.0, 1e-12) &&
              near(out.resulting_true_peak_dbtp, -4.0, 1e-12) && out.ceiling_dbtp == -1.0 &&
              out.quiet_under_youtube_lu == 0.0 && out.peak_warning == 0,
          "abi: a raise carries every number");

    report.true_peak_dbtp = -0.5;
    out = geode_loudness_advice{};
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_AS_IS, &out) == GEODE_LOUDNESS_ADVICE_AS_MIXED &&
              out.kind == GEODE_LOUDNESS_ADVICE_AS_MIXED && out.gain_db == 0.0 && out.lufs == -20.0 &&
              out.true_peak_dbtp == -0.5 && near(out.quiet_under_youtube_lu, 6.0, 1e-12) && out.peak_warning == 1 &&
              out.ceiling_dbtp == 0.0,
          "abi: as-mixed carries the hint and the warning");

    // Kills an early return that leaves a stale gain in out when nothing was measured.
    report.has_integrated = 0;
    out.gain_db = 5.0;
    out.kind = GEODE_LOUDNESS_ADVICE_RAISE;
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_YOUTUBE, &out) == GEODE_LOUDNESS_ADVICE_NOTHING_TO_MEASURE &&
              out.kind == GEODE_LOUDNESS_ADVICE_NOTHING_TO_MEASURE && out.gain_db == 0.0,
          "abi: nothing measured applies no gain");
    // Kills the target id ignored or mapped wrong: short-form aims 3 LU louder than YouTube.
    report.has_integrated = 1;
    report.true_peak_dbtp = -10.0;
    check(geode_loudness_advise(&report, GEODE_LOUDNESS_TARGET_SHORTS, &out) == GEODE_LOUDNESS_ADVICE_RAISE &&
              near(out.gain_db, 9.0, 1e-12),
          "abi: short-form target");
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
    loudnessWindowsMatchThePlatforms();
    loudnessBandEdgesAreOnTarget();
    loudnessPeakToleranceIsRoundingOnly();
    loudnessLoweringSaysWhy();
    asMixedHintsOnlyWhenWorthSaying();
    loudnessAdviceAbiCarriesEveryNumber();
    if (failures == 0) std::printf("loudness_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
