// Host-side checks for core/viz/frame/CastAudio (#772): Cast's audio pump, as counts.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a steady feed queues exactly what it gained, one frame's worth per frame; a paused feed
// pads with silence exactly to the cut; a feed that runs ahead of the cut is held for the next frame (and
// trimmed, oldest first, past half a second); a 3-hour run at 30 fps stays sample-exact at 48 kHz, the
// position never more or less than the slots taken times 1600. The C ABI is checked against the same
// rule, since that is what CastStream calls, and the cuts come from the frame clock's own ABI. The
// encoder feed hands every queued frame to the codec once, in order, at its CastAudio position, whatever
// size the codec's input buffers are and however long it has none free, dropping (and skipping the
// position past) only what a stalled codec leaves over a second behind.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"
#include "viz/frame/CastAudio.hpp"
#include "viz/frame/FixedRateClock.hpp"

using geode::viz::frame::CastAudio;
using geode::viz::frame::CastAudioFeed;
using geode::viz::frame::FixedRateClock;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRate = 48'000;
constexpr int kFps = 30;
constexpr int64_t kPerFrame = kRate / kFps;   // 1600
constexpr int64_t kStartNs = 77LL * 1'000'000'000;

// The frame clock through its C ABI, taken on time: the cut after each take.
struct Clock {
    int64_t s[GEODE_FRAME_CLOCK_STATE] = {};
    Clock() { geode_frame_clock_start(s, kFps, kStartNs); }
    int64_t nextCutUs() {
        const int64_t now = kStartNs + s[1] * FixedRateClock::kNanosPerSec / kFps;
        geode_frame_clock_take(s, now);
        return geode_frame_clock_audio_until_us(s);
    }
};

// The pump through its C ABI.
struct Pump {
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    int64_t o[GEODE_CAST_AUDIO_STEP] = {};
    int capacity = 0;
    Pump() { capacity = geode_cast_audio_start(s, kRate); }
    void step(int64_t gained, int64_t untilUs) { geode_cast_audio_step(s, gained, untilUs, o); }
    int64_t produced() const { return s[0]; }
    int64_t held() const { return s[1]; }
    int64_t evict() const { return o[0]; }
    int64_t skip() const { return o[1]; }
    int64_t keep() const { return o[2]; }
    int64_t take() const { return o[3]; }
    int64_t pad() const { return o[4]; }
    int64_t ptsUs() const { return o[5]; }
};

void startsClean() {
    Pump p;
    check(p.capacity == kRate / 2, "start: the hold is half a second");
    check(p.s[2] == kRate && p.produced() == 0 && p.held() == 0, "start: rate set, nothing queued or held");
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    check(geode_cast_audio_start(s, 0) == CastAudio::kDefaultRateHz / 2 && s[2] == CastAudio::kDefaultRateHz,
          "start: a bad rate falls back to 48 kHz");
    geode_cast_audio_step(nullptr, 1, 1, p.o);
    geode_cast_audio_step(p.s, 1, 1, nullptr);
    check(geode_cast_audio_start(nullptr, kRate) == 0, "null state is a no-op");
}

void steadyFeed() {
    Clock clock;
    Pump p;
    bool ok = true;
    for (int i = 0; i < 300; ++i) {
        const int64_t before = p.produced();
        p.step(kPerFrame, clock.nextCutUs());
        ok = ok && p.take() == kPerFrame && p.pad() == 0 && p.held() == 0;
        ok = ok && p.skip() == 0 && p.evict() == 0 && p.keep() == kPerFrame;
        ok = ok && p.ptsUs() == before * 1'000'000 / kRate;
    }
    check(ok, "steady: each frame queues exactly what it gained, no pad, nothing held");
    check(p.produced() == 300 * kPerFrame, "steady: the position is the frames taken times 1600");
}

