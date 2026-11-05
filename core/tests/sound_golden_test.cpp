// Golden test for the sound engine (core/sound): every GeodeSoundFrame scalar, frame by frame, over 20 s of
// synthetic music, against a recorded baseline. Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Icore core/tests/sound_golden_test.cpp core/sound/*.cpp -o sound_golden_test
//   ./sound_golden_test core/tests/golden/sound_frames.tsv            checks against the golden
//   ./sound_golden_test --write core/tests/golden/sound_frames.tsv    rewrites it after a deliberate change
//
// Why: the analysis is about to move off the render frame clock onto a fixed sample hop. Each step of that
// work must either keep these numbers or change them on purpose, with the regenerated golden in the same PR
// so the diff shows what moved. The second check records how far the frame-clocked fields drift between
// 30, 60 and 120 fps today (reported, not failed), and asserts that the hop-clocked rhythm already agrees:
// the drift report is the baseline the hop refactor must bring to zero.
//
// Determinism: the music is built in code from a fixed seed (no assets, no wall clock, no threads), and
// written to the engine the way the export path feeds it (OffscreenSceneRenderer, TrackAudio): exactly the
// audio up to each frame's end, then step(1/fps). The tolerance only absorbs libm differences between
// toolchains; a real change to the engine moves far more than that.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "sound/SoundEngine.hpp"

using geode::sound::SoundEngine;

