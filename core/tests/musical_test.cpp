// Host test for the musical layer (core/sound: Rhythm, Harmony, Form) and the musical intelligence
// database's scale catalogue (core/sound/ScaleTable.inc), built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Icore core/tests/musical_test.cpp core/sound/*.cpp -o musical_test && ./musical_test
// Synthetic signals only, so every number here is reproducible.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "sound/Form.hpp"
#include "sound/Harmony.hpp"
#include "sound/SoundEngine.hpp"

using namespace geode::sound;

namespace {

int failures = 0;

#define CHECK(cond, ...)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            failures++;                                                \
            std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                  \
            std::printf("\n");                                         \
        }                                                              \
    } while (0)

constexpr double kTau = 2.0 * std::numbers::pi;

double hzOf(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

// Writes exactly the audio up to each frame's end, then steps: what the export path does.
GeodeSoundFrame run(SoundEngine& e, const std::function<float(double)>& signal, double seconds, double fps = 60.0) {
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    GeodeSoundFrame f{};
    int64_t done = 0;
    for (int i = 1; i <= static_cast<int>(seconds * fps); i++) {
        const int64_t end = std::llround(i * rate / fps);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) buf[2 * (s - done)] = buf[2 * (s - done) + 1] = signal(static_cast<double>(s) / rate);
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(static_cast<float>(1.0 / fps), f);
    }
    return f;
}

// The catalogue as the docs list it: ids unique, every mask holds its root, Ionian first.
void testCatalogue() {
    CHECK(scaleCount() == 46, "%d scales", scaleCount());
    CHECK(std::string(scaleInfo(0).id) == "ionian", "%s", scaleInfo(0).id);
    for (int i = 0; i < scaleCount(); i++) {
        const ScaleInfo& s = scaleInfo(i);
        CHECK((s.mask & 1u) != 0, "%s has no root", s.id);
        CHECK(s.brightness >= -1.0f && s.brightness <= 1.0f && s.tension >= 0.0f && s.tension <= 1.0f, "%s out of range", s.id);
        for (int j = 0; j < i; j++) CHECK(std::string(s.id) != scaleInfo(j).id, "%s twice", s.id);
    }
}

// A melody walking a scale over a drone on its root names that scale, on that root.
void testScales() {
    struct Case {
        const char* id;
        int root;
        std::vector<int> steps;
    };
    const Case cases[] = {
        {"ionian", 60, {0, 2, 4, 5, 7, 9, 11}},     {"aeolian", 57, {0, 2, 3, 5, 7, 8, 10}},
        {"dorian", 62, {0, 2, 3, 5, 7, 9, 10}},     {"lydian", 65, {0, 2, 4, 6, 7, 9, 11}},
        {"phrygian", 64, {0, 1, 3, 5, 7, 8, 10}},   {"minor_pentatonic", 57, {0, 3, 5, 7, 10}},
        {"whole_tone", 60, {0, 2, 4, 6, 8, 10}},    {"harmonic_minor", 64, {0, 2, 3, 5, 7, 8, 11}},
    };
    float lydianBright = 0.0f;
    float phrygianBright = 1.0f;
    for (const Case& c : cases) {
        SoundEngine e(44100);
        const GeodeSoundFrame f = run(e, [&](double t) {
            const int step = c.steps[static_cast<size_t>(t / 0.25) % c.steps.size()];
            const double hz = hzOf(c.root + step);
            const double drone = hzOf(c.root);
            return static_cast<float>(0.2 * std::sin(kTau * hz * t) + 0.1 * std::sin(2 * kTau * hz * t) + 0.12 * std::sin(kTau * drone * t));
        }, 20.0);
        const char* got = f.scale >= 0.0f ? scaleInfo(static_cast<int>(f.scale)).id : "-";
        CHECK(std::string(got) == c.id, "%s on %d read as %s", c.id, c.root, got);
        CHECK(std::lround(f.scaleRoot * 12.0f) == c.root % 12, "%s root %f", c.id, f.scaleRoot * 12.0f);
        CHECK(f.scaleFit > 0.5f, "%s fit %f", c.id, f.scaleFit);
        if (std::string(c.id) == "lydian") lydianBright = f.scaleBrightness;
        if (std::string(c.id) == "phrygian") phrygianBright = f.scaleBrightness;
    }
    CHECK(lydianBright > 0.8f && phrygianBright < 0.3f, "lydian %f, phrygian %f", lydianBright, phrygianBright);
}

