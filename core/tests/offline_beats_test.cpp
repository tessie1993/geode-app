// The offline analyser's beat grid (core/sound/offline/OfflineBeats, #463). Two modes:
//
//   ./offline_beats_test
//     Self-contained, in android.yml's native host tests: tracks built here in code with known beats.
//     Tempo, beats and downbeats land where the score put them; the grid read through sample() fires every
//     beat once at 24, 30 and 60 fps with the same counts at the same song time (an export's frame rate
//     never changes what the music does, spec #455 story 33); silence gives no grid; the analysis is
//     deterministic.
//
//   ./offline_beats_test --goldens build/analysis-lab
//     Against librosa's own output on the lab's clips (tools/analysis_lab/lab.py), in analysis-bench.yml.
//     The port must give librosa's answers, within these tolerances:
//       onset strength  Pearson r >= 0.995 and every frame within 2 % of the envelope's peak: the STFT
//                       here is float and librosa's is float64-then-complex64, and dB floors amplify that
//                       at near-silent bins, so values are close, not equal
//       tempo           within 1 % of librosa's: both pick one autocorrelation lag, and neighbouring lags
//                       are about 5 % apart at these tempi, so this means the same lag
//       beats           F-measure >= 0.95 against librosa's beats within ±1 frame (23 ms), untrimmed:
//                       the same dynamic programme on near-identical scores lands on the same frames, and
//                       the tolerance allows an end beat that the trim keeps on one side only
//
// Build: g++ -std=c++20 -O2 -Wall -Wextra -Werror -Icore core/tests/offline_beats_test.cpp core/sound/offline/OfflineBeats.cpp core/sound/Fft.cpp -o offline_beats_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include "sound/offline/OfflineBeats.hpp"
#include "tests/analysis/Annotations.hpp"
#include "tests/analysis/MirEval.hpp"
#include "tests/analysis/Wav.hpp"

namespace off = geode::sound::offline;
using namespace geode::analysis;

