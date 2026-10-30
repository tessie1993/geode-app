// Host-side checks for TempoTracker: synthetic onset envelopes at several hop rates and tempi, and the
// tracker has to report each tempo as itself, with no pull towards a preferred BPM and no octave folding.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml); no device.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "analysis/TempoTracker.hpp"

using geode::analysis::TempoTracker;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The offline (60 Hz) and live (62.5 Hz) hop rates today, and rate / (fft / 4) for the 44.1 and 48 kHz
// families once the hop follows the window.
constexpr float kHopRates[] = {60.0f, 62.5f, 86.1328f, 93.75f};

struct Noise {
    uint32_t state = 777u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    }
};

// An onset envelope: a 1.5-hop-wide bump on every beat (beats land between hops, as real ones do), an
// optional weaker bump on every off-beat eighth, and a little noise.
float envelope(float hop, float hopRateHz, float bpm, float offbeat, Noise& noise) {
    const float beatHops = 60.0f * hopRateHz / bpm;
    const float phase = std::fmod(hop, beatHops);
    const float toBeat = std::min(phase, beatHops - phase);
    const float toOff = std::fabs(phase - beatHops * 0.5f);
    const float bump = std::max(0.0f, 1.0f - toBeat / 1.5f);
    const float off = offbeat * std::max(0.0f, 1.0f - toOff / 1.5f);
    return bump + off + 0.05f * noise.next();
}

float settledBpm(float hopRateHz, float bpm, float offbeat) {
    TempoTracker tracker(hopRateHz);
    Noise noise;
    const int hops = static_cast<int>(30.0f * hopRateHz);
    std::vector<float> late;
    for (int i = 0; i < hops; i++) {
        tracker.step(envelope(static_cast<float>(i), hopRateHz, bpm, offbeat, noise));
        if (i > hops * 2 / 3) late.push_back(tracker.bpm());
    }
    std::nth_element(late.begin(), late.begin() + late.size() / 2, late.end());
    return late[late.size() / 2];
}

void reportsEachTempoAsItself() {
    for (const float hopRate : kHopRates) {
        for (const float bpm : {48.0f, 72.0f, 96.0f, 110.0f, 128.0f, 150.0f, 174.0f, 200.0f}) {
            const float measured = settledBpm(hopRate, bpm, 0.0f);
            if (std::fabs(measured - bpm) > bpm * 0.02f) {
                std::printf("  hop %.2f Hz, %.0f BPM: measured %.2f\n", hopRate, bpm, measured);
                check(false, "BPM follows the onsets with no preferred tempo");
            }
        }
    }
}

// Quieter off-beat eighths (hats) are not the beat: 90 BPM with them stays 90, it does not become 180.
void weakEighthsDoNotDoubleTheTempo() {
    for (const float hopRate : kHopRates) {
        const float measured = settledBpm(hopRate, 90.0f, 0.25f);
        if (std::fabs(measured - 90.0f) > 90.0f * 0.02f) {
            std::printf("  hop %.2f Hz, 90 BPM with off-beats: measured %.2f\n", hopRate, measured);
            check(false, "weak eighths keep the beat");
        }
    }
}

void noOnsetsNoTempo() {
    TempoTracker tracker(93.75f);
    for (int i = 0; i < 1000; i++) tracker.step(0.0f);
    check(tracker.bpm() == 0.0f && tracker.confidence() == 0.0f, "silence reports no tempo");
}

}  // namespace

int main() {
    reportsEachTempoAsItself();
    weakEighthsDoNotDoubleTheTempo();
    noOnsetsNoTempo();
    if (failures == 0) std::printf("analysis_tempo_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
