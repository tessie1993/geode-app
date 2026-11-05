// The offline analyser's frame features (core/sound/offline/OfflineFeatures, #464). Two modes:
//
//   ./offline_features_test
//     Self-contained, in android.yml's native host tests, on sounds built here in code: a held A major
//     chord reads as A, C# and E and as the key of A major; kicks, snares and hats over that chord are found
//     in the percussive part where the score put them; the features read through sampleFeatures() fire every
//     hit once at 24, 30 and 60 fps; silence gives no events and only finite values; the schema coverage
//     (which GeodeSoundFrame scalars the offline analyser writes) is pinned, so a field that gains or loses
//     its offline value is a deliberate change.
//
//   ./offline_features_test --goldens build/analysis-lab
//     Against librosa's own output on the lab's clips (<clip>.features.tsv, <clip>.onsets.tsv), in
//     analysis-bench.yml. Tolerances, each a Pearson r over the clip's frames unless named otherwise:
//       rms >= 0.999, onset strength >= 0.995    the same sums over the same frames
//       centroid >= 0.99, flatness >= 0.98       float FFT here against float64 there, felt most in
//                                                near-silent frames
//       rolloff >= 0.97                          a bin index: one-bin steps where the 85 % line sits on an edge
//       harmonic, percussive >= 0.98             the same medians and masks; ties in a median can fall to
//                                                the other neighbour
//       chroma: mean cosine >= 0.9 over frames with sound; the key equal to librosa's
//                                                librosa resamples each CQT octave with soxr, here a
//                                                windowed sinc halves it: close, not equal
//       onsets: F >= 0.9 within one frame        the peak picker is a port; the envelope is near-equal
//
// Build: g++ -std=c++20 -O2 -Wall -Wextra -Werror -Icore core/tests/offline_features_test.cpp core/sound/offline/*.cpp core/sound/Fft.cpp -o offline_features_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "sound/offline/OfflineBeats.hpp"
#include "sound/offline/OfflineFeatures.hpp"
#include "tests/analysis/Annotations.hpp"
#include "tests/analysis/MirEval.hpp"
#include "tests/analysis/Wav.hpp"

namespace off = geode::sound::offline;
using namespace geode::analysis;