// Kick-like click tracks with an accented first beat: tempo within 2%, locked, every beat trigger within
// 50 ms of a click from 10 s on, every downbeat on an accented click, and the beat phase on the click: its
// mean signed offset from the clicks within 5 ms (beat triggers land on frames, 17 ms apart; the phase is
// read at the frame's own instant, so it says where the oscillator puts the beat).
void testClickTracks() {
    for (double bpm : {90.0, 128.0, 174.0}) {
        const double period = 60.0 / bpm;
        SoundEngine e(44100);
        int beats = 0;
        int aligned = 0;
        int downs = 0;
        int downAligned = 0;
        double offset = 0.0;  // the beat phase's signed lead over the clicks, s, summed over locked frames
        int phased = 0;
        GeodeSoundFrame last{};
        const int rate = e.sampleRateHz();
        std::vector<float> buf;
        int64_t done = 0;
        for (int i = 1; i <= 60 * 20; i++) {
            const int64_t end = std::llround(i * rate / 60.0);
            buf.resize(static_cast<size_t>(2 * (end - done)));
            for (int64_t s = done; s < end; s++) {
                const double t = static_cast<double>(s) / rate;
                const int k = static_cast<int>(t / period);
                const double u = t - k * period;
                const double amp = k % 4 == 0 ? 0.8 : 0.35;
                const float v = static_cast<float>(amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) +
                                                   0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u));
                buf[2 * (s - done)] = buf[2 * (s - done) + 1] = v;
            }
            e.write(buf.data(), static_cast<int>(end - done), 2);
            done = end;
            e.step(1.0f / 60.0f, last);
            const double t = i / 60.0;
            if (t <= 10.0) continue;
            if (last.beatConf > 0.35f) {
                offset += std::remainder(static_cast<double>(last.beatPhase) - t / period, 1.0) * period;
                phased++;
            }
            if (last.beat > 0.0f) {
                beats++;
                if (std::fabs(std::remainder(t, period)) < 0.05) aligned++;
            }
            if (last.downbeat > 0.0f) {
                downs++;
                if (std::fabs(std::remainder(t, 4.0 * period)) < 0.05) downAligned++;
            }
        }
        const int expected = static_cast<int>(10.0 / period);
        const double lead = phased > 0 ? offset / phased : 1.0;
        std::printf("  click %3.0f BPM: beat phase %+.1f ms from the clicks\n", bpm, 1000.0 * lead);
        CHECK(std::fabs(lead) < 0.005, "%.0f BPM: the beat phase runs %+.1f ms from the clicks", bpm, 1000.0 * lead);
        CHECK(std::fabs(last.bpm / bpm - 1.0) < 0.02, "%.0f BPM read %.2f", bpm, last.bpm);
        CHECK(last.beatConf > 0.35f, "%.0f BPM confidence %f", bpm, last.beatConf);
        CHECK(beats >= expected - 1 && aligned == beats, "%.0f BPM: %d beats (%d expected), %d on a click", bpm, beats, expected, aligned);
        CHECK(downs >= expected / 4 - 1 && downAligned == downs, "%.0f BPM: %d downbeats, %d on beat one", bpm, downs, downAligned);
    }
}

