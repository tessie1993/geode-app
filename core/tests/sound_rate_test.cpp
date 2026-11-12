// Frame-rate test for the sound engine (core/sound): 20 s of synthetic music run at 30, 60 and 120 fps.
// Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/sound_rate_test.cpp core/sound/*.cpp core/output/FrameClock.cpp -o sound_rate_test
//   ./sound_rate_test                          the synthetic song across frame rates, and export against live
//   ./sound_rate_test --pcm <file.f32> <rate>  a real track (stereo float32) across frame rates, with a report
//   ./sound_rate_test --write <file.tsv>       every frame's scalars at 60 fps, for tools/glslcheck --stills
//
// Why: the analysis runs on a fixed 10 ms sample hop, not on the render frame, so an export must hear what
// the screen hears. Every field at 30 and 120 fps must equal the 60 fps run wherever both have been written
// the same samples: the state fields bit for bit at the shared frame, the triggers as the strongest one
// within the same stretch of audio (a 30 fps frame holds what two 60 fps frames show), and the same for the
// engine's feeling (Emotion).
//
// Determinism: the music is built in code from a fixed seed (no assets, no wall clock, no threads), and
// written to the engine the way the export path feeds it (core/output/ExportFrame, TrackPump): exactly the
// audio up to each frame's end, then step(1/fps).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "output/FrameClock.hpp"
#include "sound/SoundEngine.hpp"
#include "tests/SoundFields.hpp"

using geode::sound::SoundEngine;