namespace {

int failures = 0;

void check(bool ok, const std::string& what, double value = 0.0) {
    std::printf("%s %s (%.4f)\n", ok ? "ok  " : "FAIL", what.c_str(), value);
    if (!ok) ++failures;
}

constexpr int kRate = 44100;
constexpr double kTau = 2.0 * std::numbers::pi;

struct Lcg {
    uint32_t s = 0x2468ACE1u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

void addTone(std::vector<float>& y, double hz, float gain) {
    for (size_t i = 0; i < y.size(); ++i) y[i] += gain * static_cast<float>(std::sin(kTau * hz * i / kRate));
}

void addKick(std::vector<float>& y, double at) {
    const int start = static_cast<int>(std::lround(at * kRate));
    double phase = 0.0;
    for (int i = 0; i < kRate * 3 / 10 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += kTau * (45.0 + 75.0 * std::exp(-t / 0.04)) / kRate;
        y[start + i] += 0.7f * static_cast<float>(std::sin(phase) * std::exp(-t / 0.12));
    }
}

void addSnare(std::vector<float>& y, double at, Lcg& rng) {
    const int start = static_cast<int>(std::lround(at * kRate));
    for (int i = 0; i < kRate / 5 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        y[start + i] += static_cast<float>(0.35 * rng.next() * std::exp(-t / 0.05) + 0.4 * std::sin(kTau * 185.0 * t) * std::exp(-t / 0.03));
    }
}

void addHat(std::vector<float>& y, double at, Lcg& rng) {
    const int start = static_cast<int>(std::lround(at * kRate));
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < kRate / 20 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const float w = rng.next();
        y[start + i] += 0.25f * static_cast<float>((w - 2.0f * a + b) * std::exp(-t / 0.015));  // second difference: air only
        b = a;
        a = w;
    }
}

constexpr int kScalars = GEODE_SOUND_SCALARS;

int writtenScalars(const GeodeSoundFrame& f) {
    const float* v = reinterpret_cast<const float*>(&f);
    int n = 0;
    for (int i = 0; i < kScalars; ++i) n += !std::isnan(v[i]);
    return n;
}

void selfTests() {
    // A held A major chord (A3, C#4, E4) with a 90 BPM groove over it.
    const double seconds = 16.0, beat = 60.0 / 90.0;
    std::vector<float> y(static_cast<size_t>(seconds * kRate), 0.0f);
    addTone(y, 220.0, 0.12f);
    addTone(y, 277.18, 0.10f);
    addTone(y, 329.63, 0.10f);
    Lcg rng;
    std::vector<double> kicks, snares, hats;
    for (double t = 0.5; t < seconds - 0.5; t += beat) {
        const int k = static_cast<int>(std::lround((t - 0.5) / beat));
        if (k % 2 == 0) addKick(y, t), kicks.push_back(t);
        else addSnare(y, t, rng), snares.push_back(t);
        addHat(y, t + beat / 2, rng);
        hats.push_back(t + beat / 2);
    }
    const off::Features f = off::analyseFeatures(y.data(), y.size(), kRate);

    std::array<double, 12> mean{};
    for (const auto& c : f.chroma)
        for (int p = 0; p < 12; ++p) mean[p] += c[p];
    int top[3] = {0, 0, 0};
    std::array<double, 12> m = mean;
    for (int& t : top) {
        t = static_cast<int>(std::max_element(m.begin(), m.end()) - m.begin());
        m[t] = -1.0;
    }
    std::sort(top, top + 3);
    check(top[0] == 1 && top[1] == 4 && top[2] == 9, "chroma: the chord's three classes are A, C# and E");
    check(f.keyRoot == 9 && f.keyMajor, "key: A major", f.keyClarity);

    check(fMeasure(kicks, f.kicks, 0.05) >= 0.8, "kicks found in the percussive part (F, 50 ms)", fMeasure(kicks, f.kicks, 0.05));
    check(fMeasure(snares, f.snares, 0.05) >= 0.7, "snares found in the percussive part (F, 50 ms)", fMeasure(snares, f.snares, 0.05));
    check(fMeasure(hats, f.hats, 0.05) >= 0.6, "hats found in the percussive part (F, 50 ms)", fMeasure(hats, f.hats, 0.05));
    double harmonic = 0, percussive = 0;
    for (int t = 0; t < f.frames; ++t) harmonic += f.harmonic[t], percussive += f.percussive[t];
    check(harmonic > percussive, "HPSS: the held chord is mostly harmonic", harmonic / (percussive + 1e-30));

    // Through the live schema at three export rates: every kick fires once at each.
    for (int fps : {24, 30, 60}) {
        GeodeSoundFrame frame{};
        int fired = 0;
        for (int k = 1; k <= static_cast<int>(seconds * fps); ++k) {
            off::sampleFeatures(f, static_cast<double>(k) / fps, 1.0 / fps, frame);
            fired += frame.kick > 0.0f;
        }
        check(fired == static_cast<int>(f.kicks.size()), "sampleFeatures: every kick fires once at " + std::to_string(fps) + " fps", fired);
    }

    // Which scalars the offline analyser writes: 46 from the features, 11 from the beat grid. A change
    // here is a field gaining or losing its offline value; update the count with the reason.
    GeodeSoundFrame probe;
    float* v = reinterpret_cast<float*>(&probe);
    for (size_t i = 0; i < sizeof probe / sizeof(float); ++i) v[i] = std::numeric_limits<float>::quiet_NaN();
    off::sampleFeatures(f, 5.0, 1.0 / 60, probe);
    const int fromFeatures = writtenScalars(probe);
    check(fromFeatures == 46, "coverage: sampleFeatures writes 46 scalars", fromFeatures);
    const off::BeatGrid grid = off::analyse(y.data(), y.size(), kRate);
    off::sample(grid, 5.0, 1.0 / 60, probe);
    check(writtenScalars(probe) == 57, "coverage: with the beat grid, 57 of 108 scalars", writtenScalars(probe));
    bool arrays = true;
    for (int i = kScalars; i < GEODE_SOUND_FRAME_FLOATS; ++i)
        if (i < kScalars + GEODE_SOUND_EQ_BANDS + GEODE_SOUND_SPECTRUM_POINTS) arrays = arrays && !std::isnan(v[i]);
    for (int p = 0; p < 12; ++p) arrays = arrays && !std::isnan(probe.chroma[p]) && !std::isnan(probe.chromaFast[p]) && !std::isnan(probe.chromaSlow[p]);
    check(arrays, "coverage: eq, spectrum and the three chromas are written");

    // Silence: no events, nothing but finite numbers.
    const std::vector<float> silence(kRate * 6, 0.0f);
    const off::Features s = off::analyseFeatures(silence.data(), silence.size(), kRate);
    check(s.onsets.empty() && s.kicks.empty() && s.snares.empty() && s.hats.empty(), "silence: no events");
    GeodeSoundFrame quiet{};
    off::sampleFeatures(s, 3.0, 1.0 / 60, quiet);
    bool finite = true;
    const float* q = reinterpret_cast<const float*>(&quiet);
    for (int i = 0; i < GEODE_SOUND_FRAME_FLOATS; ++i) finite = finite && std::isfinite(q[i]);
    check(finite, "silence: every field finite");

    // The peak picker on a hand-made envelope: two clear peaks, one bump under the delta.
    std::vector<float> env(100, 0.0f);
    env[20] = 1.0f;
    env[60] = 0.8f;
    env[80] = 0.03f;
    const std::vector<int> peaks = off::peakPick(env, 43.0);
    check(peaks.size() == 2 && peaks[0] == 20 && peaks[1] == 60, "peakPick: two peaks, the bump ignored", static_cast<double>(peaks.size()));
}

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) ma += a[i], mb += b[i];
    ma /= n;
    mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : (saa == sbb ? 1.0 : 0.0);
}