// I–vi–IV–V in C, two seconds a chord: C major, and a harmonic change near most chord changes.
void testChordProgression() {
    const int chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
    SoundEngine e(44100);
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    int64_t done = 0;
    GeodeSoundFrame f{};
    int changes = 0;
    int nearBoundary = 0;
    for (int i = 1; i <= 60 * 32; i++) {
        const int64_t end = std::llround(i * rate / 60.0);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) {
            const double t = static_cast<double>(s) / rate;
            const int* c = chords[static_cast<int>(t / 2.0) % 4];
            double v = 0.0;
            for (int n = 0; n < 3; n++) {
                const double hz = hzOf(c[n] + 12);
                v += 0.1 * std::sin(kTau * hz * t) + 0.04 * std::sin(2 * kTau * hz * t);
            }
            buf[2 * (s - done)] = buf[2 * (s - done) + 1] = static_cast<float>(v);
        }
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(1.0f / 60.0f, f);
        const double t = i / 60.0;
        if (t > 8.0 && f.harmonicChange > 0.0f) {
            changes++;
            const double since = std::fmod(t, 2.0);
            if (since < 0.8) nearBoundary++;
        }
    }
    CHECK(std::lround(f.keyRoot * 12.0f) == 0 && f.keyMajor > 0.5f, "key root %f major %f", f.keyRoot * 12.0f, f.keyMajor);
    CHECK(changes >= 9 && nearBoundary == changes, "%d harmonic changes in 12 boundaries, %d near one", changes, nearBoundary);
    float seventh = f.tension;
    SoundEngine cluster(44100);
    const GeodeSoundFrame g = run(cluster, [](double t) {
        double v = 0.0;
        for (int n : {60, 61, 62, 66}) v += 0.1 * std::sin(kTau * hzOf(n + 12) * t);
        return static_cast<float>(v);
    }, 6.0);
    CHECK(g.tension > seventh + 0.2f, "a cluster (%f) is tenser than triads (%f)", g.tension, seventh);
}

struct Noise {
    uint32_t state = 7;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

// 120 BPM: a verse (kick, bass, hats) to 24 s, an 8-bar build (no kick or bass; rising noise, a rising
// sweep and a snare roll that doubles every two bars) to 40 s, then the drop (kick and a louder bass).
void testBuildAndDrop() {
    SoundEngine e(44100);
    Noise noise;
    const double p = 0.5;
    float buildPeak = 0.0f;
    bool sawBuildState = false;
    double dropAt = -1.0;
    int dropState = -1;
    float verseId = -1.0f;
    float dropId = -1.0f;
    float weakestDrop = 2.0f;
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    int64_t done = 0;
    GeodeSoundFrame f{};
    for (int i = 1; i <= 60 * 56; i++) {
        const int64_t end = std::llround(i * rate / 60.0);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) {
            const double t = static_cast<double>(s) / rate;
            const double u = std::fmod(t, p);
            double v = 0.0;
            if (t < 24.0 || t >= 40.0) {
                v += 0.6 * std::exp(-u * 20.0) * std::sin(kTau * 55.0 * u);
                v += 0.25 * std::sin(kTau * 55.0 * t) * (t >= 40.0 ? 1.4 : 1.0);
                v += 0.05 * std::exp(-std::fmod(t, p / 2) * 300.0) * noise.next();
            } else {
                const double x = (t - 24.0) / 16.0;
                v += (0.02 + 0.2 * x) * noise.next();
                v += 0.05 * std::sin(kTau * (1000.0 + 5000.0 * x) * t);
                const double div = x < 0.25 ? p : x < 0.5 ? p / 2 : x < 0.75 ? p / 4 : p / 8;
                const double r = std::fmod(t, div);
                v += (0.2 + 0.3 * x) * std::exp(-r * 60.0) * (0.7 * noise.next() + 0.3 * std::sin(kTau * 200.0 * r));
            }
            buf[2 * (s - done)] = buf[2 * (s - done) + 1] = static_cast<float>(v);
        }
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(1.0f / 60.0f, f);
        const double t = i / 60.0;
        if (t > 28.0 && t < 40.0) {
            buildPeak = std::max(buildPeak, f.buildUp);
            sawBuildState = sawBuildState || std::lround(f.sectionState) == GEODE_SECTION_BUILD;
        }
        if (t > 20.0 && t < 24.0) verseId = f.sectionId;
        if (f.drop > 0.0f && dropAt < 0.0) dropAt = t;
        if (f.drop > 0.0f) weakestDrop = std::min(weakestDrop, f.drop);
        if (std::fabs(t - 42.0) < 1e-6) dropState = static_cast<int>(std::lround(f.sectionState));
        if (std::fabs(t - 50.0) < 1e-6) dropId = f.sectionId;
    }
    CHECK(buildPeak > 0.5f, "build-up peaked at %f", buildPeak);
    CHECK(sawBuildState, "the section state never read Build");
    CHECK(dropAt >= 40.0 && dropAt < 40.5, "drop at %f", dropAt);
    CHECK(dropState == GEODE_SECTION_DROP, "state %d after the drop", dropState);
    CHECK(dropId == verseId, "the drop's kick and bass (%f) are the verse's material (%f)", dropId, verseId);
    // Form's one floor: an event fires at kEventFloor..1 or not at all (a 0.3 never reaches a consumer).
    CHECK(weakestDrop >= Form::kEventFloor && weakestDrop <= 1.0f, "a drop fired at %f, outside kEventFloor..1", weakestDrop);
}