namespace {

// Every scalar of GeodeSoundFrame, in declaration order. The arrays (eq, spectrum, waveform, chroma) are
// left out: they are derived from the same spectra as the scalars and would only multiply the golden.
#define GEODE_SOUND_SCALAR_FIELDS(X)                                                                         \
    X(bass) X(mid) X(treble) X(vol) X(bassAtt) X(midAtt) X(trebleAtt) X(volAtt) X(sub) X(subAtt) X(midHigh) \
    X(midHighAtt) X(low) X(middle) X(high) X(level) X(bassPresence) X(midPresence) X(treblePresence)        \
    X(intensity) X(bassTime) X(midTime) X(trebleTime) X(curvedTime) X(hitTime) X(noteTime) X(presence)      \
    X(rms) X(loudness) X(centroid) X(rolloff) X(flatness) X(densitySlow) X(densityFast) X(flux)             \
    X(energyTrend) X(peakFreq) X(peakMag) X(stereoWidth) X(stereoPan) X(kickEnv) X(snareEnv) X(hatEnv)      \
    X(onsetEnv) X(kickToggle) X(snareToggle) X(kickRandom) X(noteRandom) X(kick) X(snare) X(hat) X(kick2)   \
    X(kick4) X(kick8) X(kick16) X(snare2) X(snare4) X(hardCut) X(onset) X(silence) X(returned) X(surge)     \
    X(noteChange) X(legato) X(reattack) X(pitch) X(pitchClass) X(pitchHeight) X(pitchConfidence)            \
    X(interval) X(glide) X(vibrato) X(evenness) X(runDensity) X(bpm) X(beatPhase) X(beatConf) X(beat)       \
    X(beatCount) X(downbeat) X(barPhase) X(barCount) X(phrase) X(phraseIndex) X(phrasePhase) X(fluxLow)     \
    X(fluxMid) X(fluxHigh) X(hfc) X(keyHue) X(keyRoot) X(keyMajor) X(keyClarity) X(chordHue) X(tension)     \
    X(harmonicChange) X(scale) X(scaleRoot) X(scaleBrightness) X(scaleTension) X(scaleFit) X(novelty)       \
    X(sectionChange) X(sectionId) X(sectionState) X(sectionBars) X(buildUp) X(drop) X(fadeGain)

struct Field {
    const char* name;
    float GeodeSoundFrame::* member;
};
#define GEODE_FIELD_ENTRY(n) Field{#n, &GeodeSoundFrame::n},
constexpr Field kFields[] = {GEODE_SOUND_SCALAR_FIELDS(GEODE_FIELD_ENTRY)};
#undef GEODE_FIELD_ENTRY
constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
static_assert(kFieldCount == GEODE_SOUND_SCALARS, "a GeodeSoundFrame scalar was added or removed: update the list");
static_assert(offsetof(GeodeSoundFrame, eq) == GEODE_SOUND_SCALARS * sizeof(float), "scalars come first");

// Frame-clocked: the engine steps them with the frame's dt (FFT window, filter-bank slices, Harmony, Form).
// Hop-clocked: Rhythm's tempo and beat count, which run on a fixed 10 ms hop of the written samples.
bool hopClocked(const std::string& name) { return name == "bpm" || name == "beatCount"; }

constexpr int kRate = 48000;
constexpr double kSeconds = 20.0;
constexpr double kTau = 2.0 * std::numbers::pi;

// ---- The synthetic song ---------------------------------------------------------------------------------
//
// 120 BPM (a beat is 0.5 s, a bar 2 s), ten bars:
//   0–8 s    verse in C major, C–Am–F–G, kick on 1 and 3, snare on 2 and 4, eighth hats, an eighth bass
//   8–10 s   build: no kick or bass, a snare roll that speeds up, a rising noise sweep, chords held
//   10–16 s  drop in E♭ major (the key change), Eb–Cm–Ab–Bb, four on the floor, sixteenth hats, louder
//   16–17 s  silence
//   17–20 s  back in E♭ at verse level
// Every random number comes from one LCG with a fixed seed, so the song is identical on every run.

struct Lcg {
    uint32_t state = 0x6E0DE5EDu;
    double next() {  // -1 .. 1
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388608.0 - 1.0;
    }
};

double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

struct Section {
    double from, to;
    int key;        // semitones above C
    bool kick, bass, roll, sweep, sixteenths, fourOnFloor;
    double gain;
};
constexpr Section kSections[] = {
    {0.0, 8.0, 0, true, true, false, false, false, false, 0.7},
    {8.0, 10.0, 0, false, false, true, true, false, false, 0.75},
    {10.0, 16.0, 3, true, true, false, false, true, true, 1.25},
    {16.0, 17.0, 3, false, false, false, false, false, false, 0.0},
    {17.0, 20.0, 3, true, true, false, false, false, false, 0.7},
};

// I–vi–IV–V as (root, minor) above the key, one chord per bar.
constexpr int kChordRoot[4] = {0, 9, 5, 7};
constexpr bool kChordMinor[4] = {false, true, false, false};

std::vector<float> buildSong() {
    const int64_t total = static_cast<int64_t>(kSeconds * kRate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    Lcg noise;
    double hatPrev = 0.0;
    const double beat = 0.5;
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / kRate;
        const Section* sec = &kSections[0];
        for (const Section& c : kSections) if (t >= c.from && t < c.to) sec = &c;
        const double n = noise.next();  // drawn every sample, so later sections never shift earlier noise
        const double hatNoise = n - hatPrev;  // a first difference: noise tilted toward the top
        hatPrev = n;
        if (sec->gain == 0.0) continue;

        const int bar = static_cast<int>(t / (4 * beat));
        const int chord = bar % 4;
        const int root = sec->key + kChordRoot[chord];
        double left = 0.0;
        double right = 0.0;

        // Kick: a sine swept from 120 to 45 Hz, decaying over ~150 ms.
        if (sec->kick) {
            const double step = sec->fourOnFloor ? beat : 2 * beat;
            const double since = std::fmod(t, step);
            // 45 Hz plus 75 Hz that decays over 30 ms, integrated into a phase.
            const double phase = kTau * (45.0 * since + 75.0 * 0.03 * (1.0 - std::exp(-since / 0.03)));
            const double k = 0.8 * std::sin(phase) * std::exp(-since / 0.15);
            left += k;
            right += k;
        }
        // Snare on 2 and 4, or the build's roll: eighths, then sixteenths, then thirty-seconds.
        {
            double step = 0.0;
            double offset = 0.0;
            if (sec->roll) {
                const double into = t - sec->from;
                step = into < 1.0 ? beat / 2 : (into < 1.5 ? beat / 4 : beat / 8);
            } else if (sec->kick) {
                step = 2 * beat;
                offset = beat;
            }
            if (step > 0.0 && t - sec->from >= offset) {
                const double since = std::fmod(t - sec->from - offset, step);
                const double body = 0.25 * std::sin(kTau * 190.0 * since) * std::exp(-since / 0.05);
                const double rattle = 0.3 * n * std::exp(-since / 0.08);
                const double roll = sec->roll ? 0.5 + 0.25 * (t - sec->from) : 1.0;
                left += roll * (body + rattle);
                right += roll * (body + rattle);
            }
        }
        // Hats: eighths (sixteenths in the drop), leaning right.
        if (sec->kick) {
            const double step = sec->sixteenths ? beat / 4 : beat / 2;
            const double since = std::fmod(t, step);
            const double h = 0.35 * hatNoise * std::exp(-since / 0.02);
            left += 0.6 * h;
            right += h;
        }
        // Bass: the chord's root two octaves down, eighth notes, a few harmonics.
        if (sec->bass) {
            const double since = std::fmod(t, beat / 2);
            const double hz = midiHz(36 + root % 12);
            double v = 0.0;
            for (int h = 1; h <= 4; h++) v += std::sin(kTau * hz * h * t) / h;
            const double env = std::min(1.0, since / 0.005) * std::exp(-since / 0.2);
            left += 0.22 * v * env;
            right += 0.22 * v * env;
        }
        // Chords: a triad around middle C, three harmonics a voice, root left, fifth right.
        {
            const int third = kChordMinor[chord] ? 3 : 4;
            const int notes[3] = {60 + root % 12, 60 + root % 12 + third, 60 + root % 12 + 7};
            const double since = std::fmod(t, 4 * beat);
            const double env = std::min(1.0, since / 0.05) * (0.6 + 0.4 * std::exp(-since / 0.6));
            for (int i = 0; i < 3; i++) {
                const double hz = midiHz(notes[i]);
                const double v =
                    std::sin(kTau * hz * t) + 0.4 * std::sin(kTau * 2 * hz * t) + 0.15 * std::sin(kTau * 3 * hz * t);
                const double pan = 0.5 + 0.3 * (i - 1);
                left += 0.07 * env * v * (1.0 - pan) * 2;
                right += 0.07 * env * v * pan * 2;
            }
        }
        // The build's rising sweep: noise through a one-pole low-pass whose cutoff climbs.
        if (sec->sweep) {
            const double into = (t - sec->from) / (sec->to - sec->from);
            const double v = 0.25 * into * n * (0.3 + 0.7 * into);
            left += v;
            right += v;
        }
        out[2 * s] = static_cast<float>(std::tanh(sec->gain * left));
        out[2 * s + 1] = static_cast<float>(std::tanh(sec->gain * right));
    }
    return out;
}

// ---- Running the engine ---------------------------------------------------------------------------------

struct Run {
    std::vector<double> time;                  // each frame's end, seconds
    std::vector<std::vector<float>> values;    // per frame, kFieldCount scalars
};

Run run(const std::vector<float>& song, double fps) {
    SoundEngine engine(kRate);
    Run r;
    const int64_t total = static_cast<int64_t>(song.size() / 2);
    const int frames = static_cast<int>(std::llround(kSeconds * fps));
    int64_t done = 0;
    GeodeSoundFrame f{};
    for (int i = 1; i <= frames; i++) {
        const int64_t end = std::min(total, static_cast<int64_t>(std::llround(i * kRate / fps)));
        engine.write(song.data() + 2 * done, static_cast<int>(end - done), 2);
        done = end;
        engine.step(static_cast<float>(1.0 / fps), f);
        r.time.push_back(i / fps);
        std::vector<float> row(kFieldCount);
        for (int k = 0; k < kFieldCount; k++) row[k] = f.*kFields[k].member;
        r.values.push_back(std::move(row));
    }
    return r;
}

std::string tsv(const Run& r) {
    std::string out = "t";
    for (const Field& f : kFields) {
        out += '\t';
        out += f.name;
    }
    out += '\n';
    char buf[32];
    for (size_t i = 0; i < r.time.size(); i++) {
        std::snprintf(buf, sizeof buf, "%.4f", r.time[i]);
        out += buf;
        for (float v : r.values[i]) {
            std::snprintf(buf, sizeof buf, "\t%.6g", static_cast<double>(v));
            out += buf;
        }
        out += '\n';
    }
    return out;
}

// ---- Check 1: the golden at 60 fps ----------------------------------------------------------------------

// Wide enough for a libm ULP that a few hundred frames of smoothing carry forward, far below anything an
// engine change moves (and %.6g already rounds at ~5e-7 relative).
constexpr double kAbsTolerance = 1e-4;
constexpr double kRelTolerance = 1e-3;

int checkGolden(const Run& now, const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::printf("FAIL cannot read %s; generate it with sound_golden_test --write %s\n", path.c_str(), path.c_str());
        return 1;
    }
    std::string line;
    std::getline(in, line);
    std::vector<std::string> header;
    {
        std::istringstream h(line);
        std::string cell;
        while (std::getline(h, cell, '\t')) header.push_back(cell);
    }
    if (static_cast<int>(header.size()) != kFieldCount + 1) {
        std::printf("FAIL %s has %zu columns, the frame %d scalars (+ t); regenerate with --write after a deliberate "
                    "change\n", path.c_str(), header.size(), kFieldCount);
        return 1;
    }
    for (int k = 0; k < kFieldCount; k++) {
        if (header[k + 1] != kFields[k].name) {
            std::printf("FAIL column %d is %s in the golden, %s in the frame\n", k + 1, header[k + 1].c_str(),
                        kFields[k].name);
            return 1;
        }
    }
    std::vector<int> bad(kFieldCount, 0);
    std::vector<double> worst(kFieldCount, 0.0);
    std::vector<size_t> firstBad(kFieldCount, 0);
    size_t row = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (row >= now.values.size()) {
            std::printf("FAIL the golden has more frames than the run (%zu)\n", now.values.size());
            return 1;
        }
        std::istringstream cells(line);
        std::string cell;
        std::getline(cells, cell, '\t');
        for (int k = 0; k < kFieldCount; k++) {
            std::getline(cells, cell, '\t');
            const double want = std::strtod(cell.c_str(), nullptr);
            const double got = now.values[row][k];
            const double err = std::fabs(got - want);
            if (err > kAbsTolerance + kRelTolerance * std::fabs(want)) {
                if (bad[k]++ == 0) firstBad[k] = row;
                worst[k] = std::max(worst[k], err);
            }
        }
        row++;
    }
    if (row != now.values.size()) {
        std::printf("FAIL the golden has %zu frames, the run %zu\n", row, now.values.size());
        return 1;
    }
    int failed = 0;
    for (int k = 0; k < kFieldCount; k++) {
        if (bad[k] == 0) continue;
        failed++;
        std::printf("FAIL %-16s %4d frames off, first at t=%.3f s, worst |diff| %.4g\n", kFields[k].name, bad[k],
                    now.time[firstBad[k]], worst[k]);
    }
    if (failed > 0) {
        std::printf("%d of %d fields moved; if the change is deliberate run sound_golden_test --write %s and commit "
                    "the new golden with it\n", failed, kFieldCount, path.c_str());
        return 1;
    }
    std::printf("golden: %zu frames x %d fields match %s\n", row, kFieldCount, path.c_str());
    return 0;
}

