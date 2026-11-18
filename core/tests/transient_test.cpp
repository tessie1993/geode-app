// Host test for transient detection (core/sound/Transient, and the frame's transient, transientEnv and
// transientSharpness from SoundEngine), built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/transient_test.cpp core/sound/*.cpp -o transient_test
//
// The owner: "Write new transient detection." A transient is the sharp start of any sound, not only a drum's,
// so these feed the engine synthetic sound and check what fires: every click and every plucked note, within a
// few tens of milliseconds; never a slow swell, a crescendo, a held tone, silence, or clicks too quiet to hear;
// and the same attacks at every sample rate and frame rate. The stimuli hold no tempo the detector could lean on
// (it has none): plucks come every three beats at 77 BPM and clicks at 141, and the checks only ask where each
// attack is.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <vector>

#include "sound/OnsetFlux.hpp"
#include "sound/SoundEngine.hpp"

using geode::sound::SoundEngine;

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

// A mono signal by time in seconds.
using Signal = std::function<double(double)>;

struct Fire {
    double t;  // the end of the frame it showed in, in seconds
    float strength;
    float sharpness;
};

struct Run {
    std::vector<Fire> fires;
    float maxEnv = 0.0f;
    float lastSharpness = 0.0f;
};

// Fed the way a live surface feeds the engine: each frame's samples, then step(1/fps).
Run play(const Signal& signal, double seconds, int rate, double fps) {
    SoundEngine engine(rate);
    const int64_t total = static_cast<int64_t>(seconds * rate);
    std::vector<float> block;
    GeodeSoundFrame f{};
    Run run;
    int64_t done = 0;
    for (int i = 1; done < total; i++) {
        const int64_t end = std::min(total, static_cast<int64_t>(std::llround(i * rate / fps)));
        block.resize(static_cast<size_t>(end - done));
        for (int64_t s = done; s < end; s++) block[static_cast<size_t>(s - done)] = static_cast<float>(signal(static_cast<double>(s) / rate));
        engine.write(block.data(), static_cast<int>(end - done), 1);
        done = end;
        engine.step(static_cast<float>(1.0 / fps), f);
        if (f.transient > 0.0f) run.fires.push_back({static_cast<double>(done) / rate, f.transient, f.transientSharpness});
        run.maxEnv = std::max(run.maxEnv, f.transientEnv);
        run.lastSharpness = f.transientSharpness;
    }
    return run;
}

// A tone with a few harmonics, peak about `amp`.
double tone(double hz, double t, double amp) {
    return amp * (std::sin(kTau * hz * t) + 0.5 * std::sin(kTau * 2.0 * hz * t) + 0.25 * std::sin(kTau * 3.0 * hz * t)) / 1.75;
}

// 0 → 1 over `seconds`, a raised cosine.
double rise(double t, double seconds) { return t <= 0.0 ? 0.0 : t >= seconds ? 1.0 : 0.5 - 0.5 * std::cos(std::numbers::pi * t / seconds); }

// A quiet bed in all three bands (-34 dBFS or so), faded in over 0.6 s: the room every other sound starts in.
double bed(double t) {
    return rise(t, 0.6) * (0.02 * std::sin(kTau * 130.8 * t) + 0.02 * std::sin(kTau * 523.3 * t) + 0.01 * std::sin(kTau * 5200.0 * t));
}

// Clicks at 141 BPM from 0.5 s: a 0.3 ms decaying pulse each, on silence, `amp` at the peak.
constexpr double kClickPeriod = 60.0 / 141.0;
constexpr int kClicks = 12;
double clicks(double t, double amp) {
    const double since = t - 0.5;
    if (since < 0.0) return 0.0;
    const int k = static_cast<int>(since / kClickPeriod);
    if (k >= kClicks) return 0.0;
    return amp * std::exp(-(since - k * kClickPeriod) / 0.0003);
}

// Plucks and swells over the bed. A cycle is three beats at 77 BPM: a plucked note (an instant attack, 0.12 s
// decay) on its first beat, and on its second a swell of another note (0.45 s up, 0.45 s down, raised cosines)
// that is gone before the next pluck.
constexpr double kBeat = 60.0 / 77.0;
constexpr double kCycle = 3.0 * kBeat;
constexpr double kFirstPluck = 1.0;
constexpr int kPlucks = 5;
double plucked(double t) {
    double v = bed(t);
    const double since = t - kFirstPluck;
    if (since < 0.0) return v;
    const int k = static_cast<int>(since / kCycle);
    if (k >= kPlucks) return v;
    const double inCycle = since - k * kCycle;
    v += tone(330.0, inCycle, 0.35) * std::exp(-inCycle / 0.12);
    const double swell = inCycle - kBeat;
    if (swell > 0.0 && swell < 0.9) v += tone(440.0, swell, 0.3) * (swell < 0.45 ? rise(swell, 0.45) : 1.0 - rise(swell - 0.45, 0.45));
    return v;
}
double pluckAt(int k) { return kFirstPluck + k * kCycle; }

