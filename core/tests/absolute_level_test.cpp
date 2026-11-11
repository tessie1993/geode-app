// Host test for the sound engine's absolute loudness (core/sound/LufsMeter, the drive in SoundEngine), built with
// the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Icore core/tests/absolute_level_test.cpp core/sound/*.cpp -o absolute_level_test
//
// The owner's bug: "when I play an ambient track with very little beat, visuals still behave as if a fat track is
// playing." Every level was relative to the song itself (a band over its own 4 s mean, the level over its own
// peak), so a pad and a mastered kick read the same. These tests pin the other behaviour: how much the music is
// comes from absolute loudness and texture on fixed scales, with no per-song normalising.
//
// Part one is fosfora's LoudnessMeter tests (crates/fosfora-app/src/audio/loudness.rs), ported.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include "sound/DriveGain.hpp"
#include "sound/LufsMeter.hpp"
#include "sound/SoundEngine.hpp"

using geode::sound::LufsMeter;
using geode::sound::SoundEngine;

namespace {

int failures = 0;

#define CHECK(cond, ...)                                           \
    do {                                                           \
        if (!(cond)) {                                             \
            failures++;                                            \
            std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                              \
            std::printf("\n");                                     \
        }                                                          \
    } while (0)

constexpr double kTau = 2.0 * std::numbers::pi;

// ---- The meter -------------------------------------------------------------------------------------------

struct Metered {
    LufsMeter meter;
    int rate;
    int hop;
    int fill = 0;
    int64_t at = 0;
    explicit Metered(int sampleRate) : rate(sampleRate), hop(std::max(1, sampleRate / 100)) { meter.configure(rate, hop); }
    void feed(float l, float r) {
        meter.process(l, r);
        at++;
        if (++fill == hop) {
            fill = 0;
            meter.hop();
        }
    }
    void sine(double hz, float amp, double seconds) {
        const int64_t n = static_cast<int64_t>(seconds * rate);
        for (int64_t i = 0; i < n; i++) {
            const float v = amp * static_cast<float>(std::sin(kTau * hz * static_cast<double>(at) / rate));
            feed(v, v);
        }
    }
    void silence(double seconds) {
        const int64_t n = static_cast<int64_t>(seconds * rate);
        for (int64_t i = 0; i < n; i++) feed(0.0f, 0.0f);
    }
};

void testLouderReadsHigherAndInRange() {
    Metered quiet(48000), loud(48000);
    quiet.sine(1000.0, 0.1f, 4.0);
    loud.sine(1000.0, 0.5f, 4.0);
    CHECK(loud.meter.shortTermLufs() > quiet.meter.shortTermLufs() + 10.0, "%f vs %f", loud.meter.shortTermLufs(),
          quiet.meter.shortTermLufs());
    // BS.1770's calibration: a full-scale 1 kHz sine on both channels is 0 LUFS, and 20 dB down is -20.
    Metered full(48000), down(48000);
    full.sine(1000.0, 1.0f, 4.0);
    down.sine(1000.0, 0.1f, 4.0);
    CHECK(std::fabs(full.meter.shortTermLufs()) < 0.3, "full scale stereo reads %f LUFS", full.meter.shortTermLufs());
    CHECK(std::fabs(down.meter.shortTermLufs() + 20.0) < 0.3, "-20 dBFS reads %f LUFS", down.meter.shortTermLufs());
    CHECK(loud.meter.shortTerm() > 0.0f && loud.meter.shortTerm() < 1.0f && loud.meter.momentary() <= 1.0f, "in range");
    CHECK(std::fabs(down.meter.shortTerm() - 2.0f / 3.0f) < 0.01f, "-20 LUFS is two thirds, %f", down.meter.shortTerm());
}

void testSilenceReadsZeroAndGates() {
    Metered m(48000);
    CHECK(!m.meter.open(), "the gate starts closed");
    m.silence(1.0);
    CHECK(m.meter.momentary() == 0.0f && m.meter.shortTerm() == 0.0f, "silence reads zero");
    CHECK(!m.meter.open(), "silence leaves it closed");
    m.sine(440.0, 0.3f, 0.5);
    CHECK(m.meter.open(), "music opens it");
    m.silence(1.2);
    CHECK(!m.meter.open(), "a second of silence closes it");
    // A dip shorter than the hold keeps it open: 100 ms of silence between notes is not the end of the music.
    Metered n(48000);
    n.sine(440.0, 0.3f, 1.0);
    n.silence(0.1);
    CHECK(n.meter.open(), "a 100 ms gap does not close the gate");
}

void testMomentaryTracksFasterThanShortTermOnARise() {
    Metered m(48000);
    m.sine(440.0, 0.01f, 3.0);
    m.sine(440.0, 0.5f, 0.5);
    CHECK(m.meter.momentaryLufs() > m.meter.shortTermLufs() + 3.0, "M %f, S %f", m.meter.momentaryLufs(),
          m.meter.shortTermLufs());
    CHECK(m.meter.trend() > 0.2f, "the rise reads as a trend, %f", static_cast<double>(m.meter.trend()));
    // Steady, the trend is nothing.
    Metered s(48000);
    s.sine(440.0, 0.2f, 5.0);
    CHECK(s.meter.trend() < 0.05f, "steady trend %f", static_cast<double>(s.meter.trend()));
}

void testOddSampleRate() {
    for (int rate : {22050, 44100, 96000}) {
        Metered m(rate);
        m.sine(1000.0, 0.1f, 4.0);
        CHECK(std::fabs(m.meter.shortTermLufs() + 20.0) < 0.5, "%d Hz reads %f LUFS for -20 dBFS", rate, m.meter.shortTermLufs());
    }
}

// ---- The engine ------------------------------------------------------------------------------------------

using Signal = std::function<void(int64_t sample, float& left, float& right)>;

struct Heard {
    double drive = 0, level = 0, loudnessS = 0, busy = 0, intensity = 0, beatConf = 0;
    int kicks = 0, snares = 0, onsets = 0;
    int n = 0;
};

// Runs the engine like the export path does and averages what it said after `settle` seconds.
Heard hear(const Signal& signal, double seconds, double settle, const GeodeSoundTuning* tuning = nullptr, int rate = 48000) {
    SoundEngine e(rate);
    if (tuning) e.setTuning(*tuning);
    const double fps = 60.0;
    const int frames = static_cast<int>(seconds * fps);
    int64_t done = 0;
    std::vector<float> buf;
    GeodeSoundFrame f{};
    Heard h;
    for (int i = 1; i <= frames; i++) {
        const int64_t end = static_cast<int64_t>(std::llround(i * rate / fps));
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) signal(s, buf[2 * (s - done)], buf[2 * (s - done) + 1]);
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(static_cast<float>(1.0 / fps), f);
        if (i / fps < settle) continue;
        h.drive += f.drive;
        h.level += f.level;
        h.loudnessS += f.loudnessS;
        h.busy += f.busy;
        h.intensity += f.intensity;
        h.beatConf += f.beatConf;
        h.kicks += f.kick > 0 ? 1 : 0;
        h.snares += f.snare > 0 ? 1 : 0;
        h.onsets += f.onset > 0 ? 1 : 0;
        h.n++;
    }
    const double n = std::max(1, h.n);
    h.drive /= n;
    h.level /= n;
    h.loudnessS /= n;
    h.busy /= n;
    h.intensity /= n;
    h.beatConf /= n;
    return h;
}

