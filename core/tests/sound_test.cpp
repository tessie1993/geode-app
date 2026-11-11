// Host test for the sound engine (core/sound), built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Icore core/tests/sound_test.cpp core/sound/*.cpp -o sound_test && ./sound_test
// Synthetic signals only, so every number here is reproducible.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <numbers>
#include <vector>

#include "sound/PlayerSignals.hpp"
#include "sound/SoundEngine.hpp"
#include "sound/Tuning.hpp"

using geode::sound::SoundEngine;

namespace {

int failures = 0;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            failures++;                                        \
            std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

constexpr double kTau = 2.0 * std::numbers::pi;

// A deterministic noise source, so the "music" is the same on every run.
struct Noise {
    uint32_t state = 12345;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

using Signal = std::function<void(int64_t sample, float& left, float& right)>;

// Writes exactly the audio up to each frame's end, then steps: what the export path does.
void run(SoundEngine& e, const Signal& signal, double seconds, double fps,
         const std::function<void(double t, const GeodeSoundFrame&)>& onFrame) {
    const int rate = e.sampleRateHz();
    const int frames = static_cast<int>(seconds * fps);
    int64_t done = 0;
    std::vector<float> buf;
    GeodeSoundFrame f{};
    for (int i = 1; i <= frames; i++) {
        const int64_t end = static_cast<int64_t>(std::llround(i * rate / fps));
        buf.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) signal(s, buf[2 * (s - done)], buf[2 * (s - done) + 1]);
        e.write(buf.data(), static_cast<int>(end - done), 2);
        done = end;
        e.step(static_cast<float>(1.0 / fps), f);
        onFrame(i / fps, f);
    }
}

Signal noise(float amp) {
    auto n = std::make_shared<Noise>();
    return [n, amp](int64_t, float& l, float& r) {
        l = amp * n->next();
        r = amp * n->next();
    };
}

Signal tone(double hz, float amp, int rate) {
    return [=](int64_t s, float& l, float& r) {
        const double t = static_cast<double>(s) / rate;
        const float v = amp * static_cast<float>(std::sin(kTau * hz * t) + 0.5 * std::sin(kTau * 2 * hz * t) +
                                                 0.3 * std::sin(kTau * 3 * hz * t));
        l = v;
        r = v;
    };
}

void testFftSizes() {
    CHECK(SoundEngine::fftSizeFor(44100) == 2048, "%d", SoundEngine::fftSizeFor(44100));
    CHECK(SoundEngine::fftSizeFor(48000) == 2048, "%d", SoundEngine::fftSizeFor(48000));
    CHECK(SoundEngine::fftSizeFor(96000) == 4096, "%d", SoundEngine::fftSizeFor(96000));
    CHECK(SoundEngine::fftSizeFor(192000) == 8192, "%d", SoundEngine::fftSizeFor(192000));
}

// Steady music reads ~1.0 on every relative level, which is the whole point of the MilkDrop core.
void testSteadyReadsOne() {
    SoundEngine e(48000);
    double bass = 0, mid = 0, treble = 0, low = 0;
    int n = 0;
    run(e, noise(0.3f), 8.0, 60.0, [&](double t, const GeodeSoundFrame& f) {
        if (t < 5.0) return;
        bass += f.bass;
        mid += f.mid;
        treble += f.treble;
        low += f.low;
        n++;
    });
    CHECK(std::fabs(bass / n - 1.0) < 0.15, "bass %f", bass / n);
    CHECK(std::fabs(mid / n - 1.0) < 0.15, "mid %f", mid / n);
    CHECK(std::fabs(treble / n - 1.0) < 0.15, "treble %f", treble / n);
    CHECK(std::fabs(low / n - 0.5) < 0.15, "low %f", low / n);
}

// The same audio at 30 and at 120 frames per second settles to the same levels.
void testFrameRateIndependent() {
    double att[2] = {0, 0};
    const double fps[2] = {30.0, 120.0};
    for (int i = 0; i < 2; i++) {
        SoundEngine e(44100);
        int n = 0;
        run(e, noise(0.2f), 8.0, fps[i], [&](double t, const GeodeSoundFrame& f) {
            if (t < 5.0) return;
            att[i] += f.bassAtt;
            n++;
        });
        att[i] /= n;
    }
    CHECK(std::fabs(att[0] - att[1]) < 0.15, "30 fps %f vs 120 fps %f", att[0], att[1]);
}

// Kicks over quiet noise: one trigger per kick, and kick4 on every fourth.
void testKicks() {
    const int rate = 48000;
    SoundEngine e(rate);
    auto bed = std::make_shared<Noise>();
    int kicks = 0, kick4 = 0, snares = 0;
    float lastToggle = 0.0f;
    float maxEnv = 0;
    float hitTime = 0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            const double phase = std::fmod(t, 0.5);
            const double k = phase < 0.15 ? std::sin(kTau * 55.0 * phase) * std::exp(-phase / 0.05) : 0.0;
            l = r = static_cast<float>(0.8 * k) + 0.01f * bed->next();
        },
        8.0, 60.0,
        [&](double, const GeodeSoundFrame& f) {
            if (f.kick > 0) kicks++;
            if (f.kick4 > 0) kick4++;
            if (f.snare > 0) snares++;
            lastToggle = f.kickToggle;
            maxEnv = std::max(maxEnv, f.kickEnv);
            hitTime = f.hitTime;
        });
    CHECK(kicks >= 14 && kicks <= 16, "kicks %d of 16", kicks);
    CHECK(kick4 == kicks / 4, "kick4 %d for %d kicks", kick4, kicks);
    CHECK(maxEnv > 0.3f, "kickEnv peak %f", maxEnv);
    CHECK(std::fabs(hitTime - (kicks + snares)) < 0.5f, "hitTime %f for %d kicks + %d snares", hitTime, kicks, snares);
    // The toggle flips on every kick, so after an even count it rests at 0, after an odd one at 1.
    CHECK(std::fabs(lastToggle - static_cast<float>(kicks % 2)) < 0.05f, "toggle %f after %d kicks", lastToggle, kicks);
}