void goldenTests(const std::string& dir) {
    bool ok = false;
    const auto clips = readRows(dir + "/clips.txt", &ok);
    check(ok && !clips.empty(), "goldens: the lab's clip list");
    for (const auto& row : clips) {
        const std::string clip = row[0], base = dir + "/" + clip;
        const Wav wav = readWav(base + ".wav");
        const auto golden = readRows(base + ".features.tsv");
        bool refOk = false;
        const Answers ref = readAnswers(base + ".librosa.tsv", &refOk);
        std::vector<double> refOnsets;
        for (const auto& r : readRows(base + ".onsets.tsv"))
            if (r.size() > 1) refOnsets.push_back(std::stod(r[1]));
        if (wav.sampleRate == 0 || golden.empty() || golden[0].size() < 19 || !refOk) {
            check(false, clip + ": WAV, features golden and librosa answers present");
            continue;
        }
        const std::vector<float> mono = wav.mono();
        const off::Features f = off::analyseFeatures(mono.data(), mono.size(), wav.sampleRate);
        const size_t n = std::min(golden.size(), static_cast<size_t>(f.frames));
        auto col = [&](int c) {
            std::vector<double> out(n);
            for (size_t t = 0; t < n; ++t) out[t] = std::stod(golden[t][c]);
            return out;
        };
        auto mine = [&](const std::vector<float>& v) { return std::vector<double>(v.begin(), v.begin() + n); };
        struct Column {
            const char* name;
            int index;
            const std::vector<float>& values;
            double floor;
        };
        const Column columns[] = {{"rms", 0, f.rms, 0.999},
                                  {"centroid", 1, f.centroidHz, 0.99},
                                  {"rolloff", 2, f.rolloffHz, 0.97},
                                  {"flatness", 3, f.flatness, 0.98},
                                  {"onset strength", 4, f.onsetMean, 0.995},
                                  {"harmonic energy", 5, f.harmonic, 0.98},
                                  {"percussive energy", 6, f.percussive, 0.98}};
        for (const Column& c : columns) {
            const double r = pearson(mine(c.values), col(c.index));
            check(r >= c.floor, clip + ": " + c.name + " r >= " + std::to_string(c.floor).substr(0, 5), r);
        }
        double cosSum = 0.0;
        int counted = 0;
        for (size_t t = 0; t < n; ++t) {
            if (f.rms[t] < 1e-3f) continue;  // a frame with no sound has no chroma to agree on
            double ab = 0, aa = 0, bb = 0;
            for (int p = 0; p < 12; ++p) {
                const double a = f.chroma[t][p], b = std::stod(golden[t][7 + p]);
                ab += a * b, aa += a * a, bb += b * b;
            }
            if (aa > 0 && bb > 0) cosSum += ab / std::sqrt(aa * bb), ++counted;
        }
        const double cosine = counted ? cosSum / counted : 0.0;
        check(cosine >= 0.9, clip + ": chroma mean cosine >= 0.9", cosine);
        if (ref.hasKey)
            check(f.keyRoot == ref.keyRoot && f.keyMajor == ref.keyMajor, clip + ": key equals librosa's", f.keyRoot + (f.keyMajor ? 0.0 : 0.5));
        const double frameRate = static_cast<double>(wav.sampleRate) / f.hop;
        std::vector<double> onsets;
        for (int fr : off::peakPick(f.onsetMean, frameRate)) onsets.push_back(fr / frameRate);
        const double of = refOnsets.empty() && onsets.empty() ? 1.0 : fMeasure(refOnsets, onsets, 1.0 / frameRate);
        check(of >= 0.9, clip + ": onsets F >= 0.9 against librosa's within one frame", of);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--goldens") == 0) goldenTests(argv[2]);
    else selfTests();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("offline features: all checks passed\n");
    return 0;
}
