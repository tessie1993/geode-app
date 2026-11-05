// Host-side checks for the export's file ring (core/audio/ingest/FileFeed, #490) and the loudness meter's
// byte feed (geode_loudness_feed_pcm). Built and run by CI ("Native export DSP tests" in
// .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: video frame n gets exactly the samples [floor(n * rate / fps), floor((n + 1) * rate / fps))
// of the range, at 24, 30 and 60 fps; the same decoded input gives the same per-frame samples whatever chunk
// sizes the decoder produced, so nothing is dropped or doubled across chunk boundaries; samples before the
// range start are dropped from a chunk's presentation time, or from the running position without one
// (AIFF); 16-bit, 8-bit and float bytes convert as the ingest reads them; a mono file reads centred; and
// the loudness meter measures the same from the decoder's bytes as from floats.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/FileFeed.hpp"

using geode::audio::ingest::FileFeed;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kPcm16 = 2;
constexpr int kPcm8 = 3;
constexpr int kPcmFloat = 4;

// A decoded track: interleaved s16, distinct enough that a dropped, doubled or shifted sample shows.
struct Track {
    int rate = 48000;
    int channels = 2;
    std::vector<int16_t> samples;
    int64_t frames() const { return static_cast<int64_t>(samples.size()) / channels; }
};

Track makeTrack(int rate, int channels, double seconds, uint32_t seed) {
    Track t;
    t.rate = rate;
    t.channels = channels;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> value(-32768, 32767);
    t.samples.resize(static_cast<size_t>(rate * seconds) * static_cast<size_t>(channels));
    for (auto& s : t.samples) s = static_cast<int16_t>(value(rng));
    return t;
}

// Collects what a frame hands the renderer.
struct Collect final : FileFeed::Sink {
    std::vector<float> stereo;
    int rate = 0;
    void audio(const float* s, int frames, int sampleRate) override {
        stereo.insert(stereo.end(), s, s + static_cast<size_t>(frames) * 2);
        rate = sampleRate;
    }
};

struct Rendered {
    std::vector<std::vector<float>> perFrame;   // each frame's stereo samples
    std::vector<int> counts;
    int64_t dropped = 0;
};

// Splits [from, to) frames into random chunk sizes in [lo, hi] (each a multiple of `step`).
std::vector<int64_t> chunking(int64_t from, int64_t to, uint32_t seed, int lo, int hi, int step) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> size(lo / step, hi / step);
    std::vector<int64_t> starts;
    for (int64_t at = from; at < to; at += static_cast<int64_t>(size(rng)) * step) starts.push_back(at);
    starts.push_back(to);
    return starts;
}

// Plays the export protocol over a decoder that starts at keyframe `keyFrame` and emits the given chunks:
// decode while the frame needs more, then deliver. `timed` false sends no presentation times (AIFF).
Rendered render(const Track& t, int64_t startUs, int64_t keyFrame, int fps, int frameCount,
                const std::vector<int64_t>& starts, bool timed) {
    geode_file_feed* handle = geode_file_feed_create(startUs);
    Rendered out;
    size_t next = 0;
    for (int n = 0; n < frameCount; ++n) {
        while (geode_file_feed_needs(handle, n, fps) && next + 1 < starts.size()) {
            const int64_t a = starts[next];
            const int64_t b = starts[next + 1];
            ++next;
            const int64_t pts = timed ? (keyFrame + a) * 1'000'000 / t.rate : -1;
            const auto* bytes = t.samples.data() + (keyFrame + a) * t.channels;
            geode_file_feed_write(handle, bytes, static_cast<size_t>((b - a) * t.channels) * sizeof(int16_t), kPcm16,
                                  t.rate, t.channels, pts);
        }
        Collect c;
        const int got = handle->feed.deliver(n, fps, c);
        out.counts.push_back(got);
        out.perFrame.push_back(std::move(c.stereo));
        if (got > 0) check(c.rate == t.rate, "every block carries the track's rate");
    }
    out.dropped = handle->feed.dropped();
    geode_file_feed_destroy(handle);
    return out;
}

int64_t frameEnd(int64_t n, int rate, int fps) { return (n + 1) * rate / fps; }