// One poisoned buffer (NaN, Inf, a sample whose square overflows) must not kill the engine for the
// session (#365): after it the kicks fire again and every level reads a finite number.
void testNonFiniteInput() {
    const int rate = 48000;
    SoundEngine e(rate);
    auto bed = std::make_shared<Noise>();
    int kicksAfter = 0;
    bool finite = true;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            const double phase = std::fmod(t, 0.5);
            const double k = phase < 0.15 ? std::sin(kTau * 55.0 * phase) * std::exp(-phase / 0.05) : 0.0;
            l = r = static_cast<float>(0.8 * k) + 0.01f * bed->next();
            if (s >= 2 * rate && s < 2 * rate + 480) l = r = std::nanf("");
            if (s == 2 * rate + 480) l = r = INFINITY;
            if (s == 2 * rate + 481) l = -INFINITY;
            if (s == 2 * rate + 482) l = r = 1e20f;
        },
        8.0, 60.0,
        [&](double t, const GeodeSoundFrame& f) {
            if (t < 4.0) return;
            if (f.kick > 0) kicksAfter++;
            for (const float v : {f.bass, f.mid, f.treble, f.low, f.middle, f.high, f.kickEnv}) {
                if (!std::isfinite(v)) finite = false;
            }
        });
    CHECK(kicksAfter >= 6, "kicks after the poisoned buffer %d of 8", kicksAfter);
    CHECK(finite, "a level stayed non-finite after the poisoned buffer");
}

// Live writes arrive in uneven chunks; export writes once per frame. Same audio, same frame boundaries:
// bit-identical frames. This is the parity the export depends on.
void testChunkingIsBitExact() {
    const int rate = 44100;
    SoundEngine a(rate), b(rate);
    auto src = tone(330.0, 0.3f, rate);
    auto bed = noise(0.05f);
    std::vector<float> frame;
    GeodeSoundFrame fa{}, fb{};
    int64_t done = 0;
    bool same = true;
    for (int i = 1; i <= 600; i++) {
        const int64_t end = static_cast<int64_t>(std::llround(i * rate / 60.0));
        frame.resize(static_cast<size_t>(2 * (end - done)));
        for (int64_t s = done; s < end; s++) {
            float l, r, nl, nr;
            src(s, l, r);
            bed(s, nl, nr);
            frame[2 * (s - done)] = l + nl;
            frame[2 * (s - done) + 1] = r + nr;
        }
        const int count = static_cast<int>(end - done);
        a.write(frame.data(), count, 2);
        for (int at = 0; at < count; at += 97) b.write(frame.data() + 2 * at, std::min(97, count - at), 2);
        done = end;
        a.step(1.0f / 60.0f, fa);
        b.step(1.0f / 60.0f, fb);
        same = same && std::memcmp(&fa, &fb, sizeof fa) == 0;
    }
    CHECK(same, "chunked and whole writes diverged");
}

