// Host test for the sound engine's step (core/sound/AnalysisSettings): the analysis hop is a sample count that
// follows from the FFT size and overlap, never a time. Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/sound_hop_test.cpp core/sound/*.cpp -o sound_hop_test
//
// One: the hop is the same number of samples at 44.1, 48 and 96 kHz (analysed at 48), and its duration is that
// count over the analysis rate. Two: the same synthetic song at two settings (2048/4 and 2048/8, a 512- and a
// 256-sample hop) hears the same music in time: the beat period in seconds, every transient at the same sample
// within one hop of the longer step, and the silence at the same sample within two of them.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include "sound/AnalysisSettings.hpp"
#include "sound/SoundEngine.hpp"

using geode::sound::AnalysisSettings;
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

// The hop needs no rate to be known: it is fixed before any audio arrives.
static_assert(AnalysisSettings{}.hopSamples() == 512);
static_assert(AnalysisSettings{2048, 8}.hopSamples() == 256);

void testSameCountAtEveryRate() {
    for (const AnalysisSettings& s : {AnalysisSettings{}, AnalysisSettings{2048, 8}}) {
        for (int rate : {44100, 48000, 96000}) {
            const SoundEngine e(rate, s);
            const double seconds = static_cast<double>(s.hopSamples()) / SoundEngine::analysisRateFor(rate);
            CHECK(e.hopSamples() == s.hopSamples(), "%d/%d at %d Hz: a hop of %d samples, not %d", s.fftSize, s.overlap, rate,
                  e.hopSamples(), s.hopSamples());
            CHECK(std::fabs(e.hopSeconds() - seconds) < 1e-7, "%d Hz: the hop lasts %f s, not %f", rate, e.hopSeconds(), seconds);
            CHECK(e.historyHops() * e.hopSeconds() >= SoundEngine::kHistorySeconds, "%d Hz: %d kept hops are under %.2f s", rate,
                  e.historyHops(), SoundEngine::kHistorySeconds);
        }
    }
    const AnalysisSettings odd = AnalysisSettings{3000, 0}.checked();
    CHECK(odd.fftSize == 4096 && odd.overlap == 1, "3000/0 runs as %d/%d", odd.fftSize, odd.overlap);
}

// Clicks with an accented first beat to kStop, then silence. 97 BPM: any tempo the range holds would do.
constexpr int kRate = 48000;
constexpr double kBpm = 97.0;
constexpr double kStop = 16.0;
constexpr double kSeconds = 19.0;

float song(double t) {
    if (t >= kStop) return 0.0f;
    const double period = 60.0 / kBpm;
    const int k = static_cast<int>(t / period);
    const double u = t - k * period;
    const double amp = k % 4 == 0 ? 0.8 : 0.35;
    return static_cast<float>(amp * std::exp(-u * 25.0) * std::sin(kTau * 60.0 * u) +
                              0.3 * std::exp(-u * 200.0) * std::sin(kTau * 3000.0 * u));
}

struct Heard {
    std::vector<int64_t> transients;  // the sample of each hop that carried one, from 2 s on
    int64_t silence = -1;             // the sample of the hop that fired silence
    float bpm = 0.0f;                 // the last hop's before kStop
};

// Written a 60 fps frame at a time; every hop read back by its own sample.
Heard hear(const AnalysisSettings& settings) {
    SoundEngine e(kRate, settings);
    Heard out;
    std::vector<float> buf;
    int64_t done = 0;
    int64_t seen = 0;
    const int64_t total = static_cast<int64_t>(kSeconds * kRate);
    for (int i = 1; done < total; i++) {
        const int64_t end = std::min<int64_t>(total, std::llround(i * kRate / 60.0));
        buf.resize(static_cast<size_t>(end - done));
        for (int64_t s = done; s < end; s++) buf[static_cast<size_t>(s - done)] = song(static_cast<double>(s) / kRate);
        e.write(buf.data(), static_cast<int>(end - done), 1);
        done = end;
        for (int64_t age = e.hopsRun() - seen - 1; age >= 0; --age) {
            const SoundEngine::Hop& h = *e.keptHop(static_cast<int>(age));
            if (h.frame.transient > 0.0f && h.sample >= 2 * kRate) out.transients.push_back(h.sample);
            if (h.frame.silence > 0.0f && out.silence < 0) out.silence = h.sample;
            if (h.sample < static_cast<int64_t>(kStop * kRate)) out.bpm = h.frame.bpm;
        }
        seen = e.hopsRun();
    }
    return out;
}

void testTwoSettingsAgree() {
    const AnalysisSettings coarse{};
    const AnalysisSettings fine{2048, 8};
    const Heard a = hear(coarse);
    const Heard b = hear(fine);
    const int64_t hop = coarse.hopSamples();  // the longer step
    std::printf("  2048/4: %.2f BPM, %zu transients, silence at %lld; 2048/8: %.2f BPM, %zu transients, silence at %lld\n", a.bpm,
                a.transients.size(), static_cast<long long>(a.silence), b.bpm, b.transients.size(), static_cast<long long>(b.silence));

    // The beat period in seconds: each within 2 % of the clicks', and within 1 % of each other.
    const double truth = 60.0 / kBpm;
    CHECK(a.bpm > 0.0f && b.bpm > 0.0f, "a tempo at both: %.2f and %.2f", a.bpm, b.bpm);
    if (a.bpm > 0.0f && b.bpm > 0.0f) {
        const double pa = 60.0 / a.bpm;
        const double pb = 60.0 / b.bpm;
        CHECK(std::fabs(pa / truth - 1.0) < 0.02 && std::fabs(pb / truth - 1.0) < 0.02, "periods %.4f and %.4f s, the clicks' %.4f", pa,
              pb, truth);
        CHECK(std::fabs(pb / pa - 1.0) < 0.01, "the period moved with the step: %.4f s against %.4f", pb, pa);
    }

    // Every click from 2 s to kStop is one transient, at the same sample within one hop of the longer step.
    const int clicks = static_cast<int>((kStop - 2.0) / truth);
    CHECK(static_cast<int>(a.transients.size()) >= clicks - 1, "%zu transients for %d clicks", a.transients.size(), clicks);
    CHECK(a.transients.size() == b.transients.size(), "%zu transients at 2048/4, %zu at 2048/8", a.transients.size(),
          b.transients.size());
    for (size_t i = 0; i < std::min(a.transients.size(), b.transients.size()); i++) {
        CHECK(std::llabs(a.transients[i] - b.transients[i]) <= hop, "transient %zu at sample %lld and %lld", i,
              static_cast<long long>(a.transients[i]), static_cast<long long>(b.transients[i]));
    }

    // Silence: after the clicks stop, at the same sample within two hops of the longer step (its 0.5 s hold is
    // counted in whole hops, whose float sum can land one late).
    const int64_t stop = static_cast<int64_t>(kStop * kRate);
    CHECK(a.silence > stop && b.silence > stop, "silence at %lld and %lld, the clicks stop at %lld", static_cast<long long>(a.silence),
          static_cast<long long>(b.silence), static_cast<long long>(stop));
    CHECK(std::llabs(a.silence - b.silence) <= 2 * hop, "silence at sample %lld and %lld", static_cast<long long>(a.silence),
          static_cast<long long>(b.silence));
}

}  // namespace

int main() {
    std::printf("sound hop: a sample count from the FFT settings\n");
    testSameCountAtEveryRate();
    testTwoSettingsAgree();
    if (failures == 0) std::printf("sound_hop_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