// Where a transient shows: the attack is measured for 5 ms, reported on the hop that ends after it (10 ms), and
// shown on the frame after that hop.
constexpr double kLatency = 0.005 + 0.010 + 1.0 / 30.0 + 0.002;

void testClicksFireEachTime() {
    const Run run = play([](double t) { return clicks(t, 0.6); }, 0.5 + kClicks * kClickPeriod + 0.3, 48000, 60.0);
    CHECK(static_cast<int>(run.fires.size()) == kClicks, "%zu transients for %d clicks", run.fires.size(), kClicks);
    for (int k = 0; k < kClicks; k++) {
        const double at = 0.5 + k * kClickPeriod;
        const bool seen = std::any_of(run.fires.begin(), run.fires.end(), [&](const Fire& f) { return f.t >= at && f.t <= at + kLatency; });
        CHECK(seen, "click %d at %.3f s heard within %.0f ms", k, at, 1000.0 * kLatency);
    }
    for (const Fire& f : run.fires) CHECK(f.sharpness >= 0.6f, "a click is a step: sharpness %.2f", static_cast<double>(f.sharpness));
    CHECK(run.maxEnv > 0.2f, "the envelope rises on a click: %.2f", static_cast<double>(run.maxEnv));
}

void testSilenceNeverFires() {
    const Run run = play([](double) { return 0.0; }, 3.0, 48000, 60.0);
    CHECK(run.fires.empty() && run.maxEnv == 0.0f && run.lastSharpness == 0.0f, "silence: %zu transients", run.fires.size());
}

// The same clicks 60 dB down (peaks near -64 dBFS): under the hit floor, as quiet as no hit, so nothing fires.
void testNoTransientsNearSilence() {
    const Run run = play([](double t) { return clicks(t, 0.0006); }, 0.5 + kClicks * kClickPeriod + 0.3, 48000, 60.0);
    CHECK(run.fires.empty() && run.maxEnv == 0.0f, "clicks near silence: %zu transients", run.fires.size());
}

// A loud held tone (a low note, whose waveform ripples the fast followers): its fade-in may count as one start,
// its body never.
void testHeldToneIsQuiet() {
    const Run run = play([](double t) { return rise(t, 0.5) * tone(110.0, t, 0.5); }, 5.0, 48000, 60.0);
    const auto late = std::count_if(run.fires.begin(), run.fires.end(), [](const Fire& f) { return f.t > 1.0; });
    CHECK(late == 0, "%d transients in a held tone", static_cast<int>(late));
}

// A crescendo from -50 to -6 dBFS over four seconds: louder all the time, never sharply.
void testCrescendoIsQuiet() {
    const Run run = play(
        [](double t) {
            const double db = -50.0 + 44.0 * std::clamp((t - 0.5) / 4.0, 0.0, 1.0);
            return rise(t, 0.5) * tone(220.0, t, std::pow(10.0, db / 20.0));
        },
        5.0, 48000, 60.0);
    const auto late = std::count_if(run.fires.begin(), run.fires.end(), [](const Fire& f) { return f.t > 0.6; });
    CHECK(late == 0, "%d transients in a crescendo", static_cast<int>(late));
}

// Every pluck fires, once, soon after it; the swells between them and the bed under them never do.
void testPlucksFireSwellsDoNot() {
    const Run run = play(plucked, pluckAt(kPlucks) + 0.2, 48000, 60.0);
    for (int k = 0; k < kPlucks; k++) {
        const double at = pluckAt(k);
        const auto n = std::count_if(run.fires.begin(), run.fires.end(), [&](const Fire& f) { return f.t >= at && f.t <= at + kLatency; });
        CHECK(n == 1, "pluck %d at %.3f s: %d transients", k, at, static_cast<int>(n));
    }
    for (const Fire& f : run.fires) {
        if (f.t < 0.9) continue;  // the bed's own fade-in
        bool nearPluck = false;
        for (int k = 0; k < kPlucks; k++) nearPluck = nearPluck || (f.t >= pluckAt(k) && f.t <= pluckAt(k) + kLatency);
        CHECK(nearPluck, "a transient at %.3f s, %.3f s into its cycle: a swell or the bed fired", f.t,
              std::fmod(f.t - kFirstPluck, kCycle));
        if (nearPluck) CHECK(f.sharpness >= 0.3f, "a pluck is sharp: %.2f", static_cast<double>(f.sharpness));
    }
}

