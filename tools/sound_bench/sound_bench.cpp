// The sound engine's accuracy bench (#998 item 1): synthetic music with known ground truth through
// core/sound, scored for tempo, beat phase, meter and key. A measuring tool, not a test: it prints numbers
// and exits 0. Constants in core/sound were chosen by ear; this says how close they land.
//
//   g++ -std=c++20 -O2 -Icore tools/sound_bench/sound_bench.cpp core/sound/*.cpp -o sound_bench
//   ./sound_bench            the whole set (~25 min of audio, under a minute)
//   ./sound_bench --quick    a few cases of each kind, the CI smoke run
//
// The material is built in code from a fixed seed, so a number moves only when the engine does:
//   rhythm  click tracks (4/4, accented one), a drum kit with bass and chords, the kit with swung
//           eighths, and 3/4 (oom-pah-pah), each at tempos across the range and never one favoured
//   key     the twelve tonics in four progressions (major I–IV–V–I and I–vi–IV–V, harmonic minor
//           i–iv–V–i, natural minor i–VI–III–VII), harmonic tones with a bass, a melody on the scale
//           and a soft kit
//
// Scores, the MIREX conventions where there is one:
//   tempo   the bpm at the end, Acc1 within 4 %, Acc2 also at ×2, ×½, ×3, ×⅓
//   beat    F-measure of the beat triggers against the true beats within ±70 ms, from 5 s on; each
//           trigger is placed where the engine says the beat fell (the frame's time less beatPhase)
//   meter   downbeat F-measure the same way, and the bar length implied by the median downbeat interval
//   key     the MIREX weighted score at the end (1 right, 0.5 a fifth, 0.3 relative, 0.2 parallel) and the
//           share of frames from 8 s on with the exact key; then every key profile Essentia ships, run on
//           the engine's own slow chroma frame by frame (no hysteresis), to compare profiles on equal input
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include "sound/SoundEngine.hpp"

using geode::sound::SoundEngine;

namespace {

constexpr int kRate = 48000;
constexpr double kFps = 60.0;
constexpr double kTau = 2.0 * std::numbers::pi;

struct Lcg {
    uint32_t state;
    double next() {  // -1 .. 1
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388608.0 - 1.0;
    }
    int below(int n) { return static_cast<int>((next() + 1.0) * 0.5 * n) % n; }
};

double midiHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

// ---- Material --------------------------------------------------------------------------------------------

enum class Hit { Click, ClickAccent, Kick, Snare, Hat, Bass, Chord, Melody };

struct Event {
    double at;
    Hit kind;
    double gain;
    double length;  // seconds the voice sounds (tones release over 50 ms at the end)
    std::array<int, 3> midi{};
    std::array<double, 3> hz{};  // from midi, filled by render()
};

struct Song {
    std::string group;
    std::string name;
    double seconds;
    double bpm;
    int meter;
    double start;  // the first beat, s
    int key;       // 0..11 major on that tonic, 12..23 minor; -1 none
    std::vector<Event> events;
};

double tone(double hz, double u) {
    return std::sin(kTau * hz * u) + 0.4 * std::sin(kTau * 2 * hz * u) + 0.15 * std::sin(kTau * 3 * hz * u);
}

double voice(const Event& e, double u, double n, double hatNoise) {
    const double release = std::clamp((e.length - u) / 0.05, 0.0, 1.0);
    switch (e.kind) {
        case Hit::Click:
        case Hit::ClickAccent: {
            const double amp = e.kind == Hit::ClickAccent ? 0.8 : 0.35;
            return amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) + 0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u);
        }
        case Hit::Kick:
            return 0.8 * std::sin(kTau * (48.0 * u + 70.0 * 0.03 * (1.0 - std::exp(-u / 0.03)))) * std::exp(-u / 0.15);
        case Hit::Snare:
            return 0.25 * std::sin(kTau * 185.0 * u) * std::exp(-u / 0.05) + 0.3 * n * std::exp(-u / 0.08);
        case Hit::Hat:
            return 0.3 * hatNoise * std::exp(-u / 0.02);
        case Hit::Bass: {
            double v = 0.0;
            for (int h = 1; h <= 3; h++) v += std::sin(kTau * e.hz[0] * h * u) / h;
            return 0.22 * v * std::min(1.0, u / 0.005) * std::exp(-u / 0.4) * release;
        }
        case Hit::Chord: {
            double v = 0.0;
            for (double hz : e.hz) v += tone(hz, u);
            return 0.07 * v * std::min(1.0, u / 0.03) * (0.5 + 0.5 * std::exp(-u / 0.8)) * release;
        }
        case Hit::Melody:
            return 0.1 * tone(e.hz[0], u) * std::min(1.0, u / 0.01) * std::exp(-u / 0.3) * release;
    }
    return 0.0;
}

