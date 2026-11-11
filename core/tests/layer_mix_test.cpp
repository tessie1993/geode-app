// Host test for core/audio/ingest/LayerMix: the mic layered over the player's ring, mixed at the reader.
//
// What must hold: the primary's samples stay the clock (a mixed block has the primary's frame count and
// rate), the layer is resampled to that rate with no frame lost or repeated, a silent or absent layer
// changes nothing, the layer is heard alone once the primary stops and joins again when it resumes, a
// layer that starts or stops mid-stream never plays stale audio, and its queue stays bounded under drift.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/stub core/tests/layer_mix_test.cpp
//   core/audio/ingest/LayerMix.cpp core/audio/ingest/PcmRing.cpp core/audio/ingest/AiffReader.cpp
//   core/audio/ingest/TrackFeed.cpp core/api/geode_ingest_api.cpp
//   -o layer_mix_test && ./layer_mix_test
#include <cmath>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "api/geode_ingest_handles.hpp"
#include "audio/ingest/LayerMix.hpp"
#include "audio/ingest/PcmRing.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::LayerMix;
using geode::audio::ingest::PcmRing;

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);              \
            std::fprintf(stderr, "\n");                     \
            ++failures;                                     \
        }                                                   \
    } while (0)

namespace {

constexpr float kRampStep = 1e-4f;
constexpr int kRampStart = 1000;

// The mic's test signal: frame i reads (i + kRampStart) * kRampStep, so a heard value names its frame.
float ramp(int64_t frame) { return static_cast<float>(frame + kRampStart) * kRampStep; }

// One renderer's feed, as geode_viz_feed_ring runs it: the primary's takes mixed, then the layer alone.
struct Feed {
    explicit Feed(int layerChannels = 2) : layer(1 << 15, layerChannels), cursor(primary), mix(layer) {}

    PcmRing primary{1 << 16, 2};
    PcmRing layer;
    Cursor cursor;
    LayerMix mix;

    std::vector<float> left;   // every frame the engine would have been written, in order
    std::vector<float> right;
    int mixedFrames = 0;       // this frame's
    int aloneFrames = 0;
    int aloneRate = 0;
    int resets = 0;
    bool samePointer = true;   // every mixed block so far was the primary's own
    int64_t primaryWritten = 0;
    int64_t layerWritten = 0;
    int layerCarry = 0;

    void writePrimary(int frames, float value, int rate) {
        std::vector<float> block(static_cast<size_t>(frames) * 2, value);
        primary.writeFloat(block.data(), frames, 2, rate);
        primaryWritten += frames;
    }

    // The mic: mono, in AudioRecord-sized blocks once `frames` more have built up.
    void writeLayer(int frames, int rate, int blockFrames = 1024) {
        layerCarry += frames;
        while (layerCarry >= blockFrames) {
            std::vector<float> block(static_cast<size_t>(blockFrames));
            for (int i = 0; i < blockFrames; ++i) block[static_cast<size_t>(i)] = ramp(layerWritten + i);
            layer.writeFloat(block.data(), blockFrames, 1, rate);
            layerWritten += blockFrames;
            layerCarry -= blockFrames;
        }
    }

    void frame() {
        mixedFrames = 0;
        aloneFrames = 0;
        for (int run = 0; run < 4; ++run) {
            const Cursor::Take take = cursor.take();
            if (take.restarted) ++resets;
            if (take.frames <= 0) break;
            const float* block = mix.mixed(cursor.interleaved(), take.frames, cursor.channels(), take.sampleRateHz);
            if (block != cursor.interleaved()) samePointer = false;
            keep(block, take.frames, cursor.channels());
            mixedFrames += take.frames;
        }
        const LayerMix::Alone alone = mix.takeAlone();
        if (alone.frames > 0) {
            keep(alone.interleaved, alone.frames, alone.channels);
            aloneFrames = alone.frames;
            aloneRate = alone.sampleRateHz;
        }
    }

