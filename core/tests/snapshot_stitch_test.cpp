// Host test for core/audio/ingest/SnapshotStitch: the wallpaper's output-mix capture hands over snapshots
// (the newest 1024 samples, ~20 times a second), and the ring must hear them as real time, without seams.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/snapshot_stitch_test.cpp
//   core/audio/ingest/SnapshotStitch.cpp core/audio/ingest/PcmRing.cpp core/api/geode_ingest_api.cpp
//   core/sound/*.cpp -o snapshot_stitch_test && ./snapshot_stitch_test
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/PcmRing.hpp"
#include "audio/ingest/SnapshotStitch.hpp"
#include "sound/SoundEngine.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::kUnsigned8;
using geode::audio::ingest::PcmRing;
using geode::audio::ingest::SnapshotStitch;

static int failures = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                       \
            std::fprintf(stderr, "\n");                              \
            ++failures;                                              \
        }                                                            \
    } while (0)

namespace {

constexpr int kRate = 48000;
constexpr int kSnap = 1024;  // Visualizer's largest capture size
constexpr double kTau = 2.0 * std::numbers::pi;
// As OutputMixCapture makes it.
constexpr int kRingFrames = 32768;

using Signal = std::function<double(double)>;

// The newest kSnap samples of `signal` at capture time t, as Visualizer hands them: 8-bit unsigned.
std::vector<uint8_t> snapshotAt(const Signal& signal, double t) {
    std::vector<uint8_t> bytes(kSnap);
    const int64_t end = std::llround(t * kRate);
    for (int i = 0; i < kSnap; ++i) {
        const double v = signal(static_cast<double>(end - kSnap + i) / kRate);
        bytes[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(std::lround(v * 128.0 + 128.0), 0L, 255L));
    }
    return bytes;
}

int64_t ns(double seconds) { return std::llround(seconds * 1e9); }

// Capture times: every 50 ms from `start`, with up to ±jitterMs of the Looper's lateness, deterministic.
std::vector<double> captureTimes(double start, double seconds, double jitterMs) {
    std::vector<double> times;
    uint32_t r = 12345u;
    for (double t = start; t < start + seconds; t += 0.05) {
        r = r * 1664525u + 1013904223u;
        const double jitter = (static_cast<double>(r >> 8) / 16777216.0 * 2.0 - 1.0) * jitterMs * 1e-3;
        times.push_back(t + jitter);
    }
    return times;
}

// Every frame the cursor hands out after `ring` was written, mono (the stitch writes one channel).
std::vector<float> drain(Cursor& cursor) {
    std::vector<float> out;
    for (;;) {
        const Cursor::Take take = cursor.take();
        if (take.frames <= 0) break;
        for (int i = 0; i < take.frames; ++i) out.push_back(cursor.interleaved()[static_cast<size_t>(i) * cursor.channels()]);
    }
    return out;
}

double rms(const std::vector<float>& v, size_t from) {
    double sum = 0.0;
    for (size_t i = from; i < v.size(); ++i) sum += static_cast<double>(v[i]) * v[i];
    return std::sqrt(sum / static_cast<double>(v.size() - from));
}

float maxStep(const std::vector<float>& v, size_t from) {
    float worst = 0.0f;
    for (size_t i = from + 1; i < v.size(); ++i) worst = std::max(worst, std::fabs(v[i] - v[i - 1]));
    return worst;
}

const Signal kSine = [](double t) { return 0.5 * std::sin(kTau * 440.0 * t); };

// The ring gains the capture clock's time, not the snapshots' length: 10 s of captures is 10 s of frames.
// Written raw (as before) the same captures were 0.43 s a second, with a seam every 1024 samples.
void testRealTimeNoSeams() {
    PcmRing stitchedRing(kRingFrames, 2);
    PcmRing rawRing(kRingFrames, 2);
    Cursor stitchedCursor(stitchedRing);
    Cursor rawCursor(rawRing);
    SnapshotStitch stitch;
    std::vector<float> stitched;
    std::vector<float> raw;
    const std::vector<double> times = captureTimes(1.0, 10.0, 3.0);
    for (double t : times) {
        const std::vector<uint8_t> snap = snapshotAt(kSine, t);
        stitch.write(stitchedRing, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(t));
        rawRing.writeBytes(snap.data(), snap.size(), kUnsigned8, 1, kRate);
        for (float v : drain(stitchedCursor)) stitched.push_back(v);
        for (float v : drain(rawCursor)) raw.push_back(v);
    }
    const double span = times.back() - times.front();
    const double expected = span * kRate + kSnap;
    std::printf("  10 s of captures: stitched %zu frames (%.0f expected), raw %zu\n", stitched.size(), expected, raw.size());
    CHECK(std::fabs(static_cast<double>(stitched.size()) - expected) < 0.01 * kRate,
          "stitched %zu frames, the clock says %.0f", stitched.size(), expected);
    CHECK(raw.size() == times.size() * kSnap, "raw wrote %zu", raw.size());

    // A 440 Hz sine at 0.5 moves at most 0.029 a sample (plus 8-bit steps); a seam jumps anywhere.
    const float smooth = maxStep(stitched, 0);
    const float seams = maxStep(raw, 0);
    std::printf("  largest sample step: stitched %.3f, raw %.3f\n", smooth, seams);
    CHECK(smooth < 0.06f, "a seam in the stitched stream: a step of %.3f", smooth);
    CHECK(seams > 0.2f, "the raw stream had no seam to show (%.3f)", seams);

    // The level holds: the sine's RMS (0.354) within 1.5 dB, past the first grain's fade-in.
    const double level = rms(stitched, kSnap);
    const double db = 20.0 * std::log10(level / (0.5 / std::sqrt(2.0)));
    std::printf("  stitched RMS %.3f (%+.2f dB)\n", level, db);
    CHECK(std::fabs(db) < 1.5, "stitched level %+.2f dB off the sine's", db);
}

// A break longer than a gap (the capture stopped and started again, a long stall) is not filled.
void testBreakStartsOver() {
    PcmRing ring(kRingFrames, 2);
    SnapshotStitch stitch;
    const std::vector<uint8_t> snap = snapshotAt(kSine, 1.0);
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.0));
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.05));
    const int64_t before = ring.writtenFrames();
    CHECK(before == kSnap + 2400, "a 50 ms gap: %lld frames", static_cast<long long>(before));
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(3.0));
    CHECK(ring.writtenFrames() - before == kSnap, "a 2 s break filled with %lld frames",
          static_cast<long long>(ring.writtenFrames() - before));
    // A rate change is a new run too.
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, 44100, ns(3.05));
    CHECK(ring.writtenFrames() - before == 2 * kSnap, "a new rate was filled from the old clock");

    // Nor a gap longer than a reader takes at once (the ring's runway): its start would be skipped.
    PcmRing small(16384, 2);
    SnapshotStitch other;
    other.write(small, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.0));
    other.write(small, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.08));
    CHECK(small.writtenFrames() == kSnap + 3840, "an 80 ms gap under the runway: %lld", static_cast<long long>(small.writtenFrames()));
    other.write(small, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.18));
    CHECK(small.writtenFrames() == 2 * kSnap + 3840, "a 100 ms gap over the runway was filled");
}