// Frame-exact counts and values at 24, 30 and 60 fps over random decoder chunks, starting mid-file after a
// seek to an earlier keyframe; and the same at every frame rate when concatenated.
void frameExactAtEveryRate() {
    const Track t = makeTrack(48000, 2, 6.0, 7);
    const int64_t startUs = 1'250'000;                    // 1.25 s into the track
    const int64_t startFrame = startUs * t.rate / 1'000'000;
    const int64_t keyFrame = 48000;                       // the extractor landed on the keyframe at 1 s
    // Multiples of 6 frames at 48 kHz keep each chunk's microsecond time exact.
    const auto starts = chunking(0, t.frames() - keyFrame, 11, 6, 9000, 6);
    const int64_t available = t.frames() - startFrame;

    std::vector<float> reference;
    for (int64_t i = startFrame * 2; i < t.frames() * 2; ++i) reference.push_back(t.samples[i] / 32768.0f);

    for (const int fps : {24, 30, 60}) {
        const int frameCount = static_cast<int>(available * fps / t.rate) + 3;   // a few past the end
        const Rendered r = render(t, startUs, keyFrame, fps, frameCount, starts, true);
        check(r.dropped == 0, "the cursor never skips (no overrun)");
        std::vector<float> all;
        bool countsExact = true;
        for (int n = 0; n < frameCount; ++n) {
            const int64_t a = n == 0 ? 0 : frameEnd(n - 1, t.rate, fps);
            const int64_t b = std::min(frameEnd(n, t.rate, fps), available);
            const int64_t want = std::max<int64_t>(b - a, 0);
            if (r.counts[n] != want) countsExact = false;
            all.insert(all.end(), r.perFrame[n].begin(), r.perFrame[n].end());
        }
        check(countsExact, "frame n gets floor((n+1)*rate/fps) - floor(n*rate/fps) samples");
        check(all == reference, "every sample from the range start once, in order (stereo passes bit-exact)");
    }
}

// The same input in different chunk sizes gives the same per-frame samples: nothing dropped or doubled at
// a chunk boundary.
void chunkSizesDoNotMatter() {
    const Track t = makeTrack(48000, 2, 3.0, 21);
    const int64_t startUs = 500'000;
    for (const int fps : {24, 30, 60}) {
        const int frames = 2 * fps;
        const auto small = chunking(0, t.frames(), 3, 6, 120, 6);
        const auto large = chunking(0, t.frames(), 4, 3000, 12000, 6);
        // One-second chunks: the largest a real decoder comes near (a whole WAV buffer at 48 kHz).
        const auto second = chunking(0, t.frames(), 5, 48000, 48000, 6);
        const Rendered a = render(t, startUs, 0, fps, frames, small, true);
        const Rendered b = render(t, startUs, 0, fps, frames, large, true);
        const Rendered c = render(t, startUs, 0, fps, frames, second, true);
        check(a.perFrame == b.perFrame, "small and large decoder chunks give the same frames");
        check(a.perFrame == c.perFrame, "random chunks and one-second chunks give the same frames");
    }
}

// AIFF has no timestamps: the start is counted from the top of the file, in arbitrary chunk sizes.
void untimedSourceCountsFromTheTop() {
    const Track t = makeTrack(44100, 2, 3.0, 5);
    const int64_t startUs = 700'000;
    const int64_t startFrame = startUs * t.rate / 1'000'000;
    const auto starts = chunking(0, t.frames(), 9, 1, 5000, 1);
    const Rendered r = render(t, startUs, 0, 30, 30, starts, false);
    std::vector<float> all;
    for (const auto& f : r.perFrame) all.insert(all.end(), f.begin(), f.end());
    check(static_cast<int64_t>(all.size()) == frameEnd(29, t.rate, 30) * 2, "30 frames of 44.1 kHz, exactly");
    bool same = true;
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i] != t.samples[static_cast<size_t>(startFrame * 2) + i] / 32768.0f) same = false;
    }
    check(same, "an untimed source starts at start_us * rate, counted from its first sample");
}