// A B A, 16 bars each at 120 BPM: A a C major pad with kick and hats, B an F# pad over a low buzz. The
// change into B and back into A fires within two bars, nowhere else, and A comes back under its own id.
void testSectionsReturn() {
    SoundEngine e(44100);
    Noise noise;
    const double p = 0.5;
    std::vector<double> changes;
    float idA = -1.0f;
    float idB = -1.0f;
    float idA2 = -1.0f;
    float weakest = 2.0f;
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    int64_t done = 0;
    GeodeSoundFrame f{};
    for (int i = 1; i <= 60 * 96; i++) {
        const int64_t end = std::llround(i * rate / 60.0);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) {
            const double t = static_cast<double>(s) / rate;
            const double u = std::fmod(t, p);
            const bool b = t >= 32.0 && t < 64.0;
            double v = 0.5 * std::exp(-u * 20.0) * std::sin(kTau * 50.0 * u);
            for (int n : b ? std::vector<int>{66, 70, 73} : std::vector<int>{60, 64, 67}) v += 0.06 * std::sin(kTau * hzOf(n + 12) * t);
            if (b) {
                v += 0.15 * (std::fmod(t * 82.0, 1.0) * 2.0 - 1.0);
            } else {
                v += 0.06 * std::exp(-std::fmod(t, p / 2) * 300.0) * noise.next();
            }
            buf[2 * (s - done)] = buf[2 * (s - done) + 1] = static_cast<float>(v);
        }
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(1.0f / 60.0f, f);
        const double t = i / 60.0;
        if (f.sectionChange > 0.0f) changes.push_back(t);
        if (f.sectionChange > 0.0f) weakest = std::min(weakest, f.sectionChange);
        if (f.drop > 0.0f) weakest = std::min(weakest, f.drop);
        if (std::fabs(t - 30.0) < 1e-6) idA = f.sectionId;
        if (std::fabs(t - 60.0) < 1e-6) idB = f.sectionId;
        if (std::fabs(t - 90.0) < 1e-6) idA2 = f.sectionId;
    }
    bool intoB = false;
    bool backToA = false;
    bool stray = false;
    for (double t : changes) {
        if (t >= 32.0 && t <= 36.5) {
            intoB = true;
        } else if (t >= 64.0 && t <= 68.5) {
            backToA = true;
        } else {
            stray = true;
        }
    }
    CHECK(intoB && backToA && !stray, "%zu section changes; into B %d, back to A %d, stray %d", changes.size(), intoB, backToA, stray);
    CHECK(idA == idA2 && idA != idB, "ids A %f, B %f, A again %f", idA, idB, idA2);
    CHECK(weakest >= Form::kEventFloor, "an event fired at %f, under Form's floor", weakest);
}