struct Lcg {
    uint32_t state = 0x1234567u;
    double next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / 8388608.0 - 1.0;
    }
};

double midiHz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// An ambient pad: a triad that swells slowly, no transient anywhere. `rmsDb` is its RMS in dBFS.
Signal pad(double rmsDb, int rate = 48000) {
    const double amp = std::pow(10.0, rmsDb / 20.0) / (std::sqrt(1.5) * 0.9);
    return [=](int64_t s, float& l, float& r) {
        const double t = static_cast<double>(s) / rate;
        const double swell = 0.9 + 0.1 * std::sin(kTau * 0.1 * t);  // 10 s period: the slowest of changes
        double v = 0.0;
        for (int note : {57, 61, 64}) v += std::sin(kTau * midiHz(note) * t);
        l = r = static_cast<float>(amp * swell * v);
    };
}

// A four-on-the-floor loop at `step` seconds a kick, a bass under it and hats, squashed like a master. `gain`
// scales the lot before the squash.
Signal fat(double stepSeconds, double gain, int rate = 48000) {
    auto noise = std::make_shared<Lcg>();
    auto prev = std::make_shared<double>(0.0);
    return [=](int64_t s, float& l, float& r) {
        const double t = static_cast<double>(s) / rate;
        const double since = std::fmod(t, stepSeconds);
        const double phase = kTau * (45.0 * since + 75.0 * 0.03 * (1.0 - std::exp(-since / 0.03)));
        const double kick = 0.8 * std::sin(phase) * std::exp(-since / 0.15);
        const double n = noise->next();
        const double hatNoise = n - *prev;
        *prev = n;
        const double hat = 0.3 * hatNoise * std::exp(-std::fmod(t, stepSeconds / 2) / 0.02);
        const double hz = midiHz(36);
        const double bass = 0.3 * (std::sin(kTau * hz * t) + 0.5 * std::sin(kTau * 2 * hz * t)) * std::exp(-since / 0.25);
        const double v = std::tanh(gain * (kick + hat + bass));
        l = r = static_cast<float>(v);
    };
}

