// Host-side checks for core/viz/frame/FixedRateClock (#572): Cast's frame pacing, PTS and audio cut.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a steady loop gets one frame per slot at exact, evenly spaced PTS; a stall drops the
// missed slots instead of drawing them back to back; PTS only ever goes up; the audio cut is the end of
// the frame taken, so sound runs exactly as far as the picture, through a stall too. The C ABI is checked
// against the same rule, since that is what CastStream calls.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "viz/frame/FixedRateClock.hpp"

using geode::viz::frame::CastFormat;
using geode::viz::frame::FixedRateClock;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kSec = 1'000'000'000;
constexpr int64_t kMs = 1'000'000;
constexpr int64_t kStart = 123 * kSec;   // an arbitrary monotonic origin, as System.nanoTime gives

// A render loop like CastStream's: wait, take, then spend drawNs drawing; stallAt (frame number) adds stallNs once.
struct Run {
    std::vector<int64_t> pts;
    std::vector<int64_t> takenAt;
    std::vector<int64_t> audioUs;
    std::vector<int64_t> slots;
};

Run loop(int fps, int frames, int64_t drawNs, int stallAt = -1, int64_t stallNs = 0) {
    Run r;
    FixedRateClock c = FixedRateClock::started(fps, kStart);
    int64_t now = kStart;
    for (int i = 0; i < frames; ++i) {
        now += c.waitNs(now);
        r.takenAt.push_back(now);
        r.pts.push_back(c.take(now));
        r.audioUs.push_back(c.audioUntilUs());
        r.slots.push_back(c.next - 1);
        now += drawNs;
        if (i == stallAt) now += stallNs;
    }
    return r;
}

void steadyRate() {
    const Run r = loop(30, 300, 5 * kMs);
    check(r.pts.front() == 0, "steady: the first frame's PTS is 0");
    bool exact = true;
    for (size_t i = 0; i < r.pts.size(); ++i) {
        if (r.pts[i] != static_cast<int64_t>(i) * kSec / 30) exact = false;
        if (r.takenAt[i] != kStart + r.pts[i]) exact = false;   // drawn at its slot's start, not later
    }
    check(exact, "steady: frame i has PTS i/30 s and is taken at its slot");
    check(r.pts.back() == 299 * kSec / 30, "steady: 300 frames span 299 slots, none dropped");
    // Ten seconds of frames at 30 fps is ten seconds of stream time.
    check(r.audioUs.back() == 10'000'000, "steady: 300 frames cut the audio at 10 s");

    const Run fast = loop(60, 120, 1 * kMs);
    check(fast.pts.back() == 119 * kSec / 60, "steady: 60 fps keeps its own slots");
}

void catchUpWithoutBurst() {
    // Frame 10 stalls for half a second (a scene build): 15 slots at 30 fps go by.
    const Run r = loop(30, 60, 5 * kMs, 10, 500 * kMs);
    bool onePerSlot = true;
    int unpaced = 0;
    for (size_t i = 1; i < r.pts.size(); ++i) {
        if (r.slots[i] <= r.slots[i - 1]) onePerSlot = false;
        if (r.takenAt[i] != kStart + r.pts[i]) ++unpaced;   // taken without waiting for its slot
    }
    check(onePerSlot, "stall: at most one frame per slot");
    // The old loop drew all 15 missed frames back to back here; now only the stalled take itself is late.
    check(unpaced == 1, "stall: no frames drawn back to back to catch up");
    int inWindow = 0;
    for (int64_t t : r.takenAt) {
        if (t >= r.takenAt[11] && t < r.takenAt[11] + 100 * kMs) ++inWindow;
    }
    check(inWindow <= 4, "stall: the 100 ms after it hold at most 3 slots' frames plus the late one");
    // After the stall the clock is back on its slots: the frame after it is the slot due when it was taken.
    const int64_t gap = r.pts[11] - r.pts[10];
    check(gap > 10 * kSec / 30, "stall: the missed slots are dropped (PTS jumps over them)");
    bool backOnSlots = true;
    for (size_t i = 12; i < r.pts.size(); ++i) {
        if (r.takenAt[i] != kStart + r.pts[i]) backOnSlots = false;
        if (r.pts[i] - r.pts[i - 1] != kSec / 30 && r.pts[i] - r.pts[i - 1] != kSec / 30 + 1) backOnSlots = false;
    }
    check(backOnSlots, "stall: steady again after the drop");

    // Slow drawing (every frame takes 1.5 slots) lands each frame on the slot due when it is taken, never a burst.
    const Run slow = loop(30, 30, 50 * kMs);
    bool slowOk = true;
    for (size_t i = 1; i < slow.pts.size(); ++i) {
        if (slow.pts[i] - slow.pts[i - 1] < kSec / 30) slowOk = false;
        if (slow.takenAt[i] < kStart + slow.pts[i]) slowOk = false;   // never drawn before its slot
    }
    check(slowOk, "slow draw: frames land on later slots, never early, never twice");
}