// 8-bit and float bytes, and a mono file heard centred.
void encodingsAndMono() {
    {
        FileFeed feed(0);
        const float in[] = {0.25f, -0.5f, 1.0f, -1.0f, NAN, 0.125f};   // 3 stereo frames
        check(feed.write(in, sizeof in, kPcmFloat, 48000, 2, 0) == 3, "float: 3 frames kept");
        Collect c;
        check(feed.deliver(1999, 24, c) == 3, "float: all of it delivered once written");
        check(c.stereo.size() == 6 && c.stereo[0] == 0.25f && c.stereo[3] == -1.0f, "float reads as is");
        check(c.stereo[4] == 0.0f, "a NaN reads as silence");
    }
    {
        FileFeed feed(0);
        const uint8_t in[] = {128, 255, 0, 192};
        check(feed.write(in, sizeof in, kPcm8, 8000, 2, 0) == 2, "8-bit: 2 frames kept");
        Collect c;
        feed.deliver(999, 24, c);
        check(c.stereo.size() == 4 && c.stereo[0] == 0.0f && c.stereo[2] == -1.0f && c.stereo[3] == 0.5f,
              "8-bit reads as (v - 128) / 128");
    }
    {
        FileFeed feed(0);
        const int16_t in[] = {16384, -8192};
        check(feed.write(in, sizeof in, kPcm16, 48000, 1, 0) == 2, "mono: 2 frames kept");
        Collect c;
        feed.deliver(999, 24, c);
        check(c.stereo.size() == 4 && c.stereo[0] == 0.5f && c.stereo[1] == 0.5f && c.stereo[3] == -0.25f,
              "mono is duplicated to both sides at unity");
    }
    {
        FileFeed feed(0);
        const int32_t in[] = {1, 2};
        check(feed.write(in, sizeof in, 99, 48000, 2, 0) == -1, "an unknown encoding is refused");
        check(feed.write(in, sizeof in, kPcm16, 0, 2, 0) == -1, "a zero rate is refused");
        check(feed.needs(0, 30), "nothing usable written: frame 0 still needs audio");
    }
}

// The meter fed the decoder's s16 bytes measures exactly what it measures fed the same samples as floats.
void loudnessFromBytes() {
    const Track t = makeTrack(48000, 2, 2.0, 13);
    std::vector<float> floats(t.samples.size());
    for (size_t i = 0; i < floats.size(); ++i) floats[i] = t.samples[i] / 32768.0f;
    geode_loudness* a = geode_loudness_create(48000, 2);
    geode_loudness* b = geode_loudness_create(48000, 2);
    geode_loudness_feed(a, floats.data(), floats.size());
    // In odd-sized byte chunks, as a decoder hands them, each a whole number of frames.
    size_t at = 0;
    std::mt19937 rng(17);
    std::uniform_int_distribution<int> frames(1, 3000);
    while (at < t.samples.size()) {
        const size_t n = std::min(static_cast<size_t>(frames(rng)) * 2, t.samples.size() - at);
        check(geode_loudness_feed_pcm(b, t.samples.data() + at, n * sizeof(int16_t), kPcm16) == 1, "s16 accepted");
        at += n;
    }
    geode_loudness_report ra{};
    geode_loudness_report rb{};
    geode_loudness_finish(a, &ra);
    geode_loudness_finish(b, &rb);
    check(ra.has_integrated == rb.has_integrated && ra.integrated_lufs == rb.integrated_lufs, "same integrated loudness");
    check(ra.true_peak_dbtp == rb.true_peak_dbtp && ra.sample_peak_dbfs == rb.sample_peak_dbfs, "same peaks");
    check(ra.measured_ms == rb.measured_ms, "same duration");
    const int16_t one[] = {0, 0};
    check(geode_loudness_feed_pcm(b, one, sizeof one, 21) == 0, "an encoding the meter cannot read is refused");
    geode_loudness_destroy(a);
    geode_loudness_destroy(b);
}

}  // namespace

int main() {
    frameExactAtEveryRate();
    chunkSizesDoNotMatter();
    untimedSourceCountsFromTheTop();
    encodingsAndMono();
    loudnessFromBytes();
    if (failures) {
        std::printf("%d file feed check(s) failed\n", failures);
        return 1;
    }
    std::printf("file feed: all checks passed\n");
    return 0;
}