Signal scaled(const Signal& s, double gain) {
    return [=](int64_t i, float& l, float& r) {
        s(i, l, r);
        l = static_cast<float>(l * gain);
        r = static_cast<float>(r * gain);
    };
}

// The bug, pinned: a quiet, sparse pad is low drive, low level and fires nothing; a loud dense kick-and-bass is
// high drive and fires kicks. The relative engine called both "typical".
void testPadIsQuietAndFatIsNot() {
    const Heard p = hear(pad(-30.0), 12.0, 6.0);
    const Heard k = hear(fat(0.5, 1.0), 12.0, 6.0);
    std::printf("pad: drive %.3f level %.3f loudnessS %.3f busy %.3f intensity %.3f, kicks %d onsets %d beatConf %.3f\n", p.drive, p.level,
                p.loudnessS, p.busy, p.intensity, p.kicks, p.onsets, p.beatConf);
    std::printf("fat: drive %.3f level %.3f loudnessS %.3f busy %.3f intensity %.3f, kicks %d onsets %d beatConf %.3f\n", k.drive, k.level,
                k.loudnessS, k.busy, k.intensity, k.kicks, k.onsets, k.beatConf);
    CHECK(p.drive < 0.3, "pad drive %f", p.drive);
    CHECK(std::fabs(p.level - 0.5) < 0.05, "a -30 dBFS pad reads level %f, not stretched to its own peak", p.level);
    CHECK(p.kicks <= 1, "a pad fired %d kicks", p.kicks);
    CHECK(k.drive > 0.4, "fat drive %f", k.drive);
    CHECK(k.drive > 2.0 * p.drive, "fat %f vs pad %f", k.drive, p.drive);
    CHECK(k.kicks >= 8, "fat kicks %d of 12", k.kicks);
    CHECK(k.level > p.level + 0.2, "fat level %f vs pad %f", k.level, p.level);
    CHECK(geode::sound::driveGain(static_cast<float>(p.drive)) < 0.7f * geode::sound::driveGain(static_cast<float>(k.drive)),
          "the gain follows");
}

// No normalising: the same pad 20 dB down is 20 dB down, in loudness and in level.
void testQuieterIsQuieter() {
    const Heard a = hear(pad(-30.0), 10.0, 6.0);
    const Heard b = hear(pad(-50.0), 10.0, 6.0);
    CHECK(std::fabs((a.loudnessS - b.loudnessS) - 20.0 / 60.0) < 0.03, "loudness %f -> %f", a.loudnessS, b.loudnessS);
    CHECK(std::fabs((a.level - b.level) - 20.0 / 60.0) < 0.03, "level %f -> %f", a.level, b.level);
    CHECK(b.drive < a.drive, "drive %f -> %f", a.drive, b.drive);
}

