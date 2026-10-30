// Host-side checks for the structure events (arrival, drop) on synthetic PCM: they are properties of the
// music, so the same music at a loud and a quiet master level has to produce them alike.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml); no device.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "analysis/ReactiveAnalyzer.hpp"

using geode::analysis::ReactiveAnalyzer;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRate = 48000;
constexpr int kFft = 2048;
constexpr int kHop = kFft / 4;
constexpr float kHopRate = static_cast<float>(kRate) / kHop;

struct Noise {
    uint32_t state = 4242u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    }
};

// Mono: a 15 ms noise burst every beat at 120 BPM over a steady noise bed, all scaled by `level`.
void appendMusic(std::vector<float>& pcm, float seconds, float level, Noise& noise) {
    const long frames = static_cast<long>(seconds * kRate);
    const long period = kRate / 2;
    const long burst = kRate * 15 / 1000;
    for (long i = 0; i < frames; i++) {
        const long sinceBeat = i % period;
        float x = 0.4f * level * noise.next();
        if (sinceBeat < burst) x += level * noise.next() * std::exp(-static_cast<float>(sinceBeat) / (0.004f * kRate));
        pcm.push_back(x);
    }
}

// Runs the analyser over every hop and reports whether `event` fired at or after `fromSeconds`.
template <typename Event>
bool firesAfter(const std::vector<float>& pcm, float fromSeconds, Event event) {
    ReactiveAnalyzer analyzer(64, kFft, kRate, kHopRate);
    bool fired = false;
    for (size_t at = 0, hop = 0; at + kFft <= pcm.size(); at += kHop, hop++) {
        analyzer.analyze(pcm.data() + at, 1.0f / kHopRate);
        if (static_cast<float>(hop) / kHopRate >= fromSeconds && event(analyzer)) fired = true;
    }
    return fired;
}

// 12 s of music, 3 s of digital silence, the music again. An arrival used to need absolute RMS above 0.4
// (and the analyser skipped the structure tracker during silence), so it fired at neither level.
void arrivalAtAnyMasterLevel() {
    for (const float level : {0.5f, 0.02f}) {
        std::vector<float> pcm;
        Noise noise;
        appendMusic(pcm, 12.0f, level, noise);
        pcm.insert(pcm.end(), static_cast<size_t>(3 * kRate), 0.0f);
        appendMusic(pcm, 6.0f, level, noise);
        const bool arrived = firesAfter(pcm, 15.0f, [](const ReactiveAnalyzer& a) { return a.arrival(); });
        if (!arrived) std::printf("  level %.2f: no arrival after the break\n", level);
        check(arrived, "arrival fires after a silent break at any master level");
    }
}

// Steady music has no arrival and no drop, loud or quiet.
void steadyMusicHasNoEvents() {
    for (const float level : {0.5f, 0.02f}) {
        std::vector<float> pcm;
        Noise noise;
        appendMusic(pcm, 20.0f, level, noise);
        const bool fired = firesAfter(pcm, 0.0f, [](const ReactiveAnalyzer& a) { return a.arrival() || a.drop(); });
        if (fired) std::printf("  level %.2f: an event fired in steady music\n", level);
        check(!fired, "no structure events in steady music");
    }
}

}  // namespace

int main() {
    arrivalAtAnyMasterLevel();
    steadyMusicHasNoEvents();
    if (failures == 0) std::printf("analysis_structure_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