// Two snapshots at once (the Looper was busy) each still sound, and the clock takes the extra back.
void testBunched() {
    PcmRing ring(kRingFrames, 2);
    SnapshotStitch stitch;
    const std::vector<uint8_t> snap = snapshotAt(kSine, 1.0);
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.0));
    const int64_t a = ring.writtenFrames();
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.1));
    const int64_t b = ring.writtenFrames();
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.1));
    const int64_t c = ring.writtenFrames();
    CHECK(b - a == 4800 && c - b == kSnap / 4, "late then bunched: %lld, %lld", static_cast<long long>(b - a),
          static_cast<long long>(c - b));
    stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(1.15));
    CHECK(ring.writtenFrames() - c == 2400 - kSnap / 4, "the clock did not take the extra back: %lld",
          static_cast<long long>(ring.writtenFrames() - c));
}

// The C ABI: a snapshot ring stitches, a plain one writes as before.
void testAbi() {
    geode_pcm_ring* ring = geode_pcm_ring_create(kRingFrames, 2, 0);
    const std::vector<uint8_t> snap = snapshotAt(kSine, 1.0);
    CHECK(geode_pcm_ring_write_bytes(ring, snap.data(), snap.size(), GEODE_PCM_U8, 1, kRate, nullptr) == kSnap,
          "a plain ring wrote the block as it is");
    CHECK(geode_pcm_ring_set_snapshots(ring, 1) == 1, "snapshots refused");
    float peak = 0.0f;
    CHECK(geode_pcm_ring_write_bytes(ring, snap.data(), snap.size(), GEODE_PCM_U8, 1, kRate, &peak) == kSnap,
          "a snapshot ring's first write is the snapshot");
    CHECK(peak > 0.45f && peak < 0.55f, "snapshot peak %f", peak);
    CHECK(geode_pcm_ring_set_snapshots(ring, 0) == 1, "back to a stream");
    CHECK(geode_pcm_ring_write_bytes(ring, snap.data(), snap.size(), GEODE_PCM_U8, 1, kRate, nullptr) == kSnap,
          "back to a stream");
    CHECK(geode_pcm_ring_set_snapshots(nullptr, 1) == 0, "a null ring");
    geode_pcm_ring_destroy(ring);
}