std::vector<float> render(Song& song, uint32_t seed) {
    std::sort(song.events.begin(), song.events.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    double longest = 0.0;
    for (Event& e : song.events) {
        longest = std::max(longest, e.length);
        for (size_t i = 0; i < 3; i++) e.hz[i] = midiHz(e.midi[i]);
    }
    const int64_t total = static_cast<int64_t>(song.seconds * kRate);
    std::vector<float> out(static_cast<size_t>(2 * total));
    Lcg noise{seed};
    double prev = 0.0;
    size_t first = 0;
    for (int64_t s = 0; s < total; s++) {
        const double t = static_cast<double>(s) / kRate;
        const double n = noise.next();
        const double hatNoise = n - prev;
        prev = n;
        while (first < song.events.size() && song.events[first].at + longest < t) first++;
        double l = 0.0;
        double r = 0.0;
        for (size_t i = first; i < song.events.size() && song.events[i].at <= t; i++) {
            const Event& e = song.events[i];
            const double u = t - e.at;
            if (u > e.length) continue;
            const double v = e.gain * voice(e, u, n, hatNoise);
            const double pan = e.kind == Hit::Hat ? 0.65 : e.kind == Hit::Melody ? 0.4 : 0.5;
            l += 2.0 * (1.0 - pan) * v;
            r += 2.0 * pan * v;
        }
        out[2 * s] = static_cast<float>(std::tanh(0.9 * l));
        out[2 * s + 1] = static_cast<float>(std::tanh(0.9 * r));
    }
    return out;
}

// A chord's notes voiced between G3 and F#4, the bass two octaves under the root.
std::array<int, 3> voicing(int root, bool minor, bool diminished = false) {
    const int pcs[3] = {root, root + (minor || diminished ? 3 : 4), root + (diminished ? 6 : 7)};
    std::array<int, 3> m{};
    for (int i = 0; i < 3; i++) m[i] = 55 + (((pcs[i] - 55) % 12) + 12) % 12;
    return m;
}

enum class Style { Clicks, Kit, Swung, Waltz };

Song rhythmSong(Style style, double bpm, double seconds) {
    static const char* const kNames[] = {"clicks 4/4", "kit 4/4", "swung kit 4/4", "waltz 3/4"};
    Song s;
    s.group = "rhythm";
    s.name = kNames[static_cast<int>(style)];
    s.seconds = seconds;
    s.bpm = bpm;
    s.meter = style == Style::Waltz ? 3 : 4;
    s.start = 0.137;
    s.key = -1;
    const double p = 60.0 / bpm;
    // C–Am–F–G, a chord a bar.
    constexpr int kRoots[4] = {0, 9, 5, 7};
    constexpr bool kMinor[4] = {false, true, false, false};
    for (int k = 0;; k++) {
        const double t = s.start + k * p;
        if (t >= seconds) break;
        const int slot = k % s.meter;
        const int bar = (k / s.meter) % 4;
        auto add = [&](double at, Hit kind, double gain, double length, std::array<int, 3> midi = {}) {
            s.events.push_back({at, kind, gain, length, midi});
        };
        if (style == Style::Clicks) {
            add(t, slot == 0 ? Hit::ClickAccent : Hit::Click, 1.0, 0.5);
            continue;
        }
        if (style == Style::Waltz) {
            if (slot == 0) {
                add(t, Hit::Kick, 1.0, 0.6);
                add(t, Hit::Bass, 1.0, 0.9 * p, {36 + kRoots[bar]});
            } else {
                add(t, Hit::Chord, 0.8, 0.6 * p, voicing(kRoots[bar], kMinor[bar]));
                add(t, Hit::Snare, 0.35, 0.3);
            }
            add(t, Hit::Hat, slot == 0 ? 1.0 : 0.6, 0.1);
            continue;
        }
        const double swing = style == Style::Swung ? 2.0 / 3.0 : 0.5;
        if (slot == 0 || slot == 2) {
            add(t, Hit::Kick, 1.0, 0.6);
            add(t, Hit::Bass, 1.0, 1.9 * p, {36 + kRoots[bar]});
        } else {
            add(t, Hit::Snare, 1.0, 0.4);
        }
        add(t, Hit::Hat, 1.0, 0.1);
        add(t + swing * p, Hit::Hat, 0.7, 0.1);
        if (style == Style::Swung && slot == 3) add(t + swing * p, Hit::Snare, 0.3, 0.3);  // a ghost on the swung "and"
        if (slot == 0) add(t, Hit::Chord, 1.0, 4.0 * p, voicing(kRoots[bar], kMinor[bar]));
    }
    return s;
}

// Progressions as (degree in semitones, minor, diminished) per bar.
struct Chord {
    int degree;
    bool minor;
};
struct Progression {
    const char* name;
    bool minorKey;
    std::array<Chord, 4> chords;
};
constexpr Progression kProgressions[] = {
    {"I-IV-V-I", false, {{{0, false}, {5, false}, {7, false}, {0, false}}}},
    {"I-vi-IV-V", false, {{{0, false}, {9, true}, {5, false}, {7, false}}}},
    {"i-iv-V-i", true, {{{0, true}, {5, true}, {7, false}, {0, true}}}},
    {"i-VI-III-VII", true, {{{0, true}, {8, false}, {3, false}, {10, false}}}},
};

Song keySong(int tonic, const Progression& prog, double bpm, double seconds, uint32_t seed) {
    static const char* const kPc[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    Song s;
    s.group = "key";
    s.name = std::string(kPc[tonic]) + (prog.minorKey ? " minor " : " major ") + prog.name;
    s.seconds = seconds;
    s.bpm = bpm;
    s.meter = 4;
    s.start = 0.09;
    s.key = tonic + (prog.minorKey ? 12 : 0);
    constexpr int kMajorScale[7] = {0, 2, 4, 5, 7, 9, 11};
    constexpr int kMinorScale[7] = {0, 2, 3, 5, 7, 8, 10};
    const double p = 60.0 / bpm;
    Lcg walk{seed};
    int degree = 0;  // the melody's scale step above the tonic near C5
    for (int k = 0;; k++) {
        const double t = s.start + k * p;
        if (t >= seconds) break;
        const int slot = k % 4;
        const Chord c = prog.chords[static_cast<size_t>((k / 4) % 4)];
        const int root = (tonic + c.degree) % 12;
        auto add = [&](double at, Hit kind, double gain, double length, std::array<int, 3> midi = {}) {
            s.events.push_back({at, kind, gain, length, midi});
        };
        if (slot == 0) add(t, Hit::Chord, 1.0, 4.0 * p, voicing(root, c.minor));
        if (slot == 0 || slot == 2) {
            add(t, Hit::Kick, 0.6, 0.6);
            add(t, Hit::Bass, 1.0, 1.9 * p, {36 + root});
        }
        add(t, Hit::Hat, 0.5, 0.1);
        add(t + 0.5 * p, Hit::Hat, 0.35, 0.1);
        for (int half = 0; half < 2; half++) {
            degree = std::clamp(degree + walk.below(5) - 2, -3, 9);
            const int octave = degree < 0 ? -1 : degree / 7;
            const int step = ((degree % 7) + 7) % 7;
            int pc = prog.minorKey ? kMinorScale[step] : kMajorScale[step];
            if (prog.minorKey && c.degree == 7 && !c.minor && pc == 10) pc = 11;  // the leading tone over V
            add(t + 0.5 * half * p, Hit::Melody, 1.0, 0.45 * p, {72 + tonic % 12 - 12 * (tonic >= 6) + 12 * octave + pc});
        }
    }
    return s;
}

// ---- Running and scoring ---------------------------------------------------------------------------------

struct Frame {
    double t;
    GeodeSoundFrame f;
};

std::vector<Frame> analyse(const std::vector<float>& pcm) {
    SoundEngine engine(kRate);
    const int64_t total = static_cast<int64_t>(pcm.size() / 2);
    std::vector<Frame> frames;
    int64_t done = 0;
    for (int i = 1;; i++) {
        const int64_t end = std::llround(i * kRate / kFps);
        if (end > total) break;
        engine.write(pcm.data() + 2 * done, static_cast<int>(end - done), 2);
        done = end;
        Frame fr{i / kFps, {}};
        engine.step(static_cast<float>(1.0 / kFps), fr.f);
        frames.push_back(fr);
    }
    return frames;
}

// Greedy one-to-one matching within ±tol; returns the F-measure (0 when either side is empty).
double fMeasure(const std::vector<double>& truth, const std::vector<double>& found, double tol) {
    if (truth.empty() || found.empty()) return 0.0;
    std::vector<bool> used(found.size(), false);
    int hits = 0;
    for (double b : truth) {
        int best = -1;
        double bestD = tol;
        for (size_t j = 0; j < found.size(); j++) {
            const double d = std::fabs(found[j] - b);
            if (!used[j] && d <= bestD) {
                bestD = d;
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            used[static_cast<size_t>(best)] = true;
            hits++;
        }
    }
    const double precision = static_cast<double>(hits) / found.size();
    const double recall = static_cast<double>(hits) / truth.size();
    return precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
}

constexpr double kScoreFrom = 5.0;
constexpr double kTol = 0.07;

struct RhythmScore {
    double bpm = 0.0;
    bool acc1 = false;
    bool acc2 = false;
    double beatF = 0.0;
    double downF = 0.0;
    int meter = 0;
    double firstBeat = -1.0;
    double phaseMs = 0.0;  // mean signed offset of the matched beats, ms (+ = late)
};

RhythmScore scoreRhythm(const Song& s, const std::vector<Frame>& frames) {
    RhythmScore r;
    const double p = 60.0 / s.bpm;
    std::vector<double> beats;
    std::vector<double> downs;
    std::vector<double> truthBeats;
    std::vector<double> truthDowns;
    for (int k = 0;; k++) {
        const double t = s.start + k * p;
        if (t >= s.seconds - 0.1) break;
        if (t < kScoreFrom) continue;
        truthBeats.push_back(t);
        if (k % s.meter == 0) truthDowns.push_back(t);
    }
    double offset = 0.0;
    int matched = 0;
    for (const Frame& fr : frames) {
        const GeodeSoundFrame& f = fr.f;
        if (f.beat <= 0.0f || f.bpm <= 0.0f) continue;
        const double at = fr.t - f.beatPhase * 60.0 / f.bpm;
        if (r.firstBeat < 0.0) r.firstBeat = fr.t;
        if (at < kScoreFrom) continue;
        beats.push_back(at);
        if (f.downbeat > 0.0f) downs.push_back(at);
        const double d = std::remainder(at - s.start, p);
        if (std::fabs(d) <= kTol) {
            offset += d;
            matched++;
        }
    }
    r.bpm = frames.empty() ? 0.0 : frames.back().f.bpm;
    const double ratio = r.bpm / s.bpm;
    r.acc1 = std::fabs(ratio - 1.0) <= 0.04;
    r.acc2 = r.acc1;
    for (double m : {2.0, 0.5, 3.0, 1.0 / 3.0}) r.acc2 = r.acc2 || std::fabs(ratio / m - 1.0) <= 0.04;
    r.beatF = fMeasure(truthBeats, beats, kTol);
    r.downF = fMeasure(truthDowns, downs, kTol);
    r.phaseMs = matched > 0 ? 1000.0 * offset / matched : 0.0;
    if (downs.size() >= 3 && r.bpm > 0.0) {
        std::vector<double> gaps;
        for (size_t i = 1; i < downs.size(); i++) gaps.push_back((downs[i] - downs[i - 1]) * r.bpm / 60.0);
        std::nth_element(gaps.begin(), gaps.begin() + static_cast<long>(gaps.size() / 2), gaps.end());
        r.meter = static_cast<int>(std::lround(gaps[gaps.size() / 2]));
    }
    return r;
}

// Key profiles as Essentia ships them (src/algorithms/tonal/key.cpp), tonic first. bgate, braw and
// edma also carry an "other" profile there, left out: this compares major against minor only.
struct Profile {
    const char* name;
    std::array<float, 12> major;
    std::array<float, 12> minor;
};
const Profile kProfiles[] = {
    {"krumhansl", {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f},
     {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f}},
    {"temperley", {5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f},
     {5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f}},
    {"temperley2005", {0.748f, 0.060f, 0.488f, 0.082f, 0.67f, 0.46f, 0.096f, 0.715f, 0.104f, 0.366f, 0.057f, 0.4f},
     {0.712f, 0.084f, 0.474f, 0.618f, 0.049f, 0.46f, 0.105f, 0.747f, 0.404f, 0.067f, 0.133f, 0.33f}},
    {"shaath", {6.6f, 2.0f, 3.5f, 2.3f, 4.6f, 4.0f, 2.5f, 5.2f, 2.4f, 3.7f, 2.3f, 3.4f},
     {6.5f, 2.7f, 3.5f, 5.4f, 2.6f, 3.5f, 2.5f, 5.2f, 4.0f, 2.7f, 4.3f, 3.2f}},
    {"gomez", {0.82f, 0.00f, 0.55f, 0.00f, 0.53f, 0.30f, 0.08f, 1.00f, 0.00f, 0.38f, 0.00f, 0.47f},
     {0.81f, 0.00f, 0.53f, 0.54f, 0.00f, 0.27f, 0.07f, 1.00f, 0.27f, 0.07f, 0.10f, 0.36f}},
    {"noland", {0.0629f, 0.0146f, 0.061f, 0.0121f, 0.0623f, 0.0414f, 0.0248f, 0.0631f, 0.015f, 0.0521f, 0.0142f, 0.0478f},
     {0.0682f, 0.0138f, 0.0543f, 0.0519f, 0.0234f, 0.0544f, 0.0176f, 0.067f, 0.0349f, 0.0297f, 0.0401f, 0.027f}},
    {"thpcp", {0.95162f, 0.20742f, 0.71758f, 0.22007f, 0.71341f, 0.48841f, 0.31431f, 1.0f, 0.20957f, 0.53657f, 0.22585f, 0.55363f},
     {0.94409f, 0.21742f, 0.64525f, 0.63229f, 0.27897f, 0.57709f, 0.26428f, 1.0f, 0.26428f, 0.30633f, 0.45924f, 0.35929f}},
    {"edma", {1.00f, 0.29f, 0.50f, 0.40f, 0.60f, 0.56f, 0.32f, 0.80f, 0.31f, 0.45f, 0.42f, 0.39f},
     {1.00f, 0.31f, 0.44f, 0.58f, 0.33f, 0.49f, 0.29f, 0.78f, 0.43f, 0.29f, 0.53f, 0.32f}},
    {"bgate", {1.00f, 0.00f, 0.42f, 0.00f, 0.53f, 0.37f, 0.00f, 0.77f, 0.00f, 0.38f, 0.21f, 0.30f},
     {1.00f, 0.00f, 0.36f, 0.39f, 0.00f, 0.38f, 0.00f, 0.74f, 0.27f, 0.00f, 0.42f, 0.23f}},
    {"braw", {1.0000f, 0.1573f, 0.4200f, 0.1570f, 0.5296f, 0.3669f, 0.1632f, 0.7711f, 0.1676f, 0.3827f, 0.2113f, 0.2965f},
     {1.0000f, 0.2330f, 0.3615f, 0.3905f, 0.2925f, 0.3777f, 0.1961f, 0.7425f, 0.2701f, 0.2161f, 0.4228f, 0.2272f}},
    {"diatonic", {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1}},
    {"tonictriad", {1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0}, {1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}},
};
constexpr int kProfileCount = static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));

// Pearson correlation of the chroma with a profile rotated to `tonic`, as core/sound/Harmony reads it.
float correlate(const float* c, const std::array<float, 12>& profile, int tonic) {
    float mc = 0.0f;
    float mp = 0.0f;
    for (int i = 0; i < 12; i++) {
        mc += c[i] / 12.0f;
        mp += profile[static_cast<size_t>(i)] / 12.0f;
    }
    float num = 0.0f;
    float dc = 0.0f;
    float dp = 0.0f;
    for (int i = 0; i < 12; i++) {
        const float a = c[(i + tonic) % 12] - mc;
        const float b = profile[static_cast<size_t>(i)] - mp;
        num += a * b;
        dc += a * a;
        dp += b * b;
    }
    return dc > 1e-12f && dp > 1e-12f ? num / std::sqrt(dc * dp) : 0.0f;
}

int bestKey(const float* chroma, const Profile& p) {
    int best = 0;
    float top = -2.0f;
    for (int k = 0; k < 24; k++) {
        const float r = correlate(chroma, k < 12 ? p.major : p.minor, k % 12);
        if (r > top) {
            top = r;
            best = k;
        }
    }
    return best;
}

// MIREX: 1 the key, 0.5 a fifth either way in the same mode, 0.3 the relative, 0.2 the parallel.
double mirex(int truth, int got) {
    if (got < 0) return 0.0;
    if (got == truth) return 1.0;
    const bool tMinor = truth >= 12;
    const bool gMinor = got >= 12;
    const int tt = truth % 12;
    const int gt = got % 12;
    if (tMinor == gMinor && (gt == (tt + 7) % 12 || gt == (tt + 5) % 12)) return 0.5;
    if (!tMinor && gMinor && gt == (tt + 9) % 12) return 0.3;
    if (tMinor && !gMinor && gt == (tt + 3) % 12) return 0.3;
    if (tMinor != gMinor && gt == tt) return 0.2;
    return 0.0;
}

// The key the frame shows: keyRoot is the tonic, and keyHue sits on the relative major for a minor key.
int engineKey(const GeodeSoundFrame& f) {
    if (f.scale < 0.0f) return -1;
    const int tonic = static_cast<int>(std::lround(f.keyRoot * 12.0f)) % 12;
    const int hue = static_cast<int>(std::lround(f.keyHue * 12.0f)) % 12;
    return hue == (tonic * 7) % 12 ? tonic : tonic + 12;
}

std::string keyName(int k) {
    static const char* const kPc[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    if (k < 0) return "-";
    return std::string(kPc[k % 12]) + (k >= 12 ? "m" : "");
}

constexpr double kKeyFrom = 8.0;

struct KeyScore {
    int final = -1;
    double mirexFinal = 0.0;
    double exactShare = 0.0;
    std::array<double, kProfileCount> profileMirex{};
    std::array<double, kProfileCount> profileExact{};
};

KeyScore scoreKey(const Song& s, const std::vector<Frame>& frames) {
    KeyScore k;
    int n = 0;
    int exact = 0;
    for (const Frame& fr : frames) {
        if (fr.t < kKeyFrom) continue;
        n++;
        if (engineKey(fr.f) == s.key) exact++;
        for (int p = 0; p < kProfileCount; p++) {
            const int got = bestKey(fr.f.chromaSlow, kProfiles[p]);
            k.profileMirex[static_cast<size_t>(p)] += mirex(s.key, got);
            k.profileExact[static_cast<size_t>(p)] += got == s.key ? 1.0 : 0.0;
        }
    }
    if (n > 0) {
        for (int p = 0; p < kProfileCount; p++) {
            k.profileMirex[static_cast<size_t>(p)] /= n;
            k.profileExact[static_cast<size_t>(p)] /= n;
        }
        k.exactShare = static_cast<double>(exact) / n;
    }
    k.final = frames.empty() ? -1 : engineKey(frames.back().f);
    k.mirexFinal = mirex(s.key, k.final);
    return k;
}

// ---- The set ---------------------------------------------------------------------------------------------

struct RhythmCase {
    Style style;
    double bpm;
};

}  // namespace

int main(int argc, char** argv) {
    const bool quick = argc > 1 && std::strcmp(argv[1], "--quick") == 0;
    std::vector<RhythmCase> rhythm;
    if (quick) {
        rhythm = {{Style::Clicks, 93}, {Style::Kit, 141}, {Style::Swung, 104}, {Style::Waltz, 150}};
    } else {
        for (double b : {67.0, 84.0, 93.0, 107.0, 128.0, 141.0, 158.0, 174.0}) rhythm.push_back({Style::Clicks, b});
        for (double b : {78.0, 97.0, 113.0, 136.0, 165.0}) rhythm.push_back({Style::Kit, b});
        for (double b : {88.0, 104.0, 132.0, 152.0}) rhythm.push_back({Style::Swung, b});
        for (double b : {84.0, 105.0, 138.0, 150.0, 176.0}) rhythm.push_back({Style::Waltz, b});
    }
    const double rhythmSeconds = quick ? 15.0 : 30.0;
    std::printf("Rhythm (%s): tempo at the end, beat and downbeat F-measure (±70 ms, from 5 s), bar read/true, beat offset, first beat\n", quick ? "quick" : "full");
    std::printf("  %-15s %6s %8s %5s %5s %6s %6s %6s %8s %7s\n", "material", "true", "read", "acc1", "acc2", "beatF", "downF", "bar", "phase", "1st");
    struct Sum {
        int n = 0;
        int acc1 = 0;
        int acc2 = 0;
        double beatF = 0.0;
        double downF = 0.0;
        int meter = 0;
    };
    std::array<Sum, 5> sums{};  // per style, then all
    for (const RhythmCase& c : rhythm) {
        Song s = rhythmSong(c.style, c.bpm, rhythmSeconds);
        const std::vector<Frame> frames = analyse(render(s, 0xBEA7u + static_cast<uint32_t>(c.bpm)));
        const RhythmScore r = scoreRhythm(s, frames);
        std::printf("  %-15s %6.1f %8.2f %5s %5s %6.3f %6.3f %3d/%d %+6.1fms %6.2fs\n", s.name.c_str(), s.bpm, r.bpm, r.acc1 ? "yes" : "no",
                    r.acc2 ? "yes" : "no", r.beatF, r.downF, r.meter, s.meter, r.phaseMs, r.firstBeat);
        for (Sum* sum : {&sums[static_cast<size_t>(c.style)], &sums[4]}) {
            sum->n++;
            sum->acc1 += r.acc1;
            sum->acc2 += r.acc2;
            sum->beatF += r.beatF;
            sum->downF += r.downF;
            sum->meter += r.meter == s.meter;
        }
    }
    static const char* const kStyleNames[] = {"clicks 4/4", "kit 4/4", "swung kit 4/4", "waltz 3/4", "all"};
    std::printf("\n  %-15s %4s %6s %6s %7s %7s %6s\n", "summary", "n", "Acc1", "Acc2", "beatF", "downF", "meter");
    for (size_t i = 0; i < sums.size(); i++) {
        const Sum& m = sums[i];
        if (m.n == 0) continue;
        std::printf("  %-15s %4d %6.3f %6.3f %7.3f %7.3f %6.3f\n", kStyleNames[i], m.n, static_cast<double>(m.acc1) / m.n,
                    static_cast<double>(m.acc2) / m.n, m.beatF / m.n, m.downF / m.n, static_cast<double>(m.meter) / m.n);
    }

    const double keySeconds = quick ? 14.0 : 20.0;
    std::printf("\nKey (%s): the engine's key at the end and the share of frames from 8 s on with the exact key\n", quick ? "quick" : "full");
    int keyN = 0;
    double keyMirex = 0.0;
    int keyExactFinal = 0;
    double keyShare = 0.0;
    std::array<double, kProfileCount> profMirex{};
    std::array<double, kProfileCount> profExact{};
    std::array<std::array<double, 2>, 2> byMode{};  // [minor][mirex, n]
    std::vector<std::array<int, 2>> pieces;  // tonic, progression
    if (quick) {
        pieces = {{0, 1}, {5, 2}, {10, 0}, {4, 3}};
    } else {
        for (int ti = 0; ti < 12; ti++) {
            for (int pi = 0; pi < 4; pi++) pieces.push_back({ti, pi});
        }
    }
    for (const auto& [tonic, pi] : pieces) {
        const Progression& prog = kProgressions[pi];
        const double bpm = 72.0 + 7.0 * ((tonic * 4 + pi) % 13);
        Song s = keySong(tonic, prog, bpm, keySeconds, 0x1234u + static_cast<uint32_t>(tonic * 4 + pi));
        const std::vector<Frame> frames = analyse(render(s, 0xC0DEu + static_cast<uint32_t>(tonic * 4 + pi)));
        const KeyScore k = scoreKey(s, frames);
        std::printf("  %-22s read %-4s mirex %.1f  exact %5.1f %%\n", s.name.c_str(), keyName(k.final).c_str(), k.mirexFinal, 100.0 * k.exactShare);
        keyN++;
        keyMirex += k.mirexFinal;
        keyExactFinal += k.final == s.key;
        keyShare += k.exactShare;
        byMode[prog.minorKey][0] += k.mirexFinal;
        byMode[prog.minorKey][1] += 1.0;
        for (int p = 0; p < kProfileCount; p++) {
            profMirex[static_cast<size_t>(p)] += k.profileMirex[static_cast<size_t>(p)];
            profExact[static_cast<size_t>(p)] += k.profileExact[static_cast<size_t>(p)];
        }
    }
    std::printf("\n  engine: %d pieces, exact at the end %.3f, MIREX at the end %.3f (major %.3f, minor %.3f), exact frames %.3f\n", keyN,
                static_cast<double>(keyExactFinal) / keyN, keyMirex / keyN, byMode[0][0] / std::max(1.0, byMode[0][1]),
                byMode[1][0] / std::max(1.0, byMode[1][1]), keyShare / keyN);
    std::printf("\n  profiles on the engine's slow chroma, every frame from 8 s on (no hysteresis):\n");
    std::printf("  %-14s %7s %7s\n", "profile", "exact", "MIREX");
    for (int p = 0; p < kProfileCount; p++) {
        std::printf("  %-14s %7.3f %7.3f\n", kProfiles[p].name, profExact[static_cast<size_t>(p)] / keyN, profMirex[static_cast<size_t>(p)] / keyN);
    }
    return 0;
}
