// Host test for the meter Rhythm reads (core/sound/Rhythm): 3 or 4 beats a bar, from the accents alone.
//   g++ -std=c++20 -O1 -Icore core/tests/meter_test.cpp core/sound/*.cpp -o meter_test && ./meter_test
// Synthetic click tracks at tempos that are not round (93 and 141 BPM), so every number is reproducible.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <vector>

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
constexpr double kStart = 0.137;

// A click: a 60 Hz thump and a 3 kHz tick, the thump `amp` loud.
double click(double u, double amp) {
    return amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) + 0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u);
}

// Beat k's thump: the bar's first beat loud in `meter` beats, or (backbeat) a kick on 1 and 3 and a snare-like
// noise burst on 2 and 4, whose pattern repeats every 2 beats and so says 4, not 3.
struct Track {
    double bpm;
    int meter;      // 0: every click alike
    bool backbeat;  // 4/4 kick and snare
};

float sample(const Track& tr, double t) {
    const double period = 60.0 / tr.bpm;
    if (t < kStart) return 0.0f;
    const int k = static_cast<int>((t - kStart) / period);
    const double u = t - kStart - k * period;
    if (tr.backbeat) {
        if (k % 2 == 0) return static_cast<float>(click(u, 0.8));
        // A deterministic noise burst for the snare, over the same tick.
        const uint32_t n = static_cast<uint32_t>(t * 44100.0) * 2654435761u;
        const double white = static_cast<double>(n >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
        return static_cast<float>(0.4 * std::exp(-u * 30.0) * white + click(u, 0.2));
    }
    const bool accent = tr.meter > 0 && k % tr.meter == 0;
    return static_cast<float>(click(u, accent ? 0.8 : 0.35));
}

struct Heard {
    float bpm = 0.0f;
    int downs = 0;          // downbeats from `from` on
    int onAccent = 0;       // ... on a true bar start
    int otherMeter = 0;     // beats from `from` on whose bar is not `expect` beats long
    double settledAt = -1;  // when the bar last changed length, s
};

// The bar's length is read off the bar phase: from one beat to the next it moves 1/3 or 1/4 of a bar (a
// moved downbeat jumps it by more, and says nothing about the length).
Heard listen(const Track& tr, double seconds, double from, int expect) {
    SoundEngine e(44100);
    const int rate = e.sampleRateHz();
    std::vector<float> buf;
    GeodeSoundFrame f{};
    int64_t done = 0;
    Heard h;
    float lastPhase = -1.0f;
    int meter = 0;
    const double period = 60.0 / tr.bpm;
    for (int i = 1; i <= static_cast<int>(seconds * 60.0); i++) {
        const int64_t end = std::llround(i * rate / 60.0);
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) buf[2 * (s - done)] = buf[2 * (s - done) + 1] = sample(tr, static_cast<double>(s) / rate);
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(1.0f / 60.0f, f);
        const double t = i / 60.0;
        if (f.beat > 0.0f) {
            const double step = lastPhase >= 0.0f ? f.barPhase - lastPhase - std::floor(f.barPhase - lastPhase) : 0.0;
            lastPhase = f.barPhase;
            const int read = step > 0.2 && step < 0.4 ? static_cast<int>(std::lround(1.0 / step)) : 0;
            if (read != 0 && read != meter) {
                meter = read;
                h.settledAt = t;
            }
            if (t >= from && meter != 0 && meter != expect) h.otherMeter++;
        }
        if (f.downbeat <= 0.0f || t < from) continue;
        h.downs++;
        if (std::fabs(std::remainder(t - kStart, expect * period)) < 0.05) h.onAccent++;
    }
    h.bpm = f.bpm;
    return h;
}

// Each meter at each tempo reads as itself: the tempo, a bar of 3 or 4 beats on every beat from 25 s on,
// every downbeat on the accented click, and the bar's length settled by then and never changed after.
void testMeters() {
    for (double bpm : {93.0, 141.0}) {
        for (int meter : {3, 4}) {
            const Heard h = listen({bpm, meter, false}, 90.0, 25.0, meter);
            std::printf("  %d/4 at %.0f BPM: read %.2f BPM, bar settled at %.1f s, %d beats from 25 s in another meter, %d downbeats, %d on beat one\n",
                        meter, bpm, h.bpm, h.settledAt, h.otherMeter, h.downs, h.onAccent);
            CHECK(std::fabs(h.bpm / bpm - 1.0) < 0.02, "%d/4 at %.0f BPM read %.2f", meter, bpm, h.bpm);
            CHECK(h.settledAt > 0.0 && h.settledAt < 25.0 && h.otherMeter == 0, "%d/4 at %.0f BPM: settled at %.1f s, %d beats in another meter", meter,
                  bpm, h.settledAt, h.otherMeter);
            const int expected = static_cast<int>(65.0 / (meter * 60.0 / bpm));
            CHECK(h.downs >= expected - 1 && h.onAccent == h.downs, "%d/4 at %.0f BPM: %d downbeats (%d expected), %d on beat one", meter, bpm, h.downs,
                  expected, h.onAccent);
        }
    }
}

// What does not show 3 never becomes 3: a kick-snare backbeat (a 2-beat pattern) stays in 4 for two
// minutes, and so does an even pulse with no accent, which makes no claim and keeps the bar it has.
void testNoFlip() {
    for (double bpm : {93.0, 141.0}) {
        for (bool backbeat : {true, false}) {
            const Heard h = listen({bpm, 0, backbeat}, 120.0, 0.0, 4);
            std::printf("  %s at %.0f BPM: read %.2f BPM, %d beats in another meter\n", backbeat ? "backbeat" : "even clicks", bpm, h.bpm, h.otherMeter);
            CHECK(std::fabs(h.bpm / bpm - 1.0) < 0.02, "%s at %.0f BPM read %.2f", backbeat ? "backbeat" : "even clicks", bpm, h.bpm);
            CHECK(h.otherMeter == 0, "%s at %.0f BPM: %d beats not in 4", backbeat ? "backbeat" : "even clicks", bpm, h.otherMeter);
        }
    }
}

}  // namespace

int main() {
    testMeters();
    testNoFlip();
    if (failures == 0) std::printf("meter_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
