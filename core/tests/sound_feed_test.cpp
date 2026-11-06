// Host-side checks for a renderer's native ear on the ingest ring (core/viz/SoundFeed, #481), and the
// shell-side reader cast uses (geode_ingest_reader_*). Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// The player's tap now writes the native ring and each renderer reads its own cursor natively, where
// Kotlin's SoundCursor + VisualizerRenderer.feedSound used to copy every sample across JNI. What is pinned:
// - a sound engine fed through the ring, in random write chunks, steps to exactly the frames of an engine
//   fed the same stereo directly (the old path): nothing lost, nothing repeated, stereo bit-exact;
// - a new epoch (the tap's flush) resets the engine once, at its first sample, with the new rate; a format
//   change inside an epoch (setFormat: speed, pitch) changes the rate without a reset (#358);
// - a stall longer than the catch-up cap hands over only the newest kMaxCatchUpFrames, ending on the newest
//   sample; no source hands over nothing; a new source restarts;
// - a 5.1 centre voice reaches both sides (#362);
// - the cast reader gives int16 of the downmix with its rate, from the newest sample on.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Ring.hpp"
#include "sound/SoundEngine.hpp"
#include "viz/SoundFeed.hpp"

using geode::audio::ingest::Ring;
using geode::sound::SoundEngine;
using geode::viz::SoundFeed;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRate = 48000;
constexpr int kFloat = 4;   // GEODE_PCM_F32
constexpr int kS16 = 2;     // GEODE_PCM_S16

// A short synthetic groove: a 120 BPM kick, a bass and a chord, and a stereo hat, so the engine's bands,
// onsets and stereo fields all move. Deterministic.
std::vector<float> groove(int frames, int rate) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    std::mt19937 noise(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    const double tau = 2.0 * std::numbers::pi;
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double beat = std::fmod(t, 0.5);
        const double kick = std::exp(-beat * 30.0) * std::sin(tau * (50.0 + 80.0 * std::exp(-beat * 40.0)) * beat);
        const double bass = 0.25 * std::sin(tau * 55.0 * t);
        const double chord = 0.1 * (std::sin(tau * 261.6 * t) + std::sin(tau * 329.6 * t) + std::sin(tau * 392.0 * t));
        const double hat = std::fmod(t, 0.25) < 0.02 ? 0.15 * u(noise) : 0.0;
        out[static_cast<size_t>(2 * i)] = static_cast<float>(0.6 * kick + bass + chord + hat);
        out[static_cast<size_t>(2 * i + 1)] = static_cast<float>(0.6 * kick + bass + 0.8 * chord - hat);
    }
    return out;
}

// Feeds an engine the way Renderer does: restart = reset, write = write. Records what it saw.
struct EngineSink final : SoundFeed::Sink {
    SoundEngine engine{kRate};
    int restarts = 0;
    int lastRate = 0;
    long long frames = 0;
    std::vector<float> lastBlock;
    void restart() override {
        ++restarts;
        engine.reset();
    }
    void write(const float* stereo, int n, int rate) override {
        lastRate = rate;
        frames += n;
        lastBlock.assign(stereo, stereo + 2 * n);
        engine.write(stereo, n, 2);
    }
};

bool sameFrame(const GeodeSoundFrame& a, const GeodeSoundFrame& b) {
    return std::memcmp(&a, &b, sizeof(GeodeSoundFrame)) == 0;
}

void feedMatchesDirectWrite() {
    Ring ring(1 << 16, 8);
    SoundFeed feed;
    feed.setSource(&ring);   // from the newest sample: nothing written yet
    ring.beginEpoch(kRate, 2, 0);
    EngineSink sink;
    SoundEngine direct(kRate);
    direct.reset();   // the feed's first block restarts its engine; the reference does the same
    const std::vector<float> song = groove(kRate * 6, kRate);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> chunk(1, 700);
    const int perFrame = kRate / 60;
    int at = 0;
    int mismatches = 0;
    GeodeSoundFrame a{}, b{};
    for (int frame = 0; at < kRate * 6; ++frame) {
        const int end = std::min(at + perFrame + (frame % 3) - 1, kRate * 6);   // uneven frame lengths
        const int from = at;
        while (at < end) {
            const int n = std::min(chunk(rng), end - at);
            ring.write(song.data() + static_cast<size_t>(2 * at), static_cast<size_t>(n), kFloat);
            at += n;
        }
        const int got = feed.pump(sink);
        if (got != end - from) ++mismatches;
        direct.write(song.data() + static_cast<size_t>(2 * from), end - from, 2);
        sink.engine.step(1.0f / 60.0f, a);
        direct.step(1.0f / 60.0f, b);
        if (!sameFrame(a, b)) ++mismatches;
    }
    check(mismatches == 0, "every frame: the samples written since the last, and the same sound frame as a direct write");
    check(sink.restarts == 1, "one restart, at the epoch's first sample");
    check(sink.lastRate == kRate, "with the rate it was written at");
    check(a.rms > 0.01f && a.bass > 0.0f, "the engine actually heard the groove");
}