// The attacks are the samples', so 44.1, 48 and 96 kHz (analysed at 48) hear the same ones, at the same times
// within a frame and a hop; and a frame rate only changes which frame shows each.
void testRateIndependent() {
    const double seconds = pluckAt(kPlucks) + 0.2;
    const Run base = play(plucked, seconds, 48000, 60.0);
    const auto same = [&](const Run& other, const char* what) {
        CHECK(other.fires.size() == base.fires.size(), "%s: %zu transients, 48 kHz at 60 fps %zu", what, other.fires.size(),
              base.fires.size());
        const size_t n = std::min(other.fires.size(), base.fires.size());
        for (size_t i = 0; i < n; i++) {
            CHECK(std::fabs(other.fires[i].t - base.fires[i].t) <= 1.0 / 30.0 + 0.011, "%s: transient %zu at %.3f s, not %.3f", what, i,
                  other.fires[i].t, base.fires[i].t);
            CHECK(std::fabs(other.fires[i].sharpness - base.fires[i].sharpness) <= 0.15f, "%s: sharpness %.2f, not %.2f", what,
                  static_cast<double>(other.fires[i].sharpness), static_cast<double>(base.fires[i].sharpness));
        }
    };
    same(play(plucked, seconds, 44100, 60.0), "44.1 kHz");
    same(play(plucked, seconds, 96000, 60.0), "96 kHz");
    same(play(plucked, seconds, 48000, 30.0), "30 fps");
    same(play(plucked, seconds, 48000, 90.0), "90 fps");
}

// The flux's reference spectrum (sound/OnsetFlux, Böck & Widmer 2013's maximum filter): a partial that slid one
// bin, as a vibrato's wobble does, finds its old self within reach and adds no rise; a new partial where the
// neighbourhood was empty rises in full. Plain bin-by-bin arithmetic on hand-made log spectra.
void testVibratoSlideIsNoRise() {
    namespace of = geode::sound::onsetflux;
    constexpr int kBins = 64;
    int reach[kBins];
    for (int k = 0; k < kBins; k++) reach[k] = of::reachOf(k);
    CHECK(of::reachOf(0) == 1 && of::reachOf(10) == 1 && of::reachOf(40) == 2 && of::reachOf(400) == 24,
          "the reach is a semitone of the bin's own frequency, at least one bin: %d %d %d %d", of::reachOf(0), of::reachOf(10),
          of::reachOf(40), of::reachOf(400));
    float before[kBins] = {};
    float now[kBins] = {};
    before[20] = 3.0f;  // a partial at bin 20
    now[21] = 3.0f;     // ... a hop later, one bin up
    float reference[kBins] = {};
    of::maxFilter(before, reference, 1, kBins, reach);
    float plain = 0.0f;
    float filtered = 0.0f;
    for (int k = 1; k < kBins; k++) {
        plain += std::max(0.0f, now[k] - before[k]);
        filtered += std::max(0.0f, now[k] - reference[k]);
    }
    CHECK(plain > 2.9f, "bin by bin, the slid partial reads as a rise of %.2f", static_cast<double>(plain));
    CHECK(filtered == 0.0f, "against the filtered reference it reads none: %.2f", static_cast<double>(filtered));
    now[50] = 2.0f;  // a new partial, far from the old one
    filtered = 0.0f;
    for (int k = 1; k < kBins; k++) filtered += std::max(0.0f, now[k] - reference[k]);
    CHECK(std::fabs(filtered - 2.0f) < 1e-6f, "a new partial still rises in full: %.2f", static_cast<double>(filtered));
    CHECK(reference[19] == 3.0f && reference[21] == 3.0f && reference[23] == 0.0f && reference[1] == 0.0f,
          "the filter spreads a peak over its reach and no further");
}

}  // namespace

int main() {
    testVibratoSlideIsNoRise();
    testClicksFireEachTime();
    testSilenceNeverFires();
    testNoTransientsNearSilence();
    testHeldToneIsQuiet();
    testCrescendoIsQuiet();
    testPlucksFireSwellsDoNot();
    testRateIndependent();
    if (failures == 0) std::printf("transient_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