// A held note reads its pitch; gliding to a new note fires noteChange and legato, with no attack.
void testMelody() {
    const int rate = 48000;
    SoundEngine e(rate);
    double phase = 0.0;
    int changes = 0, legatos = 0;
    float pitchHeld = 0, pitchClass = 0, interval = 0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            const double hz = t < 2.0 ? 220.0 : 329.63;  // A3, then E4, phase-continuous
            phase += kTau * hz / rate;
            const float v = 0.3f * static_cast<float>(std::sin(phase) + 0.5 * std::sin(2 * phase) + 0.3 * std::sin(3 * phase));
            l = r = v;
        },
        3.0, 60.0,
        [&](double t, const GeodeSoundFrame& f) {
            if (t > 1.5 && t < 1.9) {
                pitchHeld = f.pitch;
                pitchClass = f.pitchClass;
            }
            interval = f.interval;
            if (t > 2.0 && f.noteChange > 0) changes++;
            if (t > 2.0 && f.legato > 0) legatos++;
        });
    CHECK(std::fabs(pitchHeld - 57.0f) < 0.5f, "A3 read as %f", pitchHeld);
    CHECK(changes == 1, "note changes after the glide %d", changes);
    CHECK(legatos == 1, "legato after the glide %d", legatos);
    CHECK(std::fabs(pitchClass - 9.0f / 12.0f) < 0.03f, "A's pitch class %f", pitchClass);
    CHECK(std::fabs(interval - 7.0f / 12.0f) < 0.01f, "A3 to E4 interval %f", interval);
}

// A tracker slipping an octave for a moment (here the tone itself does, every 0.1 s) is the same note:
// one note change, not one per slip, and pitch stays in the held octave.
void testOctaveSlip() {
    const int rate = 48000;
    SoundEngine e(rate);
    int changes = 0;
    float lo = 1000, hi = 0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            const double hz = std::fmod(t, 0.2) < 0.1 ? 220.0 : 440.0;
            const double ph = kTau * hz * t;
            l = r = static_cast<float>(0.3 * (std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.3 * std::sin(3 * ph)));
        },
        2.0, 60.0,
        [&](double t, const GeodeSoundFrame& f) {
            if (f.noteChange > 0) changes++;
            if (t > 0.5 && f.pitch > 0) {
                lo = std::min(lo, f.pitch);
                hi = std::max(hi, f.pitch);
            }
        });
    CHECK(changes == 1, "note changes across octave slips %d", changes);
    CHECK(hi - lo < 1.0f, "pitch wandered %f..%f", lo, hi);
}

// The same note struck again (level drops, then jumps back, pitch unchanged) is a re-attack, not a change.
void testReattack() {
    const int rate = 48000;
    SoundEngine e(rate);
    int reattacks = 0, changes = 0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            const double amp = std::fmod(t, 0.4) < 0.3 ? 0.3 : 0.03;
            const double ph = kTau * 440.0 * t;
            l = r = static_cast<float>(amp * (std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.3 * std::sin(3 * ph)));
        },
        2.4, 60.0,
        [&](double, const GeodeSoundFrame& f) {
            if (f.reattack > 0) reattacks++;
            if (f.noteChange > 0) changes++;
        });
    CHECK(reattacks >= 3, "reattacks %d", reattacks);
    CHECK(changes == 1, "note changes on one repeated note %d", changes);
}

void testSilenceAndReturn() {
    const int rate = 44100;
    SoundEngine e(rate);
    auto t1 = tone(440.0, 0.3f, rate);
    int silences = 0, returns = 0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            const double t = static_cast<double>(s) / rate;
            if (t >= 2.0 && t < 4.0) {
                l = r = 0.0f;
            } else {
                t1(s, l, r);
            }
        },
        6.0, 60.0,
        [&](double, const GeodeSoundFrame& f) {
            if (f.silence > 0) silences++;
            if (f.returned > 0) returns++;
        });
    CHECK(silences == 1, "silence fired %d", silences);
    CHECK(returns == 1, "returned fired %d", returns);
}