// musical_test's clicks (a 60 Hz thump and a 3 kHz tick, the downbeat loud) with a kick's length: the
// snapshots hear 21 ms of every 50 ms, so a hit shorter than the gap between two is heard only when one
// lands on it (musical_test's 5 ms tick at 90 BPM is caught on one beat in three and reads as 30 BPM).
// What falls between snapshots is not in the capture at all; no stitch can bring it back.
double clicks(double t, double bpm) {
    const double period = 60.0 / bpm;
    const int k = static_cast<int>(t / period);
    const double u = t - k * period;
    const double amp = k % 4 == 0 ? 0.8 : 0.35;
    return amp * std::exp(-u * 12.0) * std::sin(kTau * 60.0 * u) + 0.3 * std::exp(-u * 60.0) * std::sin(kTau * 3000.0 * u);
}

struct Heard {
    GeodeSoundFrame last{};
};

// 30 s of clicks captured as the wallpaper captures them, each frame (60 fps) feeding the engine what the
// ring gained, as geode_viz_feed_ring does.
Heard hear(double bpm, bool stitched) {
    const Signal song = [bpm](double t) { return clicks(t, bpm); };
    PcmRing ring(kRingFrames, 2);
    Cursor cursor(ring);
    SnapshotStitch stitch;
    geode::sound::SoundEngine engine(kRate);
    // Off the click grid, so the captures fall at every phase of the beat.
    const std::vector<double> times = captureTimes(0.013, 30.0, 3.0);
    size_t next = 0;
    Heard heard;
    for (int frame = 1; frame <= 30 * 60; ++frame) {
        const double now = frame / 60.0;
        for (; next < times.size() && times[next] <= now; ++next) {
            const std::vector<uint8_t> snap = snapshotAt(song, times[next]);
            if (stitched) stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(times[next]));
            else ring.writeBytes(snap.data(), snap.size(), kUnsigned8, 1, kRate);
        }
        for (;;) {
            const Cursor::Take take = cursor.take();
            if (take.restarted) engine.reset();
            if (take.frames <= 0) break;
            engine.write(cursor.interleaved(), take.frames, cursor.channels());
        }
        engine.step(1.0f / 60.0f, heard.last);
    }
    return heard;
}

// The tempo is the music's when the snapshots are stitched, within the 50 ms the captures place a hit to;
// butted together it read 2.3x fast (or a guess past the tracker's top).
void testTempoOnTheCaptureClock() {
    for (double bpm : {75.0, 90.0, 100.0, 110.0, 128.0, 140.0, 174.0}) {
        const Heard stitched = hear(bpm, true);
        const Heard raw = hear(bpm, false);
        std::printf("  %.0f BPM clicks through snapshots: stitched %.1f BPM (conf %.2f), raw %.1f BPM (conf %.2f)\n", bpm,
                    stitched.last.bpm, stitched.last.beatConf, raw.last.bpm, raw.last.beatConf);
        CHECK(std::fabs(stitched.last.bpm / bpm - 1.0) < 0.03, "%.0f BPM stitched read %.1f", bpm, stitched.last.bpm);
        CHECK(raw.last.bpm == 0.0f || std::fabs(raw.last.bpm / bpm - 1.0) > 0.1, "%.0f BPM raw read %.1f: no bug to show", bpm,
              raw.last.bpm);
    }
}

// The loudness is the music's: a steady sine through stitched snapshots reads the LUFS the sine reads.
void testLoudness() {
    geode::sound::SoundEngine direct(kRate);
    std::vector<float> buf;
    GeodeSoundFrame f{};
    for (int frame = 1; frame <= 5 * 60; ++frame) {
        const int64_t from = std::llround((frame - 1) * kRate / 60.0);
        const int64_t end = std::llround(frame * kRate / 60.0);
        buf.resize(static_cast<size_t>(end - from));
        for (int64_t s = from; s < end; ++s) buf[static_cast<size_t>(s - from)] = static_cast<float>(kSine(static_cast<double>(s) / kRate));
        direct.write(buf.data(), static_cast<int>(buf.size()), 1);
        direct.step(1.0f / 60.0f, f);
    }
    const float reference = f.loudnessS;

    PcmRing ring(kRingFrames, 2);
    Cursor cursor(ring);
    SnapshotStitch stitch;
    geode::sound::SoundEngine engine(kRate);
    for (double t : captureTimes(0.013, 5.0, 3.0)) {
        const std::vector<uint8_t> snap = snapshotAt(kSine, t);
        stitch.write(ring, snap.data(), snap.size(), kUnsigned8, 1, kRate, ns(t));
        for (;;) {
            const Cursor::Take take = cursor.take();
            if (take.frames <= 0) break;
            engine.write(cursor.interleaved(), take.frames, cursor.channels());
        }
        engine.step(0.05f, f);
    }
    std::printf("  sine loudnessS: direct %.3f, stitched snapshots %.3f\n", reference, f.loudnessS);
    CHECK(std::fabs(f.loudnessS - reference) < 0.05f, "stitched loudness %.3f, the sine's %.3f", f.loudnessS, reference);
}

}  // namespace

int main() {
    testRealTimeNoSeams();
    testBreakStartsOver();
    testBunched();
    testAbi();
    testTempoOnTheCaptureClock();
    testLoudness();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("snapshot_stitch_test: all passed\n");
    return 0;
}