// ---- Check 2: frame-rate dependence ---------------------------------------------------------------------

// Compares another frame rate against 60 fps at the times both rendered a frame (every 1/30 s), where both
// engines have been written exactly the same samples. Reports the frame-clocked fields' drift; fails only
// if a hop-clocked rhythm field disagrees.
int compareRates(const Run& base, const Run& other, double fps) {
    const double ratio = fps / 60.0;  // other frames per base frame
    struct Drift {
        double mean = 0.0;
        double max = 0.0;
        double scale = 0.0;  // the field's own range at 60 fps, to read the drift against
    };
    std::vector<Drift> drift(kFieldCount);
    int matched = 0;
    int rhythmBad = 0;
    for (size_t i = 0; i < base.values.size(); i++) {
        // Base frame i ends at (i+1)/60 s; the other run has a frame ending there when (i+1)·ratio is whole.
        const double j = (static_cast<double>(i) + 1.0) * ratio;
        if (std::fabs(j - std::round(j)) > 1e-9) continue;
        const size_t oi = static_cast<size_t>(std::llround(j)) - 1;
        if (oi >= other.values.size()) continue;
        matched++;
        for (int k = 0; k < kFieldCount; k++) {
            const double d = std::fabs(static_cast<double>(other.values[oi][k]) - base.values[i][k]);
            drift[k].mean += d;
            drift[k].max = std::max(drift[k].max, d);
        }
    }
    for (int k = 0; k < kFieldCount; k++) {
        float lo = base.values[0][k];
        float hi = lo;
        for (const auto& row : base.values) {
            lo = std::min(lo, row[k]);
            hi = std::max(hi, row[k]);
        }
        drift[k].scale = hi - lo;
        drift[k].mean /= std::max(1, matched);
    }
    std::printf("\n%.0f fps vs 60 fps over %d shared frame times (report only: the hop refactor must zero these)\n",
                fps, matched);
    std::printf("  %-16s %10s %10s %10s\n", "field", "mean|d|", "max|d|", "range@60");
    int equal = 0;
    for (int k = 0; k < kFieldCount; k++) {
        const std::string name = kFields[k].name;
        if (hopClocked(name)) {
            // Tempo to a tenth of a BPM, and the beat count to the beat: the oscillator's phase still reads
            // frame-clocked accents (harmonyAccent, sectionStarted), so a beat may land one frame apart.
            const double tol = name == "bpm" ? 0.1 : 1.0;
            const bool ok = drift[k].max <= tol;
            std::printf("  %-16s %10.4g %10.4g %10.4g  hop-clocked, must agree within %.1f: %s\n", name.c_str(),
                        drift[k].mean, drift[k].max, drift[k].scale, tol, ok ? "ok" : "FAIL");
            if (!ok) rhythmBad++;
            continue;
        }
        if (drift[k].max == 0.0) {
            equal++;
            continue;
        }
        std::printf("  %-16s %10.4g %10.4g %10.4g\n", name.c_str(), drift[k].mean, drift[k].max, drift[k].scale);
    }
    std::printf("  (%d frame-clocked fields already equal)\n", equal);
    return rhythmBad;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<float> song = buildSong();
    const Run at60 = run(song, 60.0);
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << tsv(at60);
        std::printf("wrote %zu frames x %d fields to %s\n", at60.values.size(), kFieldCount, argv[2]);
        return 0;
    }
    int failures = checkGolden(at60, argc > 1 ? argv[1] : "core/tests/golden/sound_frames.tsv");
    failures += compareRates(at60, run(song, 30.0), 30.0);
    failures += compareRates(at60, run(song, 120.0), 120.0);
    if (failures == 0) std::printf("\nsound_golden_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