// A steady tone draws a waveform that holds still from frame to frame.
void testWaveformHoldsStill() {
    const int rate = 44100;
    SoundEngine e(rate);
    std::vector<float> prev(GEODE_SOUND_WAVEFORM_POINTS, 0.0f);
    double drift = 0;
    int n = 0;
    run(e, tone(441.0, 0.4f, rate), 2.0, 60.0, [&](double t, const GeodeSoundFrame& f) {
        if (t > 1.0) {
            for (int i = 32; i < 96; i++) drift += std::fabs(f.waveform[i] - prev[i]);
            n += 64;
        }
        std::memcpy(prev.data(), f.waveform, sizeof(float) * GEODE_SOUND_WAVEFORM_POINTS);
    });
    CHECK(drift / n < 0.08, "waveform moved %f per point", drift / n);
}

// When the writer stops, the engine hears silence instead of looping the last window forever.
void testStarvedGoesQuiet() {
    SoundEngine e(48000);
    std::vector<float> buf(2 * 4800);
    Noise n;
    for (float& v : buf) v = 0.3f * n.next();
    e.write(buf.data(), 4800, 2);
    GeodeSoundFrame f{};
    for (int i = 0; i < 30; i++) e.step(1.0f / 60.0f, f);
    CHECK(f.rms == 0.0f, "rms after starving %f", f.rms);
}

// A decoder hands over about 100 ms at a time, so a block a frame or two late leaves the engine with
// nothing new for 100 ms or more. That is jitter, not a stall: no frame between blocks may read silence
// (kills kStarved back at 0.1 s, where the 8-frame gap's 117 ms starves).
void testLateBlocksAreNotStarvation() {
    for (const int gap : {7, 8, 12}) {
        SoundEngine e(48000);
        std::vector<float> buf(2 * 4800);
        Noise n;
        GeodeSoundFrame f{};
        int zeros = 0;
        for (int i = 0; i < 20 * gap; i++) {
            if (i % gap == 0) {
                for (float& v : buf) v = 0.3f * n.next();
                e.write(buf.data(), 4800, 2);
            }
            e.step(1.0f / 60.0f, f);
            if (f.rms == 0.0f) zeros++;
        }
        CHECK(zeros == 0, "100 ms blocks every %d frames: %d frames read silence", gap, zeros);
    }
}

// The spectrum row is behind the silence gate like everything else: music, then 30 s of noise at about
// -70 dBFS (under the gate), must not be stretched to full scale as the row's peak decays to the noise.
void testSpectrumStaysGatedUnderNoise() {
    const int rate = 48000;
    SoundEngine e(rate);
    const Signal music = noise(0.3f);
    const Signal hiss = noise(4e-4f);
    const Signal signal = [&](int64_t s, float& l, float& r) { (s < 5 * rate ? music : hiss)(s, l, r); };
    float playing = 0.0f;
    float gated = 0.0f;
    run(e, signal, 35.0, 60.0, [&](double t, const GeodeSoundFrame& f) {
        const float top = *std::max_element(f.spectrum, f.spectrum + GEODE_SOUND_SPECTRUM_POINTS);
        if (t > 4.0 && t < 5.0) playing = std::max(playing, top);
        if (t > 7.0) gated = std::max(gated, top);
    });
    CHECK(playing > 0.5f, "the music fills the spectrum row: max %f", playing);
    CHECK(gated < 0.1f, "noise under the gate reached %f of full scale in the spectrum row", gated);
}

// A mic or a capture with a DC offset and nothing playing is silence: the offset must not read as level,
// hold `presence` up or keep `silence` from firing. And the blocker (about 10 Hz) leaves the bass alone.
void testDcOffsetIsSilence() {
    SoundEngine e(48000);
    int silences = 0;
    GeodeSoundFrame last{};
    run(e, [](int64_t, float& l, float& r) { l = r = 0.2f; }, 4.0, 60.0, [&](double, const GeodeSoundFrame& f) {
        if (f.silence > 0) silences++;
        last = f;
    });
    CHECK(silences == 1, "a constant offset of 0.2 fired silence %d times", silences);
    CHECK(last.rms < 1e-4f && last.level == 0.0f, "the offset still reads as level: rms %f level %f", last.rms, last.level);
    CHECK(last.presence < 0.05f, "the offset holds presence at %f", last.presence);

    // 100 Hz at 40960 Hz: the 2048-sample window holds five whole cycles, so its rms is exact at any phase.
    const int rate = 40960;
    SoundEngine bass(rate);
    CHECK(SoundEngine::fftSizeFor(rate) == 2048, "%d", SoundEngine::fftSizeFor(rate));
    const Signal sine = [rate](int64_t s, float& l, float& r) {
        l = r = 0.3f * static_cast<float>(std::sin(kTau * 100.0 * static_cast<double>(s) / rate));
    };
    run(bass, sine, 2.0, 60.0, [&](double, const GeodeSoundFrame& f) { last = f; });
    const double want = 0.3 / std::sqrt(2.0);
    CHECK(std::fabs(last.rms / want - 1.0) < 0.01, "a 100 Hz sine's rms %f, without the blocker %f", last.rms, want);
}