// The same input gives the same frames, bit for bit (live and export agree).
void testDeterministic() {
    auto music = [](double t) {
        const double beat = std::fmod(t, 0.5);
        return static_cast<float>(0.5 * std::exp(-beat * 30.0) * std::sin(kTau * 55.0 * t) + 0.1 * std::sin(kTau * hzOf(64) * t));
    };
    SoundEngine a(48000);
    SoundEngine b(48000);
    const GeodeSoundFrame fa = run(a, music, 12.0);
    const GeodeSoundFrame fb = run(b, music, 12.0);
    CHECK(std::memcmp(&fa, &fb, sizeof fa) == 0, "two runs diverged");
}


// ---- Beat tracking on synthesized grooves ------------------------------------------------------------
//
// Drum-machine voices on a bar of four beats, so a groove states where its beats are and the tracker's
// tempo and beat phase can be scored against them. Each voice is shaped like the real thing in the bands
// the tracker hears: a kick sweeps 150 → 50 Hz with a short click, a bass note is a sustained 55 Hz sine
// with its octave, hats are high-passed noise (closed: 12 ms, open: 100 ms), a snare is noise over a body.
enum class Voice { kKick, kSnare, kClosedHat, kOpenHat, kBass };

struct Part {
    Voice voice;
    double gain;
    std::vector<double> beats;  // positions in the bar, in beats, 0 ≤ b < 4
    double from = 0.0;          // seconds: the part plays only from here
};

struct Groove {
    const char* name;
    double bpm;
    std::vector<Part> parts;
    double seconds = 30.0;
    double settle = 12.0;  // beats before this are not scored, and the tracker must have locked by then
    double otherLevel = 0.0;  // another metrical level that also counts as right (half-time), BPM
    // Why the tracker still gets this groove wrong, or null. A known failure is reported, not counted,
    // and counted the day it passes, so the mark is taken off with the fix.
    const char* knownFailure = nullptr;
};

double voiceAt(Voice v, double u, double white, double hp) {
    switch (v) {
        case Voice::kKick: {
            const double phase = kTau * (50.0 * u + (100.0 / 30.0) * (1.0 - std::exp(-30.0 * u)));
            return std::exp(-u * 18.0) * std::sin(phase) + 0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u);
        }
        case Voice::kSnare:
            return std::exp(-u * 20.0) * (0.7 * white + 0.3 * std::sin(kTau * 185.0 * u));
        case Voice::kClosedHat:
            return std::exp(-u * 80.0) * hp;
        case Voice::kOpenHat:
            return std::exp(-u * 10.0) * hp;
        case Voice::kBass: {
            const double env = std::min(1.0, u / 0.006) * (u < 0.2 ? 1.0 : std::exp(-(u - 0.2) * 60.0));
            return env * (std::sin(kTau * 55.0 * u) + 0.4 * std::sin(kTau * 110.0 * u));
        }
    }
    return 0.0;
}

struct GrooveSynth {
    const Groove& g;
    Noise noise;
    double prevWhite = 0.0;

    explicit GrooveSynth(const Groove& groove) : g(groove) {}
    float at(double t) {
        const double white = noise.next();
        const double hp = 0.5 * (white - prevWhite);
        prevWhite = white;
        const double period = 60.0 / g.bpm;
        const double bar = std::floor(t / (4.0 * period));
        double v = 0.0;
        for (const Part& p : g.parts) {
            // The part's latest hit at or before t, in this bar or the last.
            double last = -1.0;
            for (double k : {bar - 1.0, bar}) {
                for (double b : p.beats) {
                    const double te = (4.0 * k + b) * period;
                    if (te <= t && te >= p.from && te > last) last = te;
                }
            }
            if (last >= 0.0 && t - last < 1.0) v += p.gain * voiceAt(p.voice, t - last, white, hp);
        }
        return static_cast<float>(v);
    }
};