namespace {

using geode::sound::test::Field;
using geode::sound::test::fieldIndex;
using geode::sound::test::kFieldCount;
using geode::sound::test::kFields;

// One-frame triggers: > 0 on the frame after the hop that fired them, so a coarser frame holds the strongest
// of the finer frames it spans.
bool isTrigger(const std::string& name) {
    static const char* const kTriggers[] = {"kick",     "snare",     "hat",       "kick2",      "kick4",
                                            "kick8",    "kick16",    "snare2",    "snare4",     "hardCut",
                                            "onset",    "silence",   "returned",  "surge",      "noteChange",
                                            "legato",   "reattack",  "beat",      "downbeat",   "phrase",
                                            "harmonicChange", "sectionChange", "drop", "degreeAccent", "transient"};
    for (const char* t : kTriggers) {
        if (name == t) return true;
    }
    return false;
}

// The engine's feeling (SoundEngine::emotion()), compared across rates like the frame, not in the golden.
constexpr int kGems = geode::sound::Emotion::kGems;
constexpr int kFeelCount = 9 + kGems;
std::vector<float> feelRow(const geode::sound::Emotion::State& e) {
    std::vector<float> r = {e.valence,      e.energy,      e.tension,       e.sharpness,    e.confidence,
                            static_cast<float>(e.scale),   static_cast<float>(e.degree), e.degreeValence,
                            e.degreeAccent};
    for (float g : e.gems) r.push_back(g);
    return r;
}
std::string feelName(int k) {
    static const char* const kNames[] = {"valence", "energy", "tension", "sharpness", "confidence",
                                         "feelScale", "degree", "degreeValence", "degreeAccent"};
    return k < 9 ? kNames[k] : "gems" + std::to_string(k - 9);
}

constexpr int kRate = 48000;
constexpr double kSeconds = 20.0;
constexpr double kTau = 2.0 * std::numbers::pi;

// ---- The synthetic song ---------------------------------------------------------------------------------
//
// 141 BPM (a bar 1.70 s), in bars:
//   0–5      verse in C major, C–Am–F–G, kick on 1 and 3, snare on 2 and 4, eighth hats, an eighth bass
//   5–6      build: no kick or bass, a snare roll that speeds up, a rising noise sweep, chords held
//   6–9.5    drop in E♭ major (the key change), Eb–Cm–Ab–Bb, four on the floor, sixteenth hats, louder
//   9.5–10   silence
//   10–20 s  back in E♭ at verse level
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
constexpr double kBeat = 60.0 / 141.0;
constexpr double kBar = 4 * kBeat;
constexpr Section kSections[] = {
    {0.0, 5 * kBar, 0, true, true, false, false, false, false, 0.7},
    {5 * kBar, 6 * kBar, 0, false, false, true, true, false, false, 0.75},
    {6 * kBar, 9.5 * kBar, 3, true, true, false, false, true, true, 1.25},
    {9.5 * kBar, 10 * kBar, 3, false, false, false, false, false, false, 0.0},
    {10 * kBar, kSeconds, 3, true, true, false, false, false, false, 0.7},
};

// I–vi–IV–V as (root, minor) above the key, one chord per bar.
constexpr int kChordRoot[4] = {0, 9, 5, 7};
constexpr bool kChordMinor[4] = {false, true, false, false};

// `bandLimited`: the noise (snare rattle, hats, the sweep) becomes clusters of sines below 16 kHz, a function
// of time rather than of the sample index, so the song is the same signal at every rate.
std::vector<float> buildSong(int rate = kRate, bool bandLimited = false) {
    const int64_t total = static_cast<int64_t>(kSeconds * rate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    Lcg noise;
    double hatPrev = 0.0;
    const double beat = kBeat;
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / rate;
        const Section* sec = &kSections[0];
        for (const Section& c : kSections) if (t >= c.from && t < c.to) sec = &c;
        double n = noise.next();  // drawn every sample, so later sections never shift earlier noise
        double hatNoise = n - hatPrev;  // a first difference: noise tilted toward the top
        hatPrev = n;
        if (bandLimited) {
            n = 0.0;
            hatNoise = 0.0;
            for (int k = 0; k < 6; k++) {
                n += 0.4 * std::sin(kTau * (1100.0 + 1370.0 * k) * t + k);
                hatNoise += 0.5 * std::sin(kTau * (8300.0 + 1270.0 * k) * t + 2 * k);
            }
        }
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
                const double into = (t - sec->from) / (sec->to - sec->from);
                step = into < 0.5 ? beat / 2 : (into < 0.75 ? beat / 4 : beat / 8);
            } else if (sec->kick) {
                step = 2 * beat;
                offset = beat;
            }
            if (step > 0.0 && t - sec->from >= offset) {
                const double since = std::fmod(t - sec->from - offset, step);
                const double body = 0.25 * std::sin(kTau * 190.0 * since) * std::exp(-since / 0.05);
                const double rattle = 0.3 * n * std::exp(-since / 0.08);
                const double roll = sec->roll ? 0.5 + 0.5 * (t - sec->from) / (sec->to - sec->from) : 1.0;
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
    std::vector<std::vector<float>> feel;      // per frame, kFeelCount emotion values
};

// `song` is interleaved stereo at `rate`; it is fed the way the export path feeds the engine.
Run run(const std::vector<float>& song, double fps, int rate = kRate) {
    SoundEngine engine(rate);
    Run r;
    const int64_t total = static_cast<int64_t>(song.size() / 2);
    const int frames = static_cast<int>(std::floor(static_cast<double>(total) / rate * fps));
    int64_t done = 0;
    GeodeSoundFrame f{};
    for (int i = 1; i <= frames; i++) {
        const int64_t end = std::min(total, static_cast<int64_t>(std::llround(i * rate / fps)));
        engine.write(song.data() + 2 * done, static_cast<int>(end - done), 2);
        done = end;
        engine.step(static_cast<float>(1.0 / fps), f);
        r.time.push_back(i / fps);
        std::vector<float> row(kFieldCount);
        for (int k = 0; k < kFieldCount; k++) row[k] = f.*kFields[k].member;
        r.values.push_back(std::move(row));
        r.feel.push_back(feelRow(engine.emotion()));
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

// Compares another frame rate against 60 fps at the times both rendered a frame (every 1/30 s), where both
// engines have been written exactly the same samples. A state field must be equal there; a trigger must be
// equal to the strongest the finer run showed since the previous shared time. Any difference fails.
struct Drift {
    double mean = 0.0;
    double max = 0.0;
};

int compareRates(const Run& base, const Run& other, double fps) {
    const double ratio = fps / 60.0;  // other frames per base frame
    const bool otherFiner = ratio > 1.0;
    std::vector<Drift> drift(kFieldCount);
    std::vector<Drift> feelDrift(kFeelCount);
    int matched = 0;
    size_t prevBase = 0;   // the first frame of each run after the last shared time
    size_t prevOther = 0;
    auto windowMax = [](const std::vector<std::vector<float>>& rows, size_t from, size_t to, int k) {
        float m = 0.0f;
        for (size_t r = from; r <= to; r++) m = std::max(m, rows[r][k]);
        return m;
    };
    for (size_t i = 0; i < base.values.size(); i++) {
        // Base frame i ends at (i+1)/60 s; the other run has a frame ending there when (i+1)·ratio is whole.
        const double j = (static_cast<double>(i) + 1.0) * ratio;
        if (std::fabs(j - std::round(j)) > 1e-9) continue;
        const size_t oi = static_cast<size_t>(std::llround(j)) - 1;
        if (oi >= other.values.size()) continue;
        matched++;
        auto compare = [&](const std::vector<std::vector<float>>& b, const std::vector<std::vector<float>>& o,
                           int k, bool trigger, Drift& d) {
            double want = b[i][k];
            double got = o[oi][k];
            if (trigger) {
                if (otherFiner) {
                    got = windowMax(o, prevOther, oi, k);
                } else {
                    want = windowMax(b, prevBase, i, k);
                }
            }
            const double diff = std::fabs(got - want);
            d.mean += diff;
            d.max = std::max(d.max, diff);
        };
        for (int k = 0; k < kFieldCount; k++) compare(base.values, other.values, k, isTrigger(kFields[k].name), drift[k]);
        for (int k = 0; k < kFeelCount; k++) compare(base.feel, other.feel, k, isTrigger(feelName(k)), feelDrift[k]);
        prevBase = i + 1;
        prevOther = oi + 1;
    }
    std::printf("\n%.0f fps vs 60 fps over %d shared frame times (every field must agree)\n", fps, matched);
    int bad = 0;
    int equal = 0;
    auto report = [&](const std::string& name, Drift d) {
        d.mean /= std::max(1, matched);
        if (d.max == 0.0) {
            equal++;
            return;
        }
        bad++;
        std::printf("  FAIL %-16s mean|d| %10.4g  max|d| %10.4g%s\n", name.c_str(), d.mean, d.max,
                    isTrigger(name) ? "  (trigger, per shared window)" : "");
    };
    for (int k = 0; k < kFieldCount; k++) report(kFields[k].name, drift[k]);
    for (int k = 0; k < kFeelCount; k++) report(feelName(k), feelDrift[k]);
    std::printf("  %d of %d fields and feeling values equal at every shared frame\n", equal, kFieldCount + kFeelCount);
    if (matched == 0) {
        std::printf("  FAIL no shared frame times\n");
        bad++;
    }
    return bad;
}

// ---- Hi-res sources ---------------------------------------------------------------------------------------
//
// 88.2 kHz and up is analysed at half the rate (or a quarter), behind a half-band decimator
// (core/sound/Decimate.hpp, SoundEngine::analysisRateFor). The filter must be flat through the audible band
// and shut above it, and the same band-limited song written at 96 kHz must read as it does at 48 kHz: the
// same tempo, beats, drums, key and scale, and levels within a hair (the decimator's 0.24 ms delay moves
// the hops a little against the music; nothing else differs).

double halfBandGainDb(double cycles) {  // a sine at `cycles` of the input rate, through one HalfBand
    geode::sound::HalfBand h;
    double in = 0.0;
    double out = 0.0;
    float y = 0.0f;
    for (int i = 0; i < 8000; i++) {
        const float x = static_cast<float>(std::sin(kTau * cycles * i));
        if (h.push(x, y) && i >= 4000) out += static_cast<double>(y) * y;
        if (i >= 4000) in += 0.5 * static_cast<double>(x) * x;
    }
    return 10.0 * std::log10(std::max(out, 1e-30) / in);
}

int compareHiRes(const Run& at48, const Run& at96) {
    int bad = 0;
    auto check = [&](bool ok, const char* what, double value) {
        if (!ok) {
            std::printf("  FAIL %s (%.4g)\n", what, value);
            bad++;
        }
    };
    std::printf("\nHalf-band decimator and a 96 kHz song against the same song at 48 kHz\n");
    for (double c : {0.01, 0.1, 0.2}) check(std::fabs(halfBandGainDb(c)) < 0.1, "half-band flat to 0.2 of the rate", halfBandGainDb(c));
    for (double c : {0.32, 0.4, 0.49}) check(halfBandGainDb(c) < -70.0, "half-band shut from 0.32 of the rate", halfBandGainDb(c));
    check(SoundEngine::analysisRateFor(44100) == 44100 && SoundEngine::analysisRateFor(48000) == 48000 &&
              SoundEngine::analysisRateFor(88200) == 44100 && SoundEngine::analysisRateFor(96000) == 48000 &&
              SoundEngine::analysisRateFor(176400) == 44100 && SoundEngine::analysisRateFor(192000) == 48000,
          "88.2 kHz and up analysed at 44.1/48 kHz", 0.0);
    check(SoundEngine(96000).sampleRateHz() == 96000, "the engine still names its input rate", 0.0);

    const size_t n = std::min(at48.values.size(), at96.values.size());
    struct Limit {
        const char* name;
        double meanAbs;  // the mean |96k - 48k| over every frame
    };
    // About ten times what was measured when the decimator landed, so a real regression fails and a libm does not.
    for (const Limit& l : {Limit{"bpm", 0.1}, Limit{"beatPhase", 0.01}, Limit{"keyRoot", 0.01}, Limit{"keyMajor", 0.01},
                           Limit{"scale", 0.05}, Limit{"level", 0.01}, Limit{"loudnessS", 0.01}, Limit{"bass", 0.05},
                           Limit{"mid", 0.05}, Limit{"treble", 0.1}, Limit{"centroid", 0.01}, Limit{"flatness", 0.02},
                           Limit{"pitch", 0.05}, Limit{"drive", 0.03}}) {
        const int k = fieldIndex(l.name);
        double d = 0.0;
        for (size_t i = 0; i < n; i++) d += std::fabs(at96.values[i][k] - at48.values[i][k]);
        d /= std::max<size_t>(1, n);
        std::printf("  %-10s mean |96k - 48k| %.4f\n", l.name, d);
        check(d <= l.meanAbs, l.name, d);
    }
    for (const char* name : {"kick", "snare", "hat", "onset", "beat", "downbeat", "noteChange", "sectionChange", "drop"}) {
        const int k = fieldIndex(name);
        int a = 0;
        int b = 0;
        for (size_t i = 0; i < n; i++) {
            a += at48.values[i][k] > 0.0f;
            b += at96.values[i][k] > 0.0f;
        }
        std::printf("  %-10s %d at 48 kHz, %d at 96 kHz\n", name, a, b);
        check(std::abs(a - b) <= 1, name, b - a);
    }
    if (bad == 0) std::printf("  96 kHz reads as 48 kHz\n");
    return bad;
}

// ---- Export equals live -----------------------------------------------------------------------------------
//
// The same samples must give the same sound frame whoever cut them. The export writes, per frame, exactly
// the audio up to the frame's end on its whole-ms clock (OffscreenSceneRenderer: TrackAudio.deliverUntilMs(
// FrameClock::frameTimeMs(n + 1))) and steps 1/fps. Live writes the tap's blocks as they arrive, in sizes
// that never line up with a frame (several in one frame, none in another), and steps by a measured, jittered
// frame time. At checkpoints where both have been written the same samples (every 15th export frame), every
// state field and feeling value must be equal bit for bit, and every trigger equal to the strongest since
// the previous checkpoint on each side.

struct Checkpoints {
    std::vector<std::vector<float>> values;
    std::vector<std::vector<float>> feel;
};

// Folds one step into the open window: state fields as they stand now, triggers as the strongest so far.
void fold(std::vector<float>& window, const std::vector<float>& row, bool fresh, bool (*trigger)(int)) {
    for (size_t k = 0; k < row.size(); k++) {
        window[k] = fresh || !trigger(static_cast<int>(k)) ? row[k] : std::max(window[k], row[k]);
    }
}
bool frameTrigger(int k) { return isTrigger(kFields[k].name); }
bool feelTrigger(int k) { return isTrigger(feelName(k)); }

struct Window {
    std::vector<float> values = std::vector<float>(kFieldCount);
    std::vector<float> feel = std::vector<float>(kFeelCount);
    bool fresh = true;
    void take(const GeodeSoundFrame& f, const geode::sound::Emotion::State& e) {
        std::vector<float> row(kFieldCount);
        for (int k = 0; k < kFieldCount; k++) row[k] = f.*kFields[k].member;
        fold(values, row, fresh, frameTrigger);
        fold(feel, feelRow(e), fresh, feelTrigger);
        fresh = false;
    }
    void close(Checkpoints& out) {
        out.values.push_back(values);
        out.feel.push_back(feel);
        fresh = true;
    }
};

constexpr int kExportFps = 60;
constexpr int kCheckpointEvery = 15;

// The export's cut of each frame, and the checkpoints: the cut at every kCheckpointEvery-th frame.
std::vector<int64_t> exportCuts(int64_t total) {
    const geode::output::FrameClock clock(kExportFps, kRate);
    std::vector<int64_t> cuts;
    for (int64_t n = 0;; n++) {
        const int64_t end = clock.frameTimeMs(n + 1) * kRate / 1000;
        if (end > total) break;
        cuts.push_back(end);
    }
    return cuts;
}

Checkpoints runExport(const std::vector<float>& song, const std::vector<int64_t>& cuts) {
    SoundEngine engine(kRate);
    Checkpoints out;
    Window w;
    GeodeSoundFrame f{};
    int64_t done = 0;
    for (size_t n = 0; n < cuts.size(); n++) {
        engine.write(song.data() + 2 * done, static_cast<int>(cuts[n] - done), 2);
        done = cuts[n];
        engine.step(1.0f / kExportFps, f);
        w.take(f, engine.emotion());
        if ((n + 1) % kCheckpointEvery == 0) w.close(out);
    }
    return out;
}

Checkpoints runLive(const std::vector<float>& song, const std::vector<int64_t>& cuts, uint32_t seed) {
    SoundEngine engine(kRate);
    Lcg rng;
    rng.state = seed;
    const auto uniform = [&rng](double lo, double hi) { return lo + (hi - lo) * 0.5 * (rng.next() + 1.0); };
    Checkpoints out;
    Window w;
    GeodeSoundFrame f{};
    int64_t done = 0;
    int empty = 0;
    for (size_t c = kCheckpointEvery - 1; c < cuts.size(); c += kCheckpointEvery) {
        const int64_t checkpoint = cuts[c];
        while (done < checkpoint) {
            // A live frame: none of the tap's blocks (at most twice running, well under the starve time) or
            // up to ~40 ms of them, cut short at the checkpoint.
            int64_t end = done;
            if (empty >= 2 || rng.next() > -0.6) end = std::min(checkpoint, done + static_cast<int64_t>(uniform(1.0, 1920.0)));
            empty = end == done ? empty + 1 : 0;
            while (done < end) {
                const int block = static_cast<int>(std::min<int64_t>(end - done, static_cast<int64_t>(uniform(32.0, 1024.0))));
                engine.write(song.data() + 2 * done, block, 2);
                done += block;
            }
            engine.step(static_cast<float>(uniform(0.004, 0.05)), f);
            w.take(f, engine.emotion());
        }
        w.close(out);
    }
    return out;
}

int compareExportLive(const Checkpoints& ex, const Checkpoints& live, uint32_t seed) {
    int bad = 0;
    if (ex.values.size() != live.values.size() || ex.values.empty()) {
        std::printf("  FAIL export has %zu checkpoints, live %zu\n", ex.values.size(), live.values.size());
        return 1;
    }
    auto report = [&](const std::vector<std::vector<float>>& a, const std::vector<std::vector<float>>& b, int k,
                      const std::string& name) {
        double max = 0.0;
        for (size_t i = 0; i < a.size(); i++) max = std::max(max, std::fabs(static_cast<double>(a[i][k]) - b[i][k]));
        if (max == 0.0) return;
        bad++;
        std::printf("  FAIL %-16s max|d| %10.4g\n", name.c_str(), max);
    };
    std::printf("\nexport vs live (seed %08x) over %zu checkpoints (every field must agree)\n", seed, ex.values.size());
    for (int k = 0; k < kFieldCount; k++) report(ex.values, live.values, k, kFields[k].name);
    for (int k = 0; k < kFeelCount; k++) report(ex.feel, live.feel, k, feelName(k));
    if (bad == 0) std::printf("  all %d fields and feeling values equal\n", kFieldCount + kFeelCount);
    return bad;
}

// ---- Real music (--pcm) -----------------------------------------------------------------------------------
//
// A decoded track (raw interleaved stereo float32, e.g. `ffmpeg -i t.mp3 -f f32le -ac 2 -ar 44100 t.f32`)
// through the same frame-rate check as the synthetic song, plus what the engine heard, for an ear to judge:
// there are no labels to assert against.

std::string keyName(float root, float major) {
    static const char* kNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    const int r = static_cast<int>(std::lround(root * 12.0f)) % 12;
    return std::string(kNames[(r + 12) % 12]) + (major >= 0.5f ? " major" : " minor");
}

void reportTrack(const Run& r, const char* label) {
    const auto at = [&](size_t i, const char* name) { return r.values[i][fieldIndex(name)]; };
    const size_t n = r.values.size();
    if (n == 0) return;
    int sections = 0;
    int drops = 0;
    for (size_t i = 0; i < n; i++) {
        if (at(i, "sectionChange") > 0.0f) sections++;
        if (at(i, "drop") > 0.0f) drops++;
    }
    std::printf("\n%s: %.1f s, %d section changes, %d drops\n", label, r.time.back(), sections, drops);
    for (size_t i = 0; i < n; i += static_cast<size_t>(std::max<size_t>(1, n / 12))) {
        const float scale = at(i, "scale");
        std::printf("  %6.1f s  %6.1f BPM (conf %.2f)  key %-9s (clarity %.2f)  scale %-24s on %-2s  section %.0f  build %.2f\n",
                    r.time[i], at(i, "bpm"), at(i, "beatConf"), keyName(at(i, "keyRoot"), at(i, "keyMajor")).c_str(),
                    at(i, "keyClarity"), scale >= 0.0f ? geode::sound::scaleInfo(static_cast<int>(scale)).id : "-",
                    keyName(at(i, "scaleRoot"), 1.0f).substr(0, 2).c_str(), at(i, "sectionId"), at(i, "buildUp"));
    }
}

int runPcm(const char* path, int rate) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in || rate <= 0) {
        std::printf("FAIL cannot read %s at %d Hz\n", path, rate);
        return 1;
    }
    const auto bytes = static_cast<size_t>(in.tellg());
    std::vector<float> song(bytes / sizeof(float) / 2 * 2);  // whole stereo frames
    in.seekg(0);
    in.read(reinterpret_cast<char*>(song.data()), static_cast<std::streamsize>(song.size() * sizeof(float)));
    if (!in || song.empty()) {
        std::printf("FAIL %s is not stereo float32\n", path);
        return 1;
    }
    const Run at60 = run(song, 60.0, rate);
    reportTrack(at60, path);
    int failures = compareRates(at60, run(song, 30.0, rate), 30.0);
    failures += compareRates(at60, run(song, 120.0, rate), 120.0);
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--pcm") == 0) return runPcm(argv[2], std::atoi(argv[3])) == 0 ? 0 : 1;
    const std::vector<float> song = buildSong();
    const Run at60 = run(song, 60.0);
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << tsv(at60);
        std::printf("wrote %zu frames x %d fields to %s\n", at60.values.size(), kFieldCount, argv[2]);
        return 0;
    }
    int failures = compareRates(at60, run(song, 30.0), 30.0);
    failures += compareRates(at60, run(song, 120.0), 120.0);
    failures += compareHiRes(run(buildSong(kRate, true), 60.0), run(buildSong(96000, true), 60.0, 96000));
    const std::vector<int64_t> cuts = exportCuts(static_cast<int64_t>(song.size() / 2));
    const Checkpoints exported = runExport(song, cuts);
    for (uint32_t seed : {0x1u, 0xC0FFEEu}) failures += compareExportLive(exported, runLive(song, cuts, seed), seed);
    if (failures == 0) std::printf("\nsound_rate_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