// A loud entrance after a quiet intro is MilkDrop's hard cut: it fires at once, and only once. Steady
// music never cuts. On soft-limited ratios (below 4) the starting threshold of 5 could never be reached.
void testHardCut() {
    const int rate = 44100;
    SoundEngine e(rate);
    auto quiet = noise(0.01f);
    auto loud = noise(0.5f);
    int cuts = 0;
    double firstCut = -1.0;
    run(
        e,
        [&](int64_t s, float& l, float& r) {
            if (static_cast<double>(s) / rate < 6.0) {
                quiet(s, l, r);
            } else {
                loud(s, l, r);
            }
        },
        12.0, 60.0,
        [&](double t, const GeodeSoundFrame& f) {
            if (f.hardCut > 0) {
                cuts++;
                if (firstCut < 0) firstCut = t;
            }
        });
    CHECK(cuts == 1, "hard cuts on one entrance %d", cuts);
    CHECK(firstCut >= 6.0 && firstCut < 6.1, "hard cut at %f s, entrance at 6", firstCut);

    SoundEngine steady(rate);
    int steadyCuts = 0;
    run(steady, noise(0.3f), 20.0, 60.0, [&](double, const GeodeSoundFrame& f) {
        if (f.hardCut > 0) steadyCuts++;
    });
    CHECK(steadyCuts == 0, "hard cuts on steady noise %d", steadyCuts);
}

// The player's fade gain (#460): the frame carries it as fadeGain and nothing else moves, since the tap sits
// before the volume and the engine never hears a fade. Clamped to 0..1, 1 when not a number, kept by reset().
void testFadeGain() {
    const int rate = 48000;
    SoundEngine plain(rate);
    SoundEngine faded(rate);
    plain.setPlayerSource(true);
    faded.setPlayerSource(true);
    faded.setFadeGain(0.25f);
    std::vector<GeodeSoundFrame> plainFrames;
    run(plain, noise(0.2f), 2.0, 60.0, [&](double, const GeodeSoundFrame& f) { plainFrames.push_back(f); });
    size_t i = 0;
    int differ = 0;
    int wrongGain = 0;
    // noise() starts from the same state each time, so this is the same audio.
    run(faded, noise(0.2f), 2.0, 60.0, [&](double, const GeodeSoundFrame& f) {
        if (f.fadeGain != 0.25f || plainFrames[i].fadeGain != 1.0f) wrongGain++;
        GeodeSoundFrame g = f;
        g.fadeGain = plainFrames[i].fadeGain;
        if (std::memcmp(&g, &plainFrames[i], sizeof g) != 0) differ++;
        i++;
    });
    CHECK(wrongGain == 0, "%d frames carried the wrong gain", wrongGain);
    CHECK(differ == 0, "the fade moved %d frames' other values", differ);

    GeodeSoundFrame f{};
    faded.setFadeGain(2.0f);
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 1.0f, "above 1 clamps to 1, read %f", f.fadeGain);
    faded.setFadeGain(-1.0f);
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 0.0f, "below 0 clamps to 0, read %f", f.fadeGain);
    faded.setFadeGain(NAN);
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 1.0f, "not a number reads 1, read %f", f.fadeGain);
    faded.setFadeGain(0.5f);
    faded.reset();
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 0.5f, "reset keeps the gain, read %f", f.fadeGain);

    // Under the mic or another app's audio the player's fade is not what the engine hears: it reads 1.
    faded.setPlayerSource(false);
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 1.0f, "a capture source reads a fade gain of 1, read %f", f.fadeGain);
    faded.setPlayerSource(true);
    faded.step(1.0f / 60.0f, f);
    CHECK(f.fadeGain == 0.5f, "back on the player the fade returns, read %f", f.fadeGain);
}