struct BeatScore {
    float bpm = 0.0f;
    double level = 0.0;     // the groove's level the reading is scored at, BPM
    double lockAt = -1.0;   // the first beat trigger, s
    int beats = 0;          // beat triggers after settle
    int onBeat = 0;         // ... within 50 ms of a beat
    int offBeat = 0;        // ... within 50 ms of the "and"
    double phaseErr = 1.0;  // mean |beatPhase − true phase| after settle, in beats
};

BeatScore listen(const Groove& g) {
    SoundEngine e(44100);
    GrooveSynth synth(g);
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    std::vector<double> beatTimes;
    std::vector<std::pair<double, float>> phases;
    GeodeSoundFrame f{};
    BeatScore s;
    int64_t done = 0;
    for (int i = 1; i <= static_cast<int>(g.seconds * 60.0); i++) {
        const int64_t end = std::llround(i * rate / 60.0);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t k = done; k < end; k++) buf[2 * (k - done)] = buf[2 * (k - done) + 1] = synth.at(static_cast<double>(k) / rate);
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(1.0f / 60.0f, f);
        const double t = i / 60.0;
        if (f.beat > 0.0f && s.lockAt < 0.0) s.lockAt = t;
        if (t <= g.settle) continue;
        if (f.beat > 0.0f) beatTimes.push_back(t);
        phases.emplace_back(t, f.beatPhase);
    }
    s.bpm = f.bpm;
    s.level = g.bpm;
    if (g.otherLevel > 0.0 && std::fabs(std::log(s.bpm / g.otherLevel)) < std::fabs(std::log(s.bpm / g.bpm))) s.level = g.otherLevel;
    const double period = 60.0 / s.level;
    s.beats = static_cast<int>(beatTimes.size());
    for (double t : beatTimes) {
        if (std::fabs(std::remainder(t, period)) < 0.05) s.onBeat++;
        if (std::fabs(std::remainder(t - 0.5 * period, period)) < 0.05) s.offBeat++;
    }
    double err = 0.0;
    for (const auto& [t, phase] : phases) err += std::fabs(std::remainder(phase - t / period, 1.0));
    s.phaseErr = phases.empty() ? 1.0 : err / static_cast<double>(phases.size());
    std::printf("  %-26s %6.1f BPM read %7.2f  lock %5.2f s  %3d beats, %3d on, %3d on the and  phase err %.3f\n",
                g.name, g.bpm, s.bpm, s.lockAt, s.beats, s.onBeat, s.offBeat, s.phaseErr);
    return s;
}

// The groove is heard right: the tempo of one of its levels within 2 %, locked before scoring starts, a
// beat trigger on nearly every beat and 90 % of them within 50 ms of the beat, and the beat phase a
// fifth of a beat or less off on average.
void checkGroove(const Groove& g) {
    const BeatScore s = listen(g);
    const double expected = (g.seconds - g.settle) * s.level / 60.0;
    const bool tempo = std::fabs(s.bpm / s.level - 1.0) < 0.02;
    const bool locked = s.lockAt > 0.0 && s.lockAt <= g.settle;
    const bool counted = s.beats >= expected - 2.0;
    const bool onBeat = s.onBeat >= 0.9 * s.beats && s.offBeat == 0;
    const bool phase = s.phaseErr < 0.1;
    if (g.knownFailure != nullptr) {
        const bool passes = tempo && locked && counted && onBeat && phase;
        if (!passes) std::printf("    known failure: %s\n", g.knownFailure);
        CHECK(!passes, "%s now passes: take off its known-failure mark", g.name);
        return;
    }
    CHECK(tempo, "%s: %.1f BPM read %.2f", g.name, g.bpm, s.bpm);
    CHECK(locked, "%s: locked at %.2f s (by %.0f s)", g.name, s.lockAt, g.settle);
    CHECK(counted, "%s: %d beats, %.0f expected", g.name, s.beats, expected);
    CHECK(onBeat, "%s: %d beats, %d on the beat, %d on the and", g.name, s.beats, s.onBeat, s.offBeat);
    CHECK(phase, "%s: beat phase off by %.3f of a beat on average", g.name, s.phaseErr);
}