    void keep(const float* block, int frames, int channels) {
        for (int i = 0; i < frames; ++i) {
            left.push_back(block[static_cast<size_t>(i) * static_cast<size_t>(channels)]);
            right.push_back(block[static_cast<size_t>(i) * static_cast<size_t>(channels) + (channels > 1 ? 1 : 0)]);
        }
    }
};

// From the first frame the layer is heard in: every later frame follows the one before by `step`.
// Returns how many frames carried the layer; a gap (an underrun) or a jump (a trim) fails.
int checkContinuous(const std::vector<float>& heard, float base, float step, const char* what) {
    int carried = 0;
    float last = 0.0f;
    for (size_t i = 0; i < heard.size(); ++i) {
        const float layer = heard[i] - base;
        if (carried == 0 && layer <= 0.0f) continue;
        if (carried > 0 && std::fabs(layer - last - step) > 2e-5f) {
            CHECK(false, "%s: frame %zu follows %g with %g, not by %g", what, i, last, layer, step);
            return carried;
        }
        last = layer;
        ++carried;
    }
    return carried;
}

void testSameRateSum() {
    Feed feed;
    for (int f = 0; f < 120; ++f) {
        feed.writePrimary(800, 0.25f, 48000);
        feed.writeLayer(800, 48000);
        feed.frame();
        CHECK(feed.mixedFrames == 800, "a mixed frame keeps the primary's count, got %d", feed.mixedFrames);
        CHECK(feed.aloneFrames == 0, "nothing alone while the primary flows");
    }
    CHECK(feed.left.size() == static_cast<size_t>(feed.primaryWritten), "the primary's samples are the clock");
    CHECK(feed.left.front() == 0.25f, "the layer waits to be primed");
    const int carried = checkContinuous(feed.left, 0.25f, kRampStep, "same rate");
    // Primed after 0.1 s of mic: all but the first few frames carry it.
    CHECK(carried > 120 * 800 - 48000 / 4, "the layer joined, %d frames", carried);
    CHECK(feed.left == feed.right, "a mono mic is heard on both channels");
    CHECK(!feed.mix.heardAlone(), "mixed is not alone");
    CHECK(feed.resets == 1, "only the primary's first take restarts the engine, got %d", feed.resets);
}

void testResampledToThePrimarysRate() {
    Feed feed;
    for (int f = 0; f < 600; ++f) {
        feed.writePrimary(735, 0.0f, 44100);
        feed.writeLayer(800, 48000);
        feed.frame();
        CHECK(feed.mixedFrames == 735, "44.1 kHz frames, got %d", feed.mixedFrames);
    }
    const float step = kRampStep * 48000.0f / 44100.0f;
    const int carried = checkContinuous(feed.left, 0.0f, step, "48 kHz over 44.1 kHz");
    CHECK(carried > 590 * 735, "ten seconds without an underrun or a trim, %d frames", carried);
    // Up: a 48 kHz mic over a 96 kHz track.
    Feed up;
    for (int f = 0; f < 300; ++f) {
        up.writePrimary(1600, 0.0f, 96000);
        up.writeLayer(800, 48000);
        up.frame();
    }
    CHECK(checkContinuous(up.left, 0.0f, kRampStep * 0.5f, "48 kHz over 96 kHz") > 290 * 1600, "upsampled");
}

void testSilentLayerChangesNothing() {
    Feed feed;
    for (int f = 0; f < 60; ++f) {
        feed.writePrimary(800, 0.5f, 48000);
        feed.frame();
    }
    CHECK(feed.samePointer, "with no layer audio the primary's block is handed on as it is");
    CHECK(feed.left.size() == 60u * 800u, "every primary frame once");
    for (float v : feed.left) {
        if (v != 0.5f) {
            CHECK(false, "the primary was changed: %g", v);
            break;
        }
    }
    CHECK(!feed.mix.heardAlone(), "nothing heard alone");
    // A layer that writes digital silence adds zero.
    Feed quiet;
    std::vector<float> zeros(1024, 0.0f);
    for (int f = 0; f < 60; ++f) {
        quiet.writePrimary(800, 0.5f, 48000);
        quiet.layer.writeFloat(zeros.data(), 800, 1, 48000);
        quiet.frame();
    }
    for (float v : quiet.left) {
        if (v != 0.5f) {
            CHECK(false, "silence added %g", v - 0.5f);
            break;
        }
    }
}

void testLayerAloneWithNoPrimary() {
    Feed feed;
    for (int f = 0; f < 120; ++f) {
        feed.writeLayer(800, 48000);
        feed.frame();
        CHECK(feed.mixedFrames == 0, "no primary");
    }
    CHECK(feed.aloneRate == 48000, "with no primary yet the layer keeps its own rate, got %d", feed.aloneRate);
    CHECK(feed.mix.heardAlone(), "the mic is all that is heard");
    // Every frame once, in order, from the first: the one held back is the interpolator's neighbour.
    CHECK(feed.left.size() == static_cast<size_t>(feed.layerWritten - 1), "heard %zu of %lld", feed.left.size(),
          static_cast<long long>(feed.layerWritten));
    bool exact = true;
    for (size_t i = 0; i < feed.left.size(); ++i) exact = exact && feed.left[i] == ramp(static_cast<int64_t>(i));
    CHECK(exact, "at its own rate the layer passes through sample for sample");
    CHECK(feed.resets == 1, "the layer never restarts the engine, got %d", feed.resets);
}

void testMicStartsAndStopsMidStream() {
    Feed feed;
    for (int f = 0; f < 60; ++f) {
        feed.writePrimary(800, 0.25f, 48000);
        feed.frame();
    }
    const size_t before = feed.left.size();
    // Mic on: a new epoch, then its blocks.
    feed.layer.beginEpoch();
    for (int f = 0; f < 60; ++f) {
        feed.writePrimary(800, 0.25f, 48000);
        feed.writeLayer(800, 48000);
        feed.frame();
    }
    CHECK(feed.left[before] == 0.25f, "the first frames after the mic starts are the primary's");
    CHECK(feed.left.back() > 0.25f, "then the mic is in the mix");
    CHECK(checkContinuous(feed.left, 0.25f, kRampStep, "mic on") > 50 * 800, "joined within a few frames");
    // Mic off: a new epoch and no more blocks. What waited in the queue is not played out.
    feed.layer.beginEpoch();
    const size_t stopped = feed.left.size();
    for (int f = 0; f < 60; ++f) {
        feed.writePrimary(800, 0.25f, 48000);
        feed.frame();
    }
    bool clean = true;
    for (size_t i = stopped; i < feed.left.size(); ++i) clean = clean && feed.left[i] == 0.25f;
    CHECK(clean, "after the mic stops only the primary is heard");
    CHECK(feed.left.size() == static_cast<size_t>(feed.primaryWritten), "the clock never left the primary");
    CHECK(feed.resets == 1, "mic on and off never restart the engine, got %d", feed.resets);
}

void testPauseAndResume() {
    Feed feed;
    for (int f = 0; f < 120; ++f) {
        feed.writePrimary(735, 0.0f, 44100);
        feed.writeLayer(800, 48000);
        feed.frame();
    }
    // Pause: the primary stops, the mic carries on.
    const size_t paused = feed.left.size();
    const int64_t layerAtPause = feed.layerWritten;
    int firstAlone = -1;
    for (int f = 0; f < 120; ++f) {
        feed.writeLayer(800, 48000);
        feed.frame();
        if (firstAlone < 0 && feed.aloneFrames > 0) firstAlone = f;
    }
    // 0.3 s of mic is 18 frames at 60 fps; blocks of 1024 round it up by one or two.
    CHECK(firstAlone >= 17 && firstAlone <= 20, "alone 0.3 s after the last primary block, at frame %d", firstAlone);
    CHECK(feed.aloneRate == 44100, "alone at the primary's last rate, so the engine is not rebuilt: %d", feed.aloneRate);
    CHECK(feed.mix.heardAlone(), "paused with the mic on: the mic is the source");
    const double expected = static_cast<double>(feed.layerWritten - layerAtPause) * 44100.0 / 48000.0;
    const double got = static_cast<double>(feed.left.size() - paused);
    // What waited at the pause is heard too (up to the queue's limit), and nothing is made up.
    CHECK(got >= expected - 2.0 && got <= expected + LayerMix::kMaxSeconds * 44100.0 + 2.0,
          "alone frames %g for %g of mic", got, expected);
    const float step = kRampStep * 48000.0f / 44100.0f;
    std::vector<float> tail(feed.left.begin() + static_cast<std::ptrdiff_t>(paused), feed.left.end());
    CHECK(checkContinuous(tail, 0.0f, step, "alone") == static_cast<int>(tail.size()), "no hole across the pause");
    // Resume: the first primary block ends alone; the mic joins again once primed.
    const size_t resumed = feed.left.size();
    for (int f = 0; f < 60; ++f) {
        feed.writePrimary(735, 0.0f, 44100);
        feed.writeLayer(800, 48000);
        feed.frame();
        CHECK(feed.aloneFrames == 0, "nothing alone once the primary is back");
        CHECK(!feed.mix.heardAlone(), "the player is the source again");
    }
    CHECK(feed.left.size() - resumed == 60u * 735u, "the primary's count again");
    CHECK(feed.left[resumed] == 0.0f, "the mic waits to be primed after a resume");
    CHECK(feed.left.back() > 0.0f, "and joins again");
}

void testEpochs() {
    Feed feed;
    // Stale audio in the layer's ring from before its epoch is never heard.
    feed.writeLayer(4096, 48000);
    feed.layer.beginEpoch();
    feed.frame();
    CHECK(feed.left.empty(), "nothing from before the epoch");
    for (int f = 0; f < 30; ++f) {
        feed.writeLayer(800, 48000);
        feed.frame();
    }
    CHECK(!feed.left.empty() && feed.left.front() == ramp(4096), "the first frame heard is the epoch's first");
    CHECK(feed.mix.heardAlone(), "heard alone");
    // The mic stops (its epoch ends the stream): the player counts as the source again at once.
    feed.layer.beginEpoch();
    feed.frame();
    CHECK(!feed.mix.heardAlone(), "a stopped mic is not heard");
    CHECK(feed.aloneFrames == 0, "and hands out nothing");
    // A seek on the primary restarts the engine and leaves the layer's queue alone.
    Feed seek;
    for (int f = 0; f < 60; ++f) {
        seek.writePrimary(800, 0.0f, 48000);
        seek.writeLayer(800, 48000);
        seek.frame();
    }
    seek.primary.beginEpoch();
    const size_t at = seek.left.size();
    for (int f = 0; f < 10; ++f) {
        seek.writePrimary(800, 0.0f, 48000);
        seek.writeLayer(800, 48000);
        seek.frame();
    }
    CHECK(seek.resets == 2, "the seek restarts the engine once, got %d", seek.resets);
    CHECK(seek.left[at] > 0.0f, "the mic is still in the mix on the first block after a seek");
    CHECK(checkContinuous(seek.left, 0.0f, kRampStep, "across a seek") > 60 * 800, "no hole in the mic");
}

void testQueueBoundedUnderDrift() {
    Feed feed;
    float worst = 0.0f;
    int jumps = 0;
    float last = 0.0f;
    size_t seen = 0;
    for (int f = 0; f < 3000; ++f) {
        feed.writePrimary(800, 0.0f, 48000);
        feed.writeLayer(816, 48000);  // a mic clock 2 % fast
        feed.frame();
        for (; seen < feed.left.size(); ++seen) {
            const float v = feed.left[seen];
            if (v <= 0.0f) continue;
            if (last > 0.0f && v - last > 2.0f * kRampStep) ++jumps;
            last = v;
        }
        if (last > 0.0f) worst = std::fmax(worst, ramp(feed.layerWritten) - last);
    }
    // The newest frame heard never trails the newest written by more than the queue's limit and a block.
    const float limit = static_cast<float>(LayerMix::kMaxSeconds * 48000.0 + 2048.0) * kRampStep;
    CHECK(worst <= limit, "the mic trails by %g, limit %g", worst, limit);
    CHECK(jumps >= 3 && jumps <= 12, "drift is dropped in a few trims, got %d", jumps);
}

void testMonoLayerRingAndStereoMic() {
    // A one-channel layer ring under a stereo primary: its channel goes to both.
    Feed mono(1);
    for (int f = 0; f < 60; ++f) {
        mono.writePrimary(800, 0.25f, 48000);
        mono.writeLayer(800, 48000);
        mono.frame();
    }
    CHECK(mono.left.back() > 0.25f && mono.left == mono.right, "a mono layer ring reaches both channels");
    // A stereo layer keeps its sides.
    Feed stereo;
    std::vector<float> block(2 * 800);
    for (int i = 0; i < 800; ++i) {
        block[static_cast<size_t>(i) * 2] = 0.5f;
        block[static_cast<size_t>(i) * 2 + 1] = -0.5f;
    }
    for (int f = 0; f < 60; ++f) {
        stereo.writePrimary(800, 0.25f, 48000);
        stereo.layer.writeFloat(block.data(), 800, 2, 48000);
        stereo.frame();
    }
    CHECK(stereo.left.back() == 0.75f && stereo.right.back() == -0.25f, "left %g right %g", stereo.left.back(),
          stereo.right.back());
}

void testCAbi() {
    geode_pcm_ring* player = geode_pcm_ring_create(1 << 12, 2, 0);
    geode_pcm_ring* mic = geode_pcm_ring_create(1 << 12, 2, 0);
    CHECK(player && mic, "rings");
    CHECK(player->layerRing() == nullptr, "no layer until one is set");
    geode_pcm_ring_set_layer(player, mic);
    CHECK(player->layerRing() == mic->ring, "the layer is the mic's ring");
    CHECK(mic->layerRing() == nullptr, "layering is one way");
    geode_pcm_ring_set_layer(player, player);
    CHECK(player->layerRing() == mic->ring, "a ring is never its own layer");
    geode_pcm_ring_set_layer(nullptr, mic);
    // The mic's handle goes first: the player's handle keeps the ring for its readers.
    const std::weak_ptr<const PcmRing> watch = mic->ring;
    geode_pcm_ring_destroy(mic);
    CHECK(!watch.expired(), "the layered ring outlives its handle");
    geode_pcm_ring_set_layer(player, nullptr);
    CHECK(watch.expired() && player->layerRing() == nullptr, "removed");
    geode_pcm_ring_destroy(player);
}

}  // namespace

int main() {
    testSameRateSum();
    testResampledToThePrimarysRate();
    testSilentLayerChangesNothing();
    testLayerAloneWithNoPrimary();
    testMicStartsAndStopsMidStream();
    testPauseAndResume();
    testEpochs();
    testQueueBoundedUnderDrift();
    testMonoLayerRingAndStereoMic();
    testCAbi();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("layer_mix_test: ok\n");
    return 0;
}