// The process-wide signals the player pushes and each live renderer latches once a frame.
void testPlayerSignals() {
    namespace player = geode::sound::player;
    const uint64_t before = player::boundary().epoch;
    const uint64_t first = player::trackBoundary(0x1234);
    CHECK(first == before + 1, "a boundary adds one to the epoch: %llu after %llu", static_cast<unsigned long long>(first),
          static_cast<unsigned long long>(before));
    const player::Boundary b = player::boundary();
    CHECK(b.epoch == first && b.seed == 0x1234, "the latched boundary carries its seed");
    CHECK(player::trackBoundary(0x1234) == first + 1, "the same seed again is still a new boundary (a replay)");
    player::setFadeGain(0.3f);
    CHECK(player::fadeGain() == 0.3f, "the fade gain latches as pushed");
    player::setFadeGain(1.0f);
    CHECK(player::playerIsSource(), "the player owns the ring until a capture takes it");
    player::setPlayerIsSource(false);
    CHECK(!player::playerIsSource(), "a capture takes the ring");
    player::setPlayerIsSource(true);
}

// A boundary fires one full-strength section change, on the next step only, and survives a reset() between
// (a seek or a source switch on the same frame).
void testBoundaryEvent() {
    const int rate = 48000;
    SoundEngine e(rate);
    int changes = 0;
    run(e, noise(0.2f), 2.0, 60.0, [&](double, const GeodeSoundFrame& f) { changes += f.sectionChange > 0.0f ? 1 : 0; });
    CHECK(changes == 0, "steady noise fires no section change, fired %d", changes);
    GeodeSoundFrame f{};
    e.trackBoundary();
    e.reset();
    e.step(1.0f / 60.0f, f);
    CHECK(f.sectionChange == 1.0f, "the boundary fires at full strength, read %f", f.sectionChange);
    e.step(1.0f / 60.0f, f);
    CHECK(f.sectionChange == 0.0f, "once, read %f on the next frame", f.sectionChange);
}

// Settings › Audio's tuning schema (sound/Tuning), pinned to the Kotlin SoundTuning it replaced: its
// defaults, its ranges, and its wire order (the slots are GeodeSoundTuning's fields).
float slotOf(const GeodeSoundTuning& t, int i) {
    float v[geode::sound::tuning::kSlots];
    std::memcpy(v, &t, sizeof v);
    return v[i];
}

