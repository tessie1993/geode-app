// Host checks for speaker sync (sound/interp/SpeakerSync, viz/SoundFeed::pumpUntil, #349, #488): a live
// renderer's ear on the player's ingest ring hears up to the sample the speaker plays now, never past it, and
// never skips or repeats a sample to get there. Built and run by CI (.github/workflows/android.yml, "Native
// host tests") with the system compiler; no device.
//
// Everything here is the real thing except the sink: the ingest ring and its cursor (audio/ingest), the
// renderer's feed (viz/SoundFeed, what Renderer::feedSoundSource pumps each frame), the playout clock
// (sound/interp/PlayoutClock through geode_playout_*) and the bound (SpeakerSync). The fake sink behaves like
// Media3's DefaultAudioSink with the tap first in its chain: each buffer it takes is anchored at the ring's
// write position and then written to the ring (source samples, on the media timeline), it keeps a 250 ms
// AudioTrack buffer full, and every 10 ms it reports its position in media time, which counts speed and
// skipped silence. Each written sample carries its own index, so the ear can tell a skipped or repeated
// sample from the values alone.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Ring.hpp"
#include "sound/interp/SpeakerSync.hpp"
#include "viz/SoundFeed.hpp"

namespace {

using geode::audio::ingest::Ring;
using geode::viz::SoundFeed;
namespace interp = geode::sound::interp;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void checkNear(int64_t got, int64_t want, int64_t tolerance, const char* what) {
    if (std::llabs(got - want) > tolerance) {
        std::printf("FAIL: %s (got %lld, want %lld +- %lld)\n", what, static_cast<long long>(got),
                    static_cast<long long>(want), static_cast<long long>(tolerance));
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;      // ns per ms
constexpr int64_t kTickNs = 10 * kMs;   // how often the sink reports its position
constexpr int64_t kFrameNs = 1'000'000'000LL / 60;
constexpr int kRate = 48'000;
constexpr int kBufferFrames = 480;      // 10 ms of source per buffer the sink takes
constexpr int kEncodingF32 = 4;         // GEODE_PCM_F32
constexpr int kWrap = 65'536;

// The value written for ring sample i: exact in a float, so the ear can read the index back.
float valueOf(int64_t i) { return static_cast<float>(i % kWrap) / static_cast<float>(kWrap); }

struct Player {
    Ring ring{1 << 18, 2};
    geode_playout* clock = geode_playout_create();
    double speed = 1.0;
    int64_t trackMs = 250;
    int64_t startUs = 0;
    int64_t handled = 0;     // media frames the sink took since start (all written to the ring)
    int64_t ringBase = 0;    // ring position of the start
    double headFrames = 0;   // media frames the AudioTrack consumed
    double heardFrames = 0;  // media frames the listener heard (no link past the AudioTrack here)
    int64_t nowNs = 1'000 * kMs;
    int64_t nextTickNs = 1'000 * kMs + kTickNs;
    bool playing = false;
    std::vector<float> scratch = std::vector<float>(kBufferFrames * 2);

    Player() { ring.beginEpoch(kRate, 2, 0); }
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    ~Player() { geode_playout_destroy(clock); }

    // What the listener has heard, as a ring position (exclusive end).
    int64_t ideal() const { return ringBase + static_cast<int64_t>(std::floor(heardFrames + 1e-6)); }
    int64_t speaker() const { return geode_playout_speaker_frame(clock, nowNs); }

    void takeBuffer() {
        const int64_t first = ring.written();
        geode_playout_buffer_handled(clock, startUs + handled * 1'000'000 / kRate, first, kBufferFrames);
        for (int f = 0; f < kBufferFrames; ++f) {
            scratch[static_cast<size_t>(2 * f)] = valueOf(first + f);
            scratch[static_cast<size_t>(2 * f + 1)] = valueOf(first + f);
        }
        ring.write(scratch.data(), kBufferFrames, kEncodingF32);
        handled += kBufferFrames;
    }

    void fill() {
        const double target = headFrames + static_cast<double>(trackMs) * kRate * speed / 1000.0;
        while (static_cast<double>(handled) < target) takeBuffer();
    }

    void report() {
        const auto heard = static_cast<int64_t>(std::floor(heardFrames + 1e-6));
        geode_playout_position(clock, startUs + heard * 1'000'000 / kRate, nowNs);
    }

    void step(int64_t ns) {
        if (playing) {
            headFrames += static_cast<double>(ns) * kRate * speed / 1e9;
            heardFrames += static_cast<double>(ns) * kRate * speed / 1e9;
        }
        nowNs += ns;
        fill();
    }

    void advanceTo(int64_t t) {
        while (nowNs < t) {
            step(std::min(t, nextTickNs) - nowNs);
            if (nowNs == nextTickNs) {
                report();
                nextTickNs += kTickNs;
            }
        }
    }

    void start(int64_t atUs) {
        geode_playout_flush(clock);
        startUs = atUs;
        ringBase = ring.written();
        handled = 0;
        headFrames = 0;
        heardFrames = 0;
        fill();
        report();
        playing = true;
        geode_playout_playing(clock, 1, nowNs);
    }

    void setSpeed(double s) {
        speed = s;
        geode_playout_speed(clock, static_cast<float>(s), nowNs);
    }

    // SilenceSkipping cuts ms of silence: the tap already wrote it, the speaker never plays it.
    void skipSilence(int64_t ms) {
        const double cut = static_cast<double>(ms) * kRate / 1000.0;
        headFrames += cut;
        heardFrames += cut;
        fill();
        report();
    }
};

// The sound engine's side of the feed: checks every sample it is handed follows the last one.
struct Ear final : SoundFeed::Sink {
    int64_t lastIndex = -1;
    bool inOrder = true;
    int restarts = 0;
    int64_t frames = 0;

    void restart() override { ++restarts; }
    void write(const float* stereo, int n, int sampleRateHz) override {
        if (sampleRateHz != kRate) inOrder = false;
        for (int f = 0; f < n; ++f) {
            const auto index = static_cast<int64_t>(std::lround(stereo[2 * f] * kWrap));
            if (lastIndex >= 0 && index != (lastIndex + 1) % kWrap) inOrder = false;
            lastIndex = index;
        }
        frames += n;
    }
};

// A live renderer: the feed Renderer::feedSoundSource pumps, bounded by the clock as it bounds it.
struct Reader {
    explicit Reader(const Ring& ring) { feed.setSource(&ring); }

    SoundFeed feed;
    Ear ear;

    int64_t position() const { return feed.position(); }

    // One drawn frame at p.nowNs; checks the cursor never passes the speaker. Returns the limit.
    int64_t frame(Player& p) {
        const int64_t speaker = p.speaker();
        const int64_t limit = interp::listenLimit(p.ring.written(), speaker);
        const int64_t before = position();
        const int64_t heardBefore = ear.frames;
        feed.pumpUntil(ear, speaker);
        check(position() <= std::max(limit, before), "never past the speaker's sample");
        // Before its first sample a fresh ear moves back from the newest to the speaker; after, never back.
        if (heardBefore > 0) check(position() >= before, "the cursor never runs backwards");
        return limit;
    }

    int64_t frameAt(Player& p, int64_t t) {
        p.advanceTo(t);
        return frame(p);
    }
};

// Draws at 60 fps for ms, checking the ear never runs ahead of what is heard.
void play(Player& p, Reader& r, int64_t ms, const char* what) {
    const int64_t t0 = p.nowNs;
    for (int64_t k = 1; k * kFrameNs <= ms * kMs; ++k) {
        r.frameAt(p, t0 + k * kFrameNs);
        check(r.position() <= p.ideal() + 1, what);
    }
}

void trailsTheSpeakerNotTheTap() {
    Player p;
    Reader r(p.ring);
    p.start(10'000'000);
    check(p.ring.written() == 12'000, "1x: the start burst fills the 250 ms AudioTrack buffer");
    r.frame(p);
    check(r.position() == 0, "1x: a fresh ear starts at the speaker, not at the newest sample");
    check(r.ear.frames == 0, "1x: none of the start burst is heard before the speaker plays it");
    play(p, r, 2'000, "1x: never ahead of the speaker");
    check(r.ear.inOrder, "1x: every sample once, in order");
    check(r.ear.restarts == 1, "1x: one restart, at the first sample");
    checkNear(r.position(), p.ideal(), 800, "1x: the ear is at the speaker, within a frame");
    checkNear(p.ring.written() - r.position(), 12'000, kBufferFrames + 800,
              "1x: and trails the tap by the AudioTrack buffer instead of reading up to it");
}

void readsOneAndAHalfTimesTheSamplesAt1_5x() {
    Player p;
    Reader r(p.ring);
    p.setSpeed(1.5);
    p.start(0);
    play(p, r, 1'000, "1.5x: never ahead of the speaker");
    const int64_t before = r.position();
    play(p, r, 1'000, "1.5x: never ahead of the speaker");
    // The ring holds source samples (the tap is ahead of Sonic): one wall second is 72 000 of them.
    checkNear(r.position() - before, 72'000, 1'200 + 2, "1.5x: the ear hears 1.5 s of source per second");
    check(r.ear.inOrder, "1.5x: every sample once, in order");
}

void readsThroughSkippedSilence() {
    Player p;
    Reader r(p.ring);
    p.start(0);
    play(p, r, 1'000, "skip silence: never ahead of the speaker");
    const int64_t before = r.position();
    p.skipSilence(1'000);  // the speaker jumps 48 000 samples: more than one frame's cap
    const int64_t limit = r.frame(p);
    checkNear(limit - before, 48'000, 2, "skip silence: the bound jumps the cut");
    check(r.position() - before == SoundFeed::kMaxCatchUpFrames, "skip silence: one capped read, not the jump at once");
    play(p, r, 100, "skip silence: never ahead of the speaker");
    checkNear(r.position(), p.ideal(), 800, "skip silence: caught up within a few frames");
    check(r.ear.inOrder, "skip silence: the silence is heard, not skipped; every sample once, in order");
}

void offsetHoldsThePictureBackWithoutRewinding() {
    Player p;
    Reader r(p.ring);
    p.start(0);
    play(p, r, 1'000, "offset: never ahead of the speaker");
    const int64_t at = r.position();
    geode_playout_set_offset_ms(p.clock, 100);
    // The bound moves 4 800 samples back: the ear stands until the speaker passes where it is.
    for (int k = 0; k < 5; ++k) {
        r.frameAt(p, p.nowNs + kFrameNs);
        check(r.position() == at, "offset grew: the ear waits; it never rewinds or hears a sample twice");
    }
    play(p, r, 500, "offset: never ahead of the speaker");
    checkNear(r.position(), p.ideal() - 4'800, 800, "offset +100 ms: 100 ms behind the speaker");
    check(r.ear.inOrder, "offset: every sample once, in order");
}

void aStalledSurfaceCatchesUpOnWhatPlaysNow() {
    Player p;
    Reader r(p.ring);
    p.start(0);
    play(p, r, 500, "stall: never ahead of the speaker");
    p.advanceTo(p.nowNs + 2'000 * kMs);  // the surface drew nothing for 2 s (off screen)
    const int64_t limit = r.frame(p);
    check(r.position() == limit, "stall: back at the speaker in one frame");
    check(r.ear.frames > 0 && limit - r.position() == 0, "stall: hears the cap before the speaker, not 2 s");
}

void withoutAClockReadsTheNewest() {
    // Cast, or any ring no speaker stands behind: pump() hears the newest written sample.
    Ring ring(1 << 16, 2);
    ring.beginEpoch(kRate, 2, 0);
    Reader r(ring);
    std::vector<float> pcm(2 * 1'000);
    for (int round = 0; round < 10; ++round) {
        const int64_t first = ring.written();
        for (int f = 0; f < 1'000; ++f) {
            pcm[static_cast<size_t>(2 * f)] = valueOf(first + f);
            pcm[static_cast<size_t>(2 * f + 1)] = valueOf(first + f);
        }
        ring.write(pcm.data(), 1'000, kEncodingF32);
        r.feed.pump(r.ear);
        check(r.position() == ring.written(), "no clock: hears up to the newest written");
    }
    check(r.ear.inOrder, "no clock: every sample once, in order");
}

void anUntimedStreamHolds() {
    Player p;
    Reader r(p.ring);
    p.fill();  // the sink took buffers, but the clock was flushed since: it knows nothing yet
    geode_playout_flush(p.clock);
    r.frame(p);
    check(r.ear.frames == 0, "an untimed stream: nothing is heard until the clock knows it");
    check(interp::listenLimit(100, std::nullopt) == 100 && interp::listenLimit(100, -1) == -1 &&
              interp::listenLimit(100, 250) == 100 && interp::listenLimit(100, 40) == 40,
          "the limit: newest without a clock, hold when unknown, never past the newest");
    const interp::ReadWindow w = interp::window(10, 100, 30, 80, 1'000);
    check(w.from == 30 && w.count == 50, "an overrun cursor starts at the oldest safe sample (the ring's rule)");
    const interp::ReadWindow behind = interp::window(60, 100, 0, 40, 1'000);
    check(behind.from == 60 && behind.count == 0, "a limit behind the cursor reads nothing");
}

}  // namespace

int main() {
    trailsTheSpeakerNotTheTap();
    readsOneAndAHalfTimesTheSamplesAt1_5x();
    readsThroughSkippedSilence();
    offsetHoldsThePictureBackWithoutRewinding();
    aStalledSurfaceCatchesUpOnWhatPlaysNow();
    withoutAClockReadsTheNewest();
    anUntimedStreamHolds();
    if (failures) {
        std::printf("%d speaker sync check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("speaker_sync: all checks passed\n");
    return EXIT_SUCCESS;
}