void epochsAndFormats() {
    Ring ring(1 << 16, 8);
    SoundFeed feed;
    feed.setSource(&ring);
    EngineSink sink;
    const std::vector<float> song = groove(4096, kRate);
    ring.beginEpoch(kRate, 2, 0);
    ring.write(song.data(), 1000, kFloat);
    feed.pump(sink);
    check(sink.restarts == 1 && sink.lastRate == kRate, "first epoch: one restart at 48 kHz");
    ring.setFormat(72000, 2, 0);   // a 1.5x speed flush: same stream, new rate
    ring.write(song.data(), 1000, kFloat);
    feed.pump(sink);
    check(sink.restarts == 1, "setFormat changes the rate without a restart (#358)");
    check(sink.lastRate == 72000, "the new rate reaches the engine with its samples");
    ring.beginEpoch(44100, 2, 0);   // a seek or a track change
    ring.write(song.data(), 1000, kFloat);
    feed.pump(sink);
    check(sink.restarts == 2, "beginEpoch restarts the engine once");
    check(sink.lastRate == 44100, "and the new epoch's samples carry its rate, never the old one");
    check(sink.frames == 3000, "every sample once across the boundaries");
}

void stallKeepsTheNewest() {
    Ring ring(1 << 17, 2);
    SoundFeed feed;
    feed.setSource(&ring);
    EngineSink sink;
    ring.beginEpoch(kRate, 2, 0);
    std::vector<float> ramp(static_cast<size_t>(kRate) * 2 * 2);
    for (size_t i = 0; i < ramp.size() / 2; ++i) ramp[2 * i] = ramp[2 * i + 1] = static_cast<float>(i) / 1e6f;
    for (int i = 0; i < 2 * kRate; i += 4000) ring.write(ramp.data() + static_cast<size_t>(2 * i), 4000, kFloat);
    const int got = feed.pump(sink);
    check(got == SoundFeed::kMaxCatchUpFrames, "a 2 s stall hands over only the catch-up cap");
    const float newest = ramp[static_cast<size_t>(2 * (2 * kRate - 1))];
    check(!sink.lastBlock.empty() && sink.lastBlock.back() == newest, "ending on the newest sample written");
    check(feed.pump(sink) == 0, "nothing new, nothing handed over");

    feed.setSource(nullptr);
    ring.write(ramp.data(), 500, kFloat);
    check(feed.pump(sink) == 0, "no source hears nothing");
    const int before = sink.restarts;
    feed.setSource(&ring);
    ring.write(ramp.data(), 500, kFloat);
    check(feed.pump(sink) == 500 && sink.restarts == before + 1, "a new source starts at its newest sample and restarts");
}

void centreReachesBothSides() {
    Ring ring(1 << 14, 8);
    SoundFeed feed;
    feed.setSource(&ring);
    EngineSink sink;
    ring.beginEpoch(kRate, 6, 0);   // 5.1, default layout: L R C LFE Ls Rs
    std::vector<float> six(6 * 256, 0.0f);
    for (int i = 0; i < 256; ++i) six[static_cast<size_t>(6 * i + 2)] = 0.5f;   // a centre voice only
    ring.write(six.data(), 256, kFloat);
    check(feed.pump(sink) == 256, "5.1 frames are read as stereo frames, one each");
    const float l = sink.lastBlock[0], r = sink.lastBlock[1];
    check(l == r && l > 0.3f && l < 0.5f, "the centre reaches both sides at about -3 dB (#362)");
}

void castReaderGivesInt16() {
    geode_ingest handle(1 << 14, 8);
    geode_ingest_reader* reader = geode_ingest_reader_create(&handle);
    check(reader != nullptr, "a reader on a ring");
    check(geode_ingest_reader_create(nullptr) == nullptr, "no reader on no ring");
    handle.ring.beginEpoch(44100, 1, 0);   // mono: centred on both sides
    const int16_t mono[4] = {16384, -16384, 32767, 0};
    handle.ring.write(mono, 4, kS16);
    int16_t out[16] = {};
    int rate = 0;
    const int got = geode_ingest_reader_read_s16(reader, out, 8, &rate);
    check(got == 4 && rate == 44100, "every new frame, with its rate");
    check(out[0] == out[1] && std::abs(out[0] - 16384) <= 1, "mono to both sides, full scale kept");
    check(std::abs(out[2] + 16384) <= 1 && std::abs(out[4] - 32767) <= 1, "in order");
    check(geode_ingest_reader_read_s16(reader, out, 8, &rate) == 0 && rate == 0, "nothing new: 0 frames, rate 0");
    geode_ingest_reader_destroy(reader);
    geode_ingest_reader_destroy(nullptr);
}

}  // namespace

int main() {
    feedMatchesDirectWrite();
    epochsAndFormats();
    stallKeepsTheNewest();
    centreReachesBothSides();
    castReaderGivesInt16();
    if (failures) {
        std::printf("%d sound feed check(s) failed\n", failures);
        return 1;
    }
    std::printf("sound feed: all checks passed\n");
    return 0;
}
