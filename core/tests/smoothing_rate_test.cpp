// Host checks that the envelope and the beat-timed wave smooth in seconds: the same input gives the same level at the
// same time at 30, 60 and 120 fps. A stage that ends mid-frame hands the rest of the frame to the next one, the
// tracking sustain and the wave's phase jump ease by exp(-dt / tau).
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/modulation/Adsr.hpp"
#include "viz/modulation/Lfo.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr std::array<int, 3> kRates = {30, 60, 120};
constexpr std::array<int, 6> kReadTenths = {1, 2, 3, 5, 10, 11};

// A hit at 0 with the gate held for the first second, read at every tenth of a second.
std::array<float, kReadTenths.size()> envelope(int fps, bool tracking) {
    AdsrShape shape;
    shape.attack = 0.07f;
    shape.decay = 0.2f;
    shape.sustain = 0.5f;
    shape.release = 0.3f;
    shape.sustainTrack = tracking;
    shape.gateThreshold = 0.5f;
    AdsrVoice voice;
    std::array<float, kReadTenths.size()> read{};
    const float dt = 1.0f / static_cast<float>(fps);
    for (int i = 0; i < fps * 11 / 10; ++i) {
        const bool held = i + 1 <= fps;
        const float level = voice.step(shape, dt, i == 0 ? 1.0f : 0.0f, held ? 0.5f : 0.0f, held, held);
        for (size_t r = 0; r < read.size(); ++r) {
            if ((i + 1) * 10 == kReadTenths[r] * fps) read[r] = level;
        }
    }
    return read;
}

void anEnvelopeRunsInSecondsAtAnyFrameRate() {
    for (const bool tracking : {false, true}) {
        const auto base = envelope(30, tracking);
        for (const int fps : kRates) {
            const auto got = envelope(fps, tracking);
            for (size_t r = 0; r < got.size(); ++r) {
                check(std::fabs(got[r] - base[r]) < 1e-3f, std::string(tracking ? "tracking " : "fixed ") + "sustain at " + std::to_string(fps) +
                                                              " fps agrees with 30 fps at " + std::to_string(kReadTenths[r]) + "/10 s (" +
                                                              std::to_string(got[r]) + " vs " + std::to_string(base[r]) + ")");
            }
        }
    }
    // 0.03 s into a 0.2 s decay from 1 to 0.5, after a 0.07 s attack that ended inside a frame at 30 fps.
    const auto fixed = envelope(30, false);
    check(std::fabs(fixed[0] - 0.925f) < 1e-3f, "the attack's leftover runs the decay (" + std::to_string(fixed[0]) + ", want 0.925)");
    // Released at 1 s from 0.5, at 1 / 0.3 a second: 0.1 s later it is 0.5 - 1/3.
    check(std::fabs(fixed[5] - (0.5f - 1.0f / 3.0f)) < 1e-3f, "the release starts the frame the gate drops (" + std::to_string(fixed[5]) + ")");
}

// The wave's phase, free-running a tenth of a second and then locked to a beat clock held at 0.6, read at whole tenths.
std::vector<float> lockedWave(int fps) {
    LfoEngine engine;
    LfoConfig& c = engine.configs[0];
    c.enabled = true;
    c.target = LfoTarget::Zoom;
    c.wave = LfoWave::Saw;
    c.polarity = ModPolarity::Bipolar;
    c.depth = 1.0f;
    c.beats = 1.0f;
    c.rateSeconds = 1.0f;
    const float dt = 1.0f / static_cast<float>(fps);
    std::vector<float> read;
    for (int i = 0; i < fps * 12 / 10; ++i) {
        GeodeSoundFrame sound{};
        const bool locked = i + 1 > fps / 10;
        if (locked) sound.bpm = 93.0f;
        sound.beatConf = locked ? 1.0f : 0.0f;
        sound.beatCount = 0.0f;
        sound.beatPhase = 0.6f;
        const float v = engine.tick(dt, sound)[0];
        if ((i + 1) * 10 % fps == 0 && (i + 1) * 10 / fps >= 5) read.push_back(v);
    }
    return read;
}

void aBeatLockEasesInSecondsAtAnyFrameRate() {
    const std::vector<float> base = lockedWave(30);
    check(base.size() == 8, "the wave is read eight times (" + std::to_string(base.size()) + ")");
    for (const int fps : kRates) {
        const std::vector<float> got = lockedWave(fps);
        check(got.size() == base.size(), std::to_string(fps) + " fps reads as often");
        for (size_t i = 0; i < got.size() && i < base.size(); ++i) {
            check(std::fabs(got[i] - base[i]) < 1e-3f, "the phase jump at " + std::to_string(fps) + " fps agrees with 30 fps, read " + std::to_string(i) +
                                                          " (" + std::to_string(got[i]) + " vs " + std::to_string(base[i]) + ")");
        }
    }
}

}  // namespace

int main() {
    anEnvelopeRunsInSecondsAtAnyFrameRate();
    aBeatLockEasesInSecondsAtAnyFrameRate();
    if (failures) {
        std::printf("smoothing_rate_test: %d failed\n", failures);
        return 1;
    }
    std::printf("smoothing_rate_test: all passed\n");
    return 0;
}