void pausedFeedPadsToCut() {
    Clock clock;
    Pump p;
    bool ok = true;
    for (int i = 0; i < 90; ++i) {
        const int64_t cut = clock.nextCutUs();
        p.step(0, cut);
        ok = ok && p.take() == 0 && p.pad() == kPerFrame;
        ok = ok && p.produced() == (cut * kRate + 500'000) / 1'000'000;
    }
    check(ok, "paused: silence pads each frame exactly to the cut");

    // Pausing mid-feed: what is held goes first, then silence makes up the rest of the frame.
    Pump q;
    Clock c2;
    q.step(500, 0);   // gained before the first cut: held
    check(q.take() + q.pad() == 0 && q.held() == 500, "paused: frames gained before any cut are held");
    const int64_t cut = c2.nextCutUs();
    q.step(0, cut);
    check(q.take() == 500 && q.pad() == kPerFrame - 500 && q.held() == 0, "paused: the hold, then silence to the cut");
    check(q.produced() == kPerFrame, "paused: the position lands on the cut");
}

void feedAheadIsHeld() {
    Clock clock;
    Pump p;
    const int64_t cut1 = clock.nextCutUs();
    p.step(kPerFrame, cut1);
    check(p.take() == kPerFrame, "ahead: the first frame queues its own frames");
    // The ring gains again before the cut moves (the pump ran twice inside one frame's slot).
    p.step(kPerFrame, cut1);
    check(p.take() + p.pad() == 0 && p.held() == kPerFrame, "ahead: gained past the cut is held, nothing queued");
    p.step(0, clock.nextCutUs());
    check(p.take() == kPerFrame && p.pad() == 0 && p.held() == 0, "ahead: the next frame queues what was held");

    // A feed steadily faster than the cut fills the hold, then the oldest frames are trimmed at half a second.
    Pump q;
    Clock c2;
    bool ok = true;
    for (int i = 0; i < 200; ++i) {
        q.step(kPerFrame + 400, c2.nextCutUs());
        ok = ok && q.take() == kPerFrame && q.pad() == 0 && q.held() <= q.capacity;
    }
    check(ok, "ahead: a fast feed never pads and never holds past the cap");
    // Full at half a second when the gained frames join it, one frame's worth lighter once that frame is queued.
    check(q.held() == q.capacity - kPerFrame, "ahead: the hold sits full at half a second");
    check(q.evict() == 400, "ahead: once full, the oldest frames make room for the newest");

    // One burst bigger than the hold keeps only its newest half second.
    Pump r;
    r.step(30'000, 0);
    check(r.skip() == 30'000 - r.capacity && r.keep() == r.capacity && r.held() == r.capacity,
          "ahead: a burst past the cap keeps its newest half second");
}

void threeHoursSampleExact() {
    Clock clock;
    Pump p;
    constexpr int64_t kFrames = 3LL * 3600 * kFps;   // 324 000 frames
    bool exact = true;
    bool even = true;
    bool contiguous = true;
    int64_t lastEnd = 0;
    for (int64_t i = 0; i < kFrames; ++i) {
        // A wobbling feed: a little short, a little over, sometimes nothing (a pause), never steady.
        const int64_t gained = (i % 97 == 0) ? 0 : kPerFrame + ((i % 3) - 1) * 7;
        const int64_t before = p.produced();
        p.step(gained, clock.nextCutUs());
        exact = exact && p.produced() == (i + 1) * kPerFrame;
        even = even && p.take() + p.pad() == kPerFrame;
        contiguous = contiguous && before == lastEnd && p.ptsUs() == before * 1'000'000 / kRate;
        lastEnd = p.produced();
    }
    check(exact, "3 h: the position is exactly slots times 1600 after every frame");
    check(even, "3 h: every block is exactly one frame's 1600 samples");
    check(contiguous, "3 h: each block starts where the last ended, stamped on the sample clock");
    check(p.produced() == kFrames * kPerFrame, "3 h: 518 400 000 frames queued, none lost or doubled");
    check(p.ptsUs() == (kFrames - 1) * kPerFrame * 1'000'000 / kRate, "3 h: the last block's PTS is exact");
}

void structMatchesAbi() {
    CastAudio a = CastAudio::started(kRate);
    Pump p;
    Clock clock;
    bool same = true;
    for (int i = 0; i < 120; ++i) {
        const int64_t gained = (i * 733) % 3000;
        const int64_t cut = clock.nextCutUs();
        const CastAudio::Step s = a.step(gained, cut);
        p.step(gained, cut);
        same = same && s.evict == p.evict() && s.skip == p.skip() && s.keep == p.keep() && s.take == p.take() &&
               s.pad == p.pad() && s.ptsUs == p.ptsUs() && a.produced == p.produced() && a.held == p.held();
    }
    check(same, "the C ABI is the struct's rule");
    check(a.cutFrames(33'333) == kPerFrame, "the clock's truncated cut rounds to the slot's 1600 frames");
    check(a.cutFrames(0) == 0 && a.cutFrames(-5) == 0, "no cut before the stream starts");
}

// The pump's blocks through the encoder feed, against codec input buffers of a few sizes, some frames
// with none free: every frame queued once, PTS on the pump's own sample clock, the backlog drained.
void feedLosesNothing() {
    for (const int64_t buffer : {int64_t{1024}, int64_t{1600}, int64_t{4096}, int64_t{700}}) {
        CastAudio a = CastAudio::started(kRate);
        CastAudioFeed f = CastAudioFeed::started(kRate);
        Clock clock;
        bool onClock = true;
        int64_t handed = 0;
        for (int i = 0; i < 3000; ++i) {
            const CastAudio::Step s = a.step(kPerFrame, clock.nextCutUs());
            check(f.add(s.take + s.pad) == 0, "a codec that keeps up never drops");
            const int buffers = (i % 11 == 0) ? 0 : 3;   // now and then none free
            for (int b = 0; b < buffers; ++b) {
                int64_t pts = -1;
                const int64_t n = f.take(buffer, pts);
                if (n == 0) break;
                onClock = onClock && pts == handed * 1'000'000 / kRate && n <= buffer;
                handed += n;
            }
        }
        check(onClock, "each buffer is stamped where the last ended, never larger than the codec's");
        check(handed + f.pending == a.produced && f.queued == handed, "every frame queued reaches the codec or still waits");
        check(f.pending < 2 * kPerFrame, "the backlog drains once buffers are free again");
    }
}

void feedDropsOnlyPastASecond() {
    CastAudioFeed f = CastAudioFeed::started(kRate);
    check(f.capacity() == kRate, "the backlog holds a second");
    check(f.add(30'000) == 0 && f.add(18'000) == 0, "up to the cap nothing goes");
    check(f.add(4'000) == 4'000 && f.pending == kRate && f.queued == 4'000, "past it the oldest go, the position moves past them");
    int64_t pts = 0;
    check(f.take(1024, pts) == 1024 && pts == int64_t{4'000} * 1'000'000 / kRate, "and the next buffer's PTS is where its frames sit");
    check(f.add(0) == 0 && f.add(-3) == 0, "nothing in, nothing dropped");
    CastAudioFeed g = CastAudioFeed::started(kRate);
    check(g.add(2 * kRate) == kRate && g.pending == kRate, "a block bigger than the cap keeps its newest second");
    int64_t none = -1;
    CastAudioFeed e = CastAudioFeed::started(kRate);
    check(e.take(1024, none) == 0 && none == 0, "an empty backlog takes nothing");
}

void feedMatchesAbi() {
    int64_t st[GEODE_CAST_AUDIO_FEED_STATE] = {};
    check(geode_cast_audio_feed_start(st, kRate) == kRate, "the ABI's cap is the struct's");
    CastAudioFeed f = CastAudioFeed::started(kRate);
    bool same = true;
    for (int i = 0; i < 200; ++i) {
        const int64_t block = (i * 977) % 5000;
        same = same && geode_cast_audio_feed_add(st, block) == f.add(block);
        int64_t p1 = 0;
        int64_t p2 = 0;
        same = same && geode_cast_audio_feed_take(st, 1024, &p1) == f.take(1024, p2) && p1 == p2;
        same = same && st[0] == f.queued && st[1] == f.pending;
    }
    check(same, "the feed's C ABI is the struct's rule");
}

}  // namespace

int main() {
    startsClean();
    steadyFeed();
    pausedFeedPadsToCut();
    feedAheadIsHeld();
    threeHoursSampleExact();
    structMatchesAbi();
    feedLosesNothing();
    feedDropsOnlyPastASecond();
    feedMatchesAbi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cast_audio_test: ok\n");
    return 0;
}