void monotonicPts() {
    // Odd rates and jittery draw times: PTS strictly increasing, always.
    for (int fps : {24, 25, 30, 50, 60, 120}) {
        FixedRateClock c = FixedRateClock::started(fps, kStart);
        int64_t now = kStart;
        int64_t last = -1;
        bool ok = true;
        for (int i = 0; i < 2000; ++i) {
            now += c.waitNs(now);
            const int64_t pts = c.take(now);
            if (pts <= last) ok = false;
            last = pts;
            now += (i * 7919) % (3 * kSec / fps);   // 0 .. 3 slots of drawing
        }
        check(ok, "monotonic: PTS strictly increasing at every rate");
    }
    // A take called early (before its slot) keeps its slot; a clock read that goes backwards takes the next slot.
    FixedRateClock c = FixedRateClock::started(30, kStart);
    const int64_t a = c.take(kStart);
    const int64_t b = c.take(kStart);   // too early: still the next slot, not the same one
    const int64_t d = c.take(kStart - kSec);
    check(a == 0 && b == kSec / 30 && d == 2 * kSec / 30, "monotonic: early or backwards takes still move forward");
    check(c.waitNs(kStart) == 3 * kSec / 30, "wait: until the next slot starts");
    check(c.waitNs(kStart + kSec) == 0, "wait: 0 once due");
    // The slot boundary is exact against PTS even where the division would land one short.
    FixedRateClock e = FixedRateClock::started(30, 0);
    check(e.dueSlot(e.ptsNs(1)) == 1 && e.dueSlot(e.ptsNs(1) - 1) == 0, "due: the slot starts exactly at its PTS");
}

void audioBoundary() {
    // The cut is the end of the frame taken: (slot + 1) / fps, so each frame queues exactly a frame of audio.
    const Run r = loop(30, 90, 5 * kMs);
    bool ok = true;
    for (size_t i = 0; i < r.audioUs.size(); ++i) {
        if (r.audioUs[i] != static_cast<int64_t>(i + 1) * 1'000'000 / 30) ok = false;
        if (r.audioUs[i] * 1000 <= r.pts[i]) ok = false;   // the cut is after the frame's own PTS
    }
    check(ok, "audio: cut at the end of each frame");
    // 48 kHz frames per video frame sum to the stream time, without drift: 3 s of frames is 144000 samples.
    check(r.audioUs.back() * 48'000 / 1'000'000 == 144'000, "audio: 90 frames at 30 fps cut at 144000 samples");

    // Through a stall the cut follows the frame taken, so the dropped slots get their audio too (the pump pads it).
    const Run s = loop(30, 30, 5 * kMs, 5, 400 * kMs);
    bool follows = true;
    for (size_t i = 0; i < s.audioUs.size(); ++i) {
        if (s.audioUs[i] != (s.slots[i] + 1) * 1'000'000 / 30) follows = false;
        if (i > 0 && s.audioUs[i] <= s.audioUs[i - 1]) follows = false;
    }
    check(s.slots[6] > s.slots[5] + 10, "audio: the stall really dropped slots");
    check(follows, "audio: the cut moves with the picture through a stall, and only forward");
    check(s.audioUs.back() * 1000 - s.pts.back() > 0 && s.audioUs.back() * 1000 - s.pts.back() <= kSec / 30 + 1000,
          "audio: after a stall sound still ends one frame past the last picture");
}

void cAbi() {
    int64_t state[GEODE_FRAME_CLOCK_STATE] = {};
    geode_frame_clock_start(state, 30, kStart);
    check(geode_frame_clock_wait_ns(state, kStart) == 0, "abi: the first frame is due at once");
    check(geode_frame_clock_take(state, kStart) == 0, "abi: first PTS 0");
    check(geode_frame_clock_audio_until_us(state) == 1'000'000 / 30, "abi: audio cut after one frame");
    check(geode_frame_clock_wait_ns(state, kStart + 10 * kMs) == kSec / 30 - 10 * kMs, "abi: wait to the next slot");
    // A one-second stall: the take lands on slot 30, not slot 1.
    check(geode_frame_clock_take(state, kStart + kSec) == kSec, "abi: a stall drops to the due slot");
    check(geode_frame_clock_audio_until_us(state) == 31 * 1'000'000 / 30, "abi: audio cut follows the dropped-to slot");
    int64_t bad[GEODE_FRAME_CLOCK_STATE] = {};
    geode_frame_clock_start(bad, 0, kStart);
    check(bad[2] == 30, "abi: a bad rate falls back to 30 fps");
    check(geode_frame_clock_take(nullptr, 0) == 0 && geode_frame_clock_wait_ns(nullptr, 0) == 0, "abi: null state is safe");

    int format[3] = {};
    geode_cast_format(format);
    check(format[0] == CastFormat::kWidth && format[1] == CastFormat::kHeight && format[2] == CastFormat::kFps,
          "abi: cast format");
    check(format[0] == 1280 && format[1] == 720 && format[2] == 30, "abi: cast stays 1280x720 at 30 fps");
}

}  // namespace

int main() {
    steadyRate();
    catchUpWithoutBurst();
    monotonicPts();
    audioBoundary();
    cAbi();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("cast_clock_test: all checks passed\n");
    return 0;
}
