#pragma once
#include <cstdint>

namespace geode::viz::frame::tee {

// The rules of Cast's tee (core/viz/frame/CastTee): the stream to a TV or a browser carries the picture the
// phone's own stage draws, not a second render (owner: "make sure the app is not rendering twice 2 different
// things, but merely sending what the phone does").
//
// The stage's GL thread copies its finished composite (the visual alone, before the UI blur and the app's
// glass) into one of kSlots GPU buffers, at most once per cast frame; the cast's render thread, which owns the
// encoder's input surface and its fixed-rate frame clock, takes the newest copy each frame slot and blits it
// into the encoder. The two threads never wait on each other's frame rate: the stage may draw at 60 or 120 Hz
// and the encoder still gets exactly one frame per slot, PTS from its own clock (FixedRateClock), with no
// handover of the encoder surface between threads.
//
// While the stage draws nothing (the app in the background, the screen off), its last copy goes stale after
// kStaleNs and the cast's own renderer draws the frame instead, from the saved look (CastStream): the smallest
// honest fallback, since then the phone shows nothing to copy. It starts drawing again as soon as a fresh copy
// lands.
//
// Pure, no GL: the slot hand-off, the publish cadence and the fitted rectangle are tested on the host
// (core/tests/cast_tee_test.cpp).

inline constexpr int kSlots = 3;
// How long a new stream holds its first frames for the stage's first copy while the shell says the stage draws.
// A stage with no surface yet (Visuals never laid out) makes one and stages its first scene over several frames;
// the old half second let the second renderer show another visual first.
inline constexpr int64_t kStageFirstCopyNs = 3'000'000'000;
// A copy older than this is not the stage's live picture any more: about seven cast frames at 30 fps, well past
// one late stage frame or a scene build, short enough that a backgrounded app hands over before the TV notices.
inline constexpr int64_t kStaleNs = 250'000'000;

// Triple-buffer mailbox: the writer (the stage) fills a slot that is neither the newest nor the one being read,
// then publishes it as the newest; the reader (the cast) takes the newest while it is fresh and gives it back
// when its blit is queued. Never blocks either side: with three slots a free one always exists for the writer.
struct Mailbox {
    int latest = -1;
    int reading = -1;
    int writing = -1;
    int64_t publishedNs[kSlots] = {0, 0, 0};
    int64_t lastPublishNs = 0;
    bool published = false;

    // A slot for the writer, or -1 when one is still being written.
    int beginWrite() {
        if (writing >= 0) return -1;
        for (int i = 0; i < kSlots; ++i) {
            if (i != latest && i != reading) {
                writing = i;
                return i;
            }
        }
        return -1;
    }

    // The slot being written is the newest picture, taken at nowNs.
    void publish(int slot, int64_t nowNs) {
        if (slot < 0 || slot != writing) return;
        writing = -1;
        latest = slot;
        publishedNs[slot] = nowNs;
        lastPublishNs = nowNs;
        published = true;
    }

    // The write did not finish (the driver refused, a fence timed out): the slot is free again.
    void abandon(int slot) {
        if (slot >= 0 && slot == writing) writing = -1;
    }

    // The newest slot for the reader, or -1 when there is none, it is stale, or the reader still holds one.
    int acquire(int64_t nowNs) {
        if (reading >= 0 || latest < 0) return -1;
        if (nowNs - publishedNs[latest] > kStaleNs) return -1;
        reading = latest;
        return reading;
    }

    void release(int slot) {
        if (slot >= 0 && slot == reading) reading = -1;
    }

    // True while the stage's copies are fresh: it leads the stream.
    bool stageLeads(int64_t nowNs) const { return latest >= 0 && nowNs - publishedNs[latest] <= kStaleNs; }
};

// Whether the stage should copy this frame: once per cast frame period. A frame a little early still counts
// (min(period / 4, 4 ms) of slack), so a 60, 90 or 120 Hz stage lands on every cast slot instead of every other.
inline bool publishDue(bool published, int64_t lastPublishNs, int64_t nowNs, int fps) {
    if (!published) return true;
    const int64_t period = 1'000'000'000LL / (fps > 0 ? fps : 30);
    const int64_t quarter = period / 4;
    const int64_t slack = quarter < 4'000'000 ? quarter : 4'000'000;
    return nowNs - lastPublishNs >= period - slack;
}

// Whether the cast leaves this frame slot empty to wait for the stage's first copy rather than draw on its own
// renderer: only at the start, before that renderer exists, and only while the stage is meant to draw (with the app
// in the background nothing will come, so the fallback draws at once).
inline bool awaitStage(bool stageDraws, bool fallbackMade, int64_t sinceOpenNs) {
    return stageDraws && !fallbackMade && sinceOpenNs >= 0 && sinceOpenNs < kStageFirstCopyNs;
}

// Where the stage's picture lands in the stream's frame: whole, centred, its own aspect kept (black bars), as
// the phone shows it; a portrait stage is pillarboxed in the 16:9 stream rather than stretched or cropped.
struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

inline Rect fit(int srcW, int srcH, int dstW, int dstH) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return {};
    int64_t w = dstW;
    int64_t h = dstH;
    if (static_cast<int64_t>(dstW) * srcH > static_cast<int64_t>(dstH) * srcW) {
        w = (static_cast<int64_t>(srcW) * dstH + srcH / 2) / srcH;   // taller than the stream: bars at the sides
    } else {
        h = (static_cast<int64_t>(srcH) * dstW + srcW / 2) / srcW;   // wider: bars above and below
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Rect r;
    r.x0 = static_cast<int>((dstW - w) / 2);
    r.y0 = static_cast<int>((dstH - h) / 2);
    r.x1 = r.x0 + static_cast<int>(w);
    r.y1 = r.y0 + static_cast<int>(h);
    return r;
}

}  // namespace geode::viz::frame::tee