// The kick and an off-beat bass note are both rises in the low bands, and no onset function tells them
// apart: the beat needs kick events that reject bass notes, then fosfora's phase arbiter on top of them
// (docs/research/fosfora/02-rhythm-structure.md R2; its kick model is fosfora's kick_model.rs).
constexpr const char* kNeedsKickModel = "the low bands hear the bass as a kick; needs a kick model and the phase arbiter (fosfora R2)";

// The fosfora study's two failures (PR #300, docs/research/fosfora/02-rhythm-structure.md §4), at 120 BPM:
// a kick on every beat under an open hat on every "and" (the hat's peak 18 dB and 4 dB under the kick's),
// and a kick on 1 and 3 under a bass note on every "and", the bass quiet and loud.
void testOffbeatGrooves() {
    const std::vector<double> fours = {0, 1, 2, 3};
    const std::vector<double> ands = {0.5, 1.5, 2.5, 3.5};
    std::printf("off-beat grooves:\n");
    for (double hat : {0.1, 0.5}) {
        char name[48];
        std::snprintf(name, sizeof name, "kick 1-4, open hat %.1f", hat);
        checkGroove({name, 120.0, {{Voice::kKick, 0.8, fours}, {Voice::kOpenHat, hat, ands}}});
    }
    for (double bass : {0.15, 0.4}) {
        char name[48];
        std::snprintf(name, sizeof name, "kick 1+3, bass %.2f on and", bass);
        Groove g{name, 120.0, {{Voice::kKick, 0.8, {0, 2}}, {Voice::kBass, bass, ands}}};
        g.knownFailure = kNeedsKickModel;
        checkGroove(g);
    }
}

// The regression set every tracker change must keep: the common shapes of a beat, one each.
void testGrooveRegressions() {
    const std::vector<double> fours = {0, 1, 2, 3};
    const std::vector<double> eighths = {0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5};
    const std::vector<double> ands = {0.5, 1.5, 2.5, 3.5};
    std::printf("groove regressions:\n");
    checkGroove({"four on the floor", 124.0, {{Voice::kKick, 0.8, fours}, {Voice::kSnare, 0.3, {1, 3}}, {Voice::kClosedHat, 0.15, eighths}}});
    // Kick on 1 and the "and" of 3, snare on 2 and 4: the snare holds the grid.
    Groove breakbeat{"breakbeat", 96.0, {{Voice::kKick, 0.8, {0, 2.5}}, {Voice::kSnare, 0.4, {1, 3}}, {Voice::kClosedHat, 0.15, eighths}}};
    breakbeat.knownFailure = "the faster-level rule takes the hats' 8ths (193 BPM); tempo levels are the study's R8/R9";
    checkGroove(breakbeat);
    // Hats on the 140 grid, the kick on 1 and the snare on 3: 140 or the felt 70 are both right.
    Groove half{"half-time", 140.0, {{Voice::kKick, 0.8, {0}}, {Voice::kSnare, 0.4, {2}}, {Voice::kClosedHat, 0.15, eighths}}};
    half.otherLevel = 70.0;
    checkGroove(half);
    const std::vector<double> swung = {0, 2.0 / 3.0, 1, 5.0 / 3.0, 2, 8.0 / 3.0, 3, 11.0 / 3.0};
    checkGroove({"swung 8ths", 110.0, {{Voice::kKick, 0.8, {0, 2}}, {Voice::kSnare, 0.4, {1, 3}}, {Voice::kClosedHat, 0.2, swung}}});
    // House: a kick on every beat and the bass on every "and".
    Groove house{"off-beat bass", 124.0, {{Voice::kKick, 0.8, fours}, {Voice::kBass, 0.3, ands}, {Voice::kClosedHat, 0.1, ands}}};
    house.knownFailure = kNeedsKickModel;
    checkGroove(house);
    // Eight seconds of hats alone, then the kick: the grid lands on the kick once it comes.
    Groove intro{"hat intro, then kick", 128.0, {{Voice::kClosedHat, 0.2, eighths}, {Voice::kKick, 0.8, fours, 8.0}}};
    intro.seconds = 34.0;
    intro.settle = 18.0;
    checkGroove(intro);
}