namespace {

int failures = 0;

void check(bool ok, const char* what, double value = 0.0) {
    std::printf("%s %s (%.4f)\n", ok ? "ok  " : "FAIL", what, value);
    if (!ok) ++failures;
}

constexpr int kRate = 44100;
constexpr double kTau = 2.0 * std::numbers::pi;

struct Lcg {
    uint32_t s = 0x12345678u;
    float next() {  // -1..1
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

// A ping plus a little noise: librosa's onset strength is a median over 128 mel bands, and a pure tone
// moves too few of them to register.
void addClick(std::vector<float>& y, double at, double hz, float gain, Lcg& rng) {
    const int start = static_cast<int>(std::lround(at * kRate));
    for (int i = 0; i < kRate * 3 / 100 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        y[start + i] += gain * static_cast<float>(std::sin(kTau * hz * t) * std::exp(-t / 0.008) +
                                                  0.5 * rng.next() * std::exp(-t / 0.004));
    }
}

void addHat(std::vector<float>& y, double at, float gain, Lcg& rng) {
    const int start = static_cast<int>(std::lround(at * kRate));
    float prev = 0.0f;
    for (int i = 0; i < kRate / 20 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const float white = rng.next();
        y[start + i] += gain * static_cast<float>((white - prev) * std::exp(-t / 0.015));  // first difference: bright
        prev = white;
    }
}

void addKick(std::vector<float>& y, double at, float gain) {
    const int start = static_cast<int>(std::lround(at * kRate));
    double phase = 0.0;
    for (int i = 0; i < kRate * 35 / 100 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        phase += kTau * (45.0 + 75.0 * std::exp(-t / 0.04)) / kRate;
        y[start + i] += gain * static_cast<float>(std::sin(phase) * std::exp(-t / 0.12));
    }
}

void addSnare(std::vector<float>& y, double at, float gain, Lcg& rng) {
    const int start = static_cast<int>(std::lround(at * kRate));
    for (int i = 0; i < kRate / 5 && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        y[start + i] += gain * static_cast<float>(0.4 * rng.next() * std::exp(-t / 0.05) +
                                                  0.5 * std::sin(kTau * 185.0 * t) * std::exp(-t / 0.03));
    }
}

void addChord(std::vector<float>& y, double at, double seconds, const std::vector<double>& hz, float gain) {
    const int start = static_cast<int>(std::lround(at * kRate));
    const int len = static_cast<int>(seconds * kRate);
    for (int i = 0; i < len && start + i < static_cast<int>(y.size()); ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double env = std::min(1.0, t / 0.04) * std::min(1.0, (seconds - t) / 0.1);
        double s = 0.0;
        for (double f : hz) s += std::sin(kTau * f * t) + 0.25 * std::sin(kTau * 2.0 * f * t);
        y[start + i] += gain * static_cast<float>(env * s / hz.size());
    }
}

struct Track {
    std::vector<float> mono;
    std::vector<double> beats, downbeats;
    double bpm = 0.0;
};

// A click per beat, the downbeat louder and higher: beat tracking with nothing else to go on.
Track clickTrack(double bpm, int bars) {
    Track tr;
    tr.bpm = bpm;
    const double beat = 60.0 / bpm, lead = 0.25;
    tr.mono.assign(static_cast<size_t>((lead + bars * 4 * beat + 1.0) * kRate), 0.0f);
    Lcg rng;
    for (int k = 0; k < bars * 4; ++k) {
        const double at = lead + k * beat;
        tr.beats.push_back(at);
        if (k % 4 == 0) tr.downbeats.push_back(at);
        addClick(tr.mono, at, k % 4 == 0 ? 1500.0 : 1000.0, k % 4 == 0 ? 0.8f : 0.45f, rng);
    }
    return tr;
}

// Kick on 1 and 3, snare on 2 and 4, eighth hats, a chord a bar alternating C major and A minor: the bar
// line is only in the harmony, which is what the downbeat phase reads.
Track groove(double bpm, int bars) {
    Track tr;
    tr.bpm = bpm;
    const double beat = 60.0 / bpm, lead = 0.25;
    tr.mono.assign(static_cast<size_t>((lead + bars * 4 * beat + 1.0) * kRate), 0.0f);
    Lcg rng;
    const std::vector<double> chords[2] = {{261.63, 329.63, 392.0}, {220.0, 261.63, 329.63}};
    for (int bar = 0; bar < bars; ++bar) {
        const double t = lead + bar * 4 * beat;
        tr.downbeats.push_back(t);
        addChord(tr.mono, t, 4 * beat, chords[bar % 2], 0.25f);
        for (int k = 0; k < 4; ++k) {
            tr.beats.push_back(t + k * beat);
            if (k % 2 == 0) addKick(tr.mono, t + k * beat, 0.6f);
            else addSnare(tr.mono, t + k * beat, 0.45f, rng);
            addHat(tr.mono, t + k * beat, 0.3f, rng);
            addHat(tr.mono, t + (k + 0.5) * beat, 0.18f, rng);
        }
    }
    return tr;
}

void selfTests() {
    {
        const Track tr = clickTrack(120.0, 16);
        const off::BeatGrid g = off::analyse(tr.mono.data(), tr.mono.size(), kRate);
        check(tempoAcc1(tr.bpm, g.bpm) > 0.0, "click 120: tempo within 4 %", g.bpm);
        check(beatF(tr.beats, g.beats) >= 0.95, "click 120: beat F >= 0.95", beatF(tr.beats, g.beats));
        check(beatF(tr.downbeats, g.downbeats) >= 0.9, "click 120: downbeat F >= 0.9", beatF(tr.downbeats, g.downbeats));

        // The same answers from a second run: nothing depends on state, threads or the clock.
        const off::BeatGrid again = off::analyse(tr.mono.data(), tr.mono.size(), kRate);
        check(again.bpm == g.bpm && again.beats == g.beats && again.downbeats == g.downbeats, "deterministic");

        // The grid through the live schema, at three export frame rates: every beat fires once, and at the
        // song times all three rates share (every 1/6 s) the counts and phases agree exactly.
        const double duration = static_cast<double>(tr.mono.size()) / kRate;
        std::vector<std::vector<float>> counts;
        for (int fps : {24, 30, 60}) {
            GeodeSoundFrame f{};
            int beats = 0, downbeats = 0;
            std::vector<float> at;
            const int frames = static_cast<int>(std::floor(duration * 6.0)) * (fps / 6);  // ends on a shared time
            for (int k = 1; k <= frames; ++k) {
                off::sample(g, static_cast<double>(k) / fps, 1.0 / fps, f);
                beats += f.beat > 0.0f;
                downbeats += f.downbeat > 0.0f;
                if (k % (fps / 6) == 0) {
                    at.push_back(f.beatCount);
                    at.push_back(f.barCount);
                    at.push_back(f.beatPhase);
                }
            }
            check(beats == static_cast<int>(g.beats.size()), "sample: every beat fires once", beats);
            check(downbeats == static_cast<int>(g.downbeats.size()), "sample: every downbeat fires once", downbeats);
            counts.push_back(at);
        }
        bool same = counts[0].size() == counts[1].size() && counts[1].size() == counts[2].size();
        for (size_t k = 0; same && k < counts[0].size(); ++k)
            same = std::fabs(counts[0][k] - counts[1][k]) < 1e-5f && std::fabs(counts[1][k] - counts[2][k]) < 1e-5f;
        check(same, "sample: 24, 30 and 60 fps agree at shared times");

        GeodeSoundFrame f{};
        off::sample(g, 0.1, 1.0 / 60, f);
        check(f.beatConf == 0.0f && f.beat == 0.0f, "sample: before the first beat, unlocked and silent");
        off::sample(g, g.beats[g.beats.size() / 2] + 0.01, 1.0 / 60, f);
        check(f.beatConf == 1.0f && f.beatPhase < 0.1f, "sample: just after a beat, locked, phase near 0", f.beatPhase);
    }
    {
        const Track tr = groove(96.0, 16);
        const off::BeatGrid g = off::analyse(tr.mono.data(), tr.mono.size(), kRate);
        check(tempoAcc1(tr.bpm, g.bpm) > 0.0, "groove 96: tempo within 4 %", g.bpm);
        check(beatF(tr.beats, g.beats) >= 0.9, "groove 96: beat F >= 0.9", beatF(tr.beats, g.beats));
        check(beatF(tr.downbeats, g.downbeats) >= 0.8, "groove 96: downbeat F >= 0.8", beatF(tr.downbeats, g.downbeats));
    }
    {
        // The tracker alone on an impulse every 20 frames: the beats are the impulses.
        std::vector<float> env(400, 0.0f);
        for (size_t i = 10; i < env.size(); i += 20) env[i] = 1.0f;
        const std::vector<int> beats = off::trackBeats(env, 60.0 * 43.0 / 20.0, 43.0);
        bool onImpulses = !beats.empty();
        for (int b : beats) onImpulses = onImpulses && (b - 10) % 20 == 0;
        check(onImpulses && beats.size() >= 18, "trackBeats: on the impulses", static_cast<double>(beats.size()));
    }
    {
        const std::vector<float> silence(kRate * 10, 0.0f);
        const off::BeatGrid g = off::analyse(silence.data(), silence.size(), kRate);
        check(g.bpm == 0.0 && g.beats.empty() && g.downbeats.empty(), "silence: no grid");
        check(off::paramsFor(44100).hop == 1024 && off::paramsFor(22050).hop == 512 && off::paramsFor(48000).hop == 1024,
              "paramsFor: librosa's hop scaled to the rate");
    }
}

void goldenTests(const std::string& dir) {
    bool ok = false;
    const auto clips = readRows(dir + "/clips.txt", &ok);
    check(ok && !clips.empty(), "goldens: the lab's clip list");
    for (const auto& row : clips) {
        const std::string clip = row[0], base = dir + "/" + clip;
        const Wav wav = readWav(base + ".wav");
        bool refOk = false;
        const Answers ref = readAnswers(base + ".librosa.tsv", &refOk);
        std::vector<double> golden;
        for (const auto& r : readRows(base + ".onset.tsv")) golden.push_back(std::stod(r[0]));
        if (wav.sampleRate == 0 || !refOk || golden.empty()) {
            check(false, (clip + ": WAV, librosa answers and onset golden present").c_str());
            continue;
        }
        const std::vector<float> mono = wav.mono();
        const off::Params p = off::paramsFor(wav.sampleRate);
        const off::MelSpectrogram mel = off::melSpectrogram(mono.data(), mono.size(), wav.sampleRate, p);
        const std::vector<float> env = off::onsetStrength(mel, p, off::Aggregate::Median);

        const bool sameLength = env.size() == golden.size();
        check(sameLength, (clip + ": onset envelope has librosa's frame count").c_str(), static_cast<double>(env.size()));
        if (sameLength) {
            double ma = 0, mb = 0, peak = 0;
            for (size_t i = 0; i < env.size(); ++i) ma += env[i], mb += golden[i], peak = std::max(peak, golden[i]);
            ma /= env.size();
            mb /= env.size();
            double sab = 0, saa = 0, sbb = 0, worst = 0;
            for (size_t i = 0; i < env.size(); ++i) {
                sab += (env[i] - ma) * (golden[i] - mb);
                saa += (env[i] - ma) * (env[i] - ma);
                sbb += (golden[i] - mb) * (golden[i] - mb);
                worst = std::max(worst, std::fabs(env[i] - golden[i]));
            }
            const double r = sab / std::sqrt(saa * sbb + 1e-30);
            check(r >= 0.995, (clip + ": onset envelope r >= 0.995").c_str(), r);
            check(worst <= 0.02 * peak, (clip + ": onset envelope within 2 % of its peak").c_str(), worst / (peak + 1e-30));
        }

        const double bpm = off::estimateTempo(env, wav.sampleRate, p.hop);
        check(std::fabs(bpm - ref.tempo) <= 0.01 * ref.tempo, (clip + ": tempo within 1 % of librosa's").c_str(), bpm);
        const double frameRate = static_cast<double>(wav.sampleRate) / p.hop;
        std::vector<double> beats;
        for (int f : off::trackBeats(env, ref.tempo, frameRate)) beats.push_back(f / frameRate);
        const double f = fMeasure(ref.beats, beats, 1.0 / frameRate);
        check(f >= 0.95, (clip + ": beats F >= 0.95 against librosa's within one frame").c_str(), f);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--goldens") == 0) {
        goldenTests(argv[2]);
    } else {
        selfTests();
    }
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("offline beats: all checks passed\n");
    return 0;
}