void testTuningSchema() {
    namespace tuning = geode::sound::tuning;
    CHECK(tuning::kSlots == 30 && GEODE_SOUND_TUNING_FLOATS == 30, "30 slots, %d", tuning::kSlots);
    struct Old {
        int slot;
        float def, lo, hi;
    };
    // The old Kotlin defaults and ranges, slot by slot in its toFloats order.
    const Old old[] = {
        {0, 1.0f, 0.0f, 4.0f},     {1, 1.0f, 0.0f, 4.0f},     {2, 1.0f, 0.0f, 4.0f},       // gain
        {3, 0.0f, 0.0f, 1.0f},     {4, 0.0f, 0.0f, 1.0f},     {5, 0.0f, 0.0f, 1.0f},       // threshold
        {6, 0.0f, -1.0f, 1.0f},    {7, 0.0f, -1.0f, 1.0f},    {8, 0.0f, -1.0f, 1.0f},      // offset
        {9, 0.03f, 0.0f, 1.0f},    {10, 0.03f, 0.0f, 1.0f},   {11, 0.03f, 0.0f, 1.0f},     // attack
        {12, 0.15f, 0.0f, 2.0f},   {13, 0.15f, 0.0f, 2.0f},   {14, 0.15f, 0.0f, 2.0f},     // release
        {15, 0.6f, 0.1f, 3.0f},    {16, 0.5f, 0.1f, 3.0f},    {17, 0.5f, 0.1f, 3.0f},      // drum
        {18, 0.18f, 0.02f, 1.0f},  {19, 0.15f, 0.02f, 1.0f},  {20, 0.08f, 0.02f, 1.0f},    // drumDecay
        {21, 1.0f, 0.0f, 3.0f},    {22, 1.0f, 0.0f, 4.0f},                                // reactivity, speed
        {23, 2.0f, 1.0f, 4.0f},    {24, 2.5f, 1.5f, 4.0f},    {25, 60.0f, 5.0f, 180.0f},   // onset, cut, half-life
        {26, 0.35f, 0.1f, 0.9f},   {27, 0.08f, 0.02f, 0.5f},  {28, -60.0f, -90.0f, -30.0f},  // pitch, hold, silence
        {29, 0.0f, -24.0f, 24.0f},                                                       // input trim
    };
    CHECK(std::size(old) == static_cast<size_t>(tuning::kSlots), "every slot pinned");
    const GeodeSoundTuning d = tuning::defaults();
    for (const Old& o : old) {
        const tuning::Slot& s = tuning::slots()[static_cast<size_t>(o.slot)];
        CHECK(s.def == o.def && s.lo == o.lo && s.hi == o.hi, "slot %d schema", o.slot);
        CHECK(slotOf(d, o.slot) == o.def, "slot %d default %f", o.slot, static_cast<double>(slotOf(d, o.slot)));
    }
    CHECK(SoundEngine::defaultTuning().drum[0] == 0.6f && d.reactivity == 1.0f && d.silenceDb == -60.0f,
          "the struct's fields are the slots");

    // Every range clamps at both ends and leaves the inside alone; NaN and both infinities are the default.
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    for (const Old& o : old) {
        float all[tuning::kSlots];
        std::memcpy(all, &d, sizeof all);
        all[o.slot] = o.hi + 1.0f;
        tuning::clamp(all, tuning::kSlots);
        CHECK(all[o.slot] == o.hi, "slot %d clamps high", o.slot);
        all[o.slot] = o.lo - 1.0f;
        tuning::clamp(all, tuning::kSlots);
        CHECK(all[o.slot] == o.lo, "slot %d clamps low", o.slot);
        const float mid = 0.5f * (o.lo + o.hi);
        all[o.slot] = mid;
        tuning::clamp(all, tuning::kSlots);
        CHECK(all[o.slot] == mid, "slot %d keeps a value inside", o.slot);
        for (float b : bad) {
            all[o.slot] = b;
            tuning::clamp(all, tuning::kSlots);
            CHECK(all[o.slot] == o.def, "slot %d: a non-finite value is the default", o.slot);
        }
    }

    // A short array clamps only what it has; the struct form clamps the same way.
    float two[2] = {9.0f, std::numeric_limits<float>::quiet_NaN()};
    tuning::clamp(two, 2);
    CHECK(two[0] == 4.0f && two[1] == 1.0f, "two slots: gain clamped to its top, NaN gain is 1");
    tuning::clamp(nullptr, 5);
    GeodeSoundTuning wild = d;
    wild.reactivity = 9.0f;
    wild.speed = std::numeric_limits<float>::quiet_NaN();
    wild.attack[0] = -1.0f;
    wild.attack[2] = 5.0f;
    const GeodeSoundTuning c = tuning::clamp(wild);
    CHECK(c.reactivity == 3.0f && c.speed == 1.0f && c.attack[0] == 0.0f && c.attack[1] == 0.03f && c.attack[2] == 1.0f,
          "the struct clamps as the old Kotlin clamped() did");
}

// The engine clamps whatever it is handed, so no surface can feed it a NaN or an out-of-range knob.
void testEngineClampsTuning() {
    SoundEngine e(48000);
    GeodeSoundTuning t = SoundEngine::defaultTuning();
    t.reactivity = std::numeric_limits<float>::quiet_NaN();
    t.speed = 100.0f;
    t.silenceDb = 0.0f;
    t.gain[1] = -3.0f;
    e.setTuning(t);
    CHECK(e.tuning().reactivity == 1.0f, "NaN reactivity is the default, %f", static_cast<double>(e.tuning().reactivity));
    CHECK(e.tuning().speed == 4.0f && e.tuning().silenceDb == -30.0f && e.tuning().gain[1] == 0.0f,
          "out-of-range slots clamp in the engine");
}

}  // namespace

int main() {
    testFftSizes();
    testSteadyReadsOne();
    testFrameRateIndependent();
    testKicks();
    testNonFiniteInput();
    testChunkingIsBitExact();
    testMelody();
    testReattack();
    testOctaveSlip();
    testSilenceAndReturn();
    testWaveformHoldsStill();
    testStarvedGoesQuiet();
    testLateBlocksAreNotStarvation();
    testSpectrumStaysGatedUnderNoise();
    testDcOffsetIsSilence();
    testHardCut();
    testFadeGain();
    testPlayerSignals();
    testBoundaryEvent();
    testTuningSchema();
    testEngineClampsTuning();
    if (failures == 0) std::printf("sound_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