// Music without a beat never locks: a pad that swells slowly, steady noise, and noise bursts at random
// times (an irregular texture on no grid). The tracker may guess a tempo; it must not fire beats.
void testBeatless() {
    std::printf("beatless:\n");
    struct Case {
        const char* name;
        std::function<float(double, Noise&)> signal;
        const char* knownFailure = nullptr;
    };
    const Case cases[] = {
        {"pad", [](double t, Noise&) {
             double v = 0.0;
             for (int n : {57, 60, 64, 69}) v += 0.08 * std::sin(kTau * hzOf(n) * t);
             return static_cast<float>(v * (0.6 + 0.4 * std::sin(kTau * 0.07 * t)));
         }},
        {"steady noise", [](double, Noise& n) { return 0.2f * n.next(); }},
        {"random bursts", [](double t, Noise& n) {
             // Bursts at Poisson times (mean gap 0.3 s, off any grid), each decaying in 40 ms.
             static const std::vector<double> starts = [] {
                 std::vector<double> s;
                 uint32_t state = 12345;
                 for (double at = 0.0; at < 60.0;) {
                     state = state * 1664525u + 1013904223u;
                     at += -0.3 * std::log((static_cast<double>(state >> 8) + 1.0) / 16777217.0);
                     s.push_back(at);
                 }
                 return s;
             }();
             const auto it = std::upper_bound(starts.begin(), starts.end(), t);
             if (it == starts.begin()) return 0.0f;
             const double u = t - *(it - 1);
             return static_cast<float>(0.5 * std::exp(-u * 25.0) * n.next() + 0.4 * std::exp(-u * 25.0) * std::sin(kTau * 70.0 * u));
         },
         "random kick-like bursts still reach the lock line now and then; the lock has no beat-support test yet (fosfora's support EMA)"},
    };
    for (const Case& c : cases) {
        SoundEngine e(44100);
        Noise noise;
        int beats = 0;
        const int rate = e.sampleRateHz();
        std::vector<float> buf;
        GeodeSoundFrame f{};
        int64_t done = 0;
        for (int i = 1; i <= 60 * 40; i++) {
            const int64_t end = std::llround(i * rate / 60.0);
            buf.resize(static_cast<size_t>(2 * (end - done)));
            for (int64_t k = done; k < end; k++) buf[2 * (k - done)] = buf[2 * (k - done) + 1] = c.signal(static_cast<double>(k) / rate, noise);
            e.write(buf.data(), static_cast<int>(end - done), 2);
            done = end;
            e.step(1.0f / 60.0f, f);
            if (f.beat > 0.0f) beats++;
        }
        std::printf("  %-26s %d beat triggers in 40 s, confidence %.2f\n", c.name, beats, f.beatConf);
        if (c.knownFailure != nullptr) {
            if (beats > 0) std::printf("    known failure: %s\n", c.knownFailure);
            CHECK(beats > 0, "%s now passes: take off its known-failure mark", c.name);
            continue;
        }
        CHECK(beats == 0, "%s: %d beat triggers", c.name, beats);
    }
}

}  // namespace

int main() {
    testCatalogue();
    testScales();
    testClickTracks();
    testChordProgression();
    testBuildAndDrop();
    testSectionsReturn();
    testDeterministic();
    testOffbeatGrooves();
    testGrooveRegressions();
    testBeatless();
    if (failures == 0) std::printf("musical_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