// At the same loudness, a sparse passage is less driven than a dense one.
void testDenseOutdrivesSparseAtEqualLoudness() {
    const Heard sparseRaw = hear(pad(-22.0), 10.0, 6.0);
    const Heard denseRaw = hear(fat(0.25, 1.0), 10.0, 6.0);
    // Scale the dense loop to the pad's loudness: loudness is linear in dB, so one correction is exact.
    const double db = (sparseRaw.loudnessS - denseRaw.loudnessS) * 60.0;
    const Heard dense = hear(scaled(fat(0.25, 1.0), std::pow(10.0, db / 20.0)), 10.0, 6.0);
    std::printf("sparse: loudnessS %.3f busy %.3f drive %.3f | dense: loudnessS %.3f busy %.3f drive %.3f\n", sparseRaw.loudnessS,
                sparseRaw.busy, sparseRaw.drive, dense.loudnessS, dense.busy, dense.drive);
    CHECK(std::fabs(dense.loudnessS - sparseRaw.loudnessS) < 0.03, "loudness %f vs %f", dense.loudnessS, sparseRaw.loudnessS);
    CHECK(dense.busy > sparseRaw.busy + 0.2, "busy %f vs %f", dense.busy, sparseRaw.busy);
    CHECK(dense.drive > sparseRaw.drive + 0.1, "drive %f vs %f", dense.drive, sparseRaw.drive);
}

// Hits need a floor: the same kicks 50 dB down sit under the gate and fire nothing.
void testNearSilenceFiresNoHits() {
    const Heard h = hear(scaled(fat(0.5, 1.0), 0.003), 10.0, 2.0);
    CHECK(h.kicks == 0 && h.snares == 0 && h.onsets == 0, "kicks %d snares %d onsets %d under the floor", h.kicks, h.snares, h.onsets);
    CHECK(h.drive < 0.05, "drive %f", h.drive);
}

// The input trim is the analysis's gain: +10 dB reads 10/60 louder, and it never touches the recording (the engine
// only ever receives a copy, it has no output) -- here, that the trim reaches loudness and level alike.
void testInputTrimReachesAnalysis() {
    GeodeSoundTuning t = SoundEngine::defaultTuning();
    t.inputTrimDb = 10.0f;
    const Heard base = hear(pad(-40.0), 10.0, 6.0);
    const Heard trimmed = hear(pad(-40.0), 10.0, 6.0, &t);
    CHECK(std::fabs((trimmed.loudnessS - base.loudnessS) - 10.0 / 60.0) < 0.01, "%f -> %f", base.loudnessS, trimmed.loudnessS);
    CHECK(std::fabs((trimmed.level - base.level) - 10.0 / 60.0) < 0.01, "%f -> %f", base.level, trimmed.level);
    t.inputTrimDb = 99.0f;
    SoundEngine e(48000);
    e.setTuning(t);
    CHECK(e.tuning().inputTrimDb == 24.0f, "the trim clamps to +24 dB, %f", static_cast<double>(e.tuning().inputTrimDb));
}

// Every stage reads the one gate: digital silence is no loudness, no drive, no busy, no hits.
void testSilenceIsOneThing() {
    const Heard h = hear([](int64_t, float& l, float& r) { l = r = 0.0f; }, 5.0, 1.0);
    CHECK(h.drive == 0.0 && h.busy == 0.0 && h.loudnessS == 0.0 && h.kicks == 0 && h.onsets == 0, "drive %f busy %f", h.drive, h.busy);
}

}  // namespace

int main() {
    testLouderReadsHigherAndInRange();
    testSilenceReadsZeroAndGates();
    testMomentaryTracksFasterThanShortTermOnARise();
    testOddSampleRate();
    testPadIsQuietAndFatIsNot();
    testQuieterIsQuieter();
    testDenseOutdrivesSparseAtEqualLoudness();
    testNearSilenceFiresNoHits();
    testInputTrimReachesAnalysis();
    testSilenceIsOneThing();
    if (failures == 0) std::printf("absolute_level_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
