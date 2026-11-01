// Host test for the musical layer (core/sound: Rhythm, Harmony, Form) and the musical intelligence
// database's scale catalogue (core/sound/ScaleTable.inc), built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Icore core/tests/musical_test.cpp core/sound/*.cpp -o musical_test && ./musical_test
// Synthetic signals only, so every number here is reproducible.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

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

int main() {
    testCatalogue();
    testScales();
    testDeterministic();
    if (failures == 0) std::printf("musical_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
