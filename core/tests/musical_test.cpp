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
        // The reading every two seconds, printed only when the last one is wrong: where it went astray.
        std::string trail;
        GeodeSoundFrame f{};
        for (int part = 0; part < 10; part++) {
            f = run(e, [&, part](double t) {
                t += 2.0 * part;
                const int step = c.steps[static_cast<size_t>(t / 0.25) % c.steps.size()];
                const double hz = hzOf(c.root + step);
                // The drone marks the root, so it sits where the chroma listens (Harmony hears 250–2000 Hz):
                // A3 is 220 Hz, and the A cases drone an octave up instead of going unheard.
                const double drone = hzOf(hzOf(c.root) < 250.0 ? c.root + 12 : c.root);
                return static_cast<float>(0.2 * std::sin(kTau * hz * t) + 0.1 * std::sin(2 * kTau * hz * t) + 0.12 * std::sin(kTau * drone * t));
            }, 2.0);
            char at[64];
            std::snprintf(at, sizeof at, " %ds:%s/%ld", 2 * part + 2, f.scale >= 0.0f ? scaleInfo(static_cast<int>(f.scale)).id : "-",
                          std::lround(f.scaleRoot * 12.0f));
            trail += at;
        }
        const char* got = f.scale >= 0.0f ? scaleInfo(static_cast<int>(f.scale)).id : "-";
        CHECK(std::string(got) == c.id, "%s on %d read as %s;%s", c.id, c.root, got, trail.c_str());
        CHECK(std::lround(f.scaleRoot * 12.0f) == c.root % 12, "%s root %f", c.id, f.scaleRoot * 12.0f);
        CHECK(f.scaleFit > 0.5f, "%s fit %f", c.id, f.scaleFit);
        if (std::string(c.id) == "lydian") lydianBright = f.scaleBrightness;
        if (std::string(c.id) == "phrygian") phrygianBright = f.scaleBrightness;
    }
    CHECK(lydianBright > 0.8f && phrygianBright < 0.3f, "lydian %f, phrygian %f", lydianBright, phrygianBright);
}

// Kick-like click tracks with an accented first beat: tempo within 2%, locked, every beat trigger within
// 50 ms of a click from 10 s on, and every downbeat on an accented click.
void testClickTracks() {
    for (double bpm : {90.0, 128.0, 174.0}) {
        const double period = 60.0 / bpm;
        SoundEngine e(44100);
        int beats = 0;
        int aligned = 0;
        int downs = 0;
        int downAligned = 0;
        std::string trail;  // every downbeat and musical event, printed only on a failure
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
            // The trail covers the whole run (beat count in brackets), so a failure shows where the bar
            // stood before the checked stretch and what moved it.
            if (last.downbeat > 0.0f || last.harmonicChange > 0.0f || last.drop > 0.0f || last.sectionChange > 0.0f) {
                const char* what = last.downbeat > 0.0f ? "down" : last.drop > 0.0f ? "drop" : last.sectionChange > 0.0f ? "section" : "harm";
                char at[64];
                std::snprintf(at, sizeof at, " %s %.3f[%.0f] (%+.3f)", what, t, last.beatCount, std::remainder(t, 4.0 * period));
                trail += at;
            }
            if (t <= 10.0) continue;
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
        CHECK(std::fabs(last.bpm / bpm - 1.0) < 0.02, "%.0f BPM read %.2f", bpm, last.bpm);
        CHECK(last.beatConf > 0.35f, "%.0f BPM confidence %f", bpm, last.beatConf);
        CHECK(beats >= expected - 1 && aligned == beats, "%.0f BPM: %d beats (%d expected), %d on a click", bpm, beats, expected, aligned);
        CHECK(downs >= expected / 4 - 1 && downAligned == downs, "%.0f BPM: %d downbeats, %d on beat one;%s", bpm, downs, downAligned,
              trail.c_str());
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

}  // namespace

// A pause is not a new track (#368): five seconds with nothing written (the player paused) must not
// re-warm the engine, so on resume the tempo it locked is still there. Only written silence re-warms.
void testPauseKeepsTempo() {
    const double bpm = 128.0;
    const double period = 60.0 / bpm;
    SoundEngine e(44100);
    double offset = 0.0;
    const auto click = [&](double t) {
        t += offset;
        const double u = t - std::floor(t / period) * period;
        return static_cast<float>(0.8 * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) + 0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u));
    };
    GeodeSoundFrame f = run(e, click, 15.0);
    offset = 15.0;
    CHECK(std::fabs(f.bpm / bpm - 1.0) < 0.02, "before the pause read %.2f", f.bpm);
    for (int i = 0; i < 5 * 60; i++) e.step(1.0f / 60.0f, f);  // paused: frames go on, no audio
    f = run(e, click, 0.5);
    CHECK(std::fabs(f.bpm / bpm - 1.0) < 0.02, "half a second after a 5 s pause read %.2f BPM (re-warmed?)", f.bpm);
}

int main() {
    testCatalogue();
    testScales();
    testClickTracks();
    testPauseKeepsTempo();
    testChordProgression();
    testBuildAndDrop();
    testSectionsReturn();
    testDeterministic();
    if (failures == 0) std::printf("musical_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
