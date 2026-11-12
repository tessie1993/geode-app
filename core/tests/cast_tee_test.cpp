// Host-side checks for core/viz/frame/CastTeeRule: how the stage's picture reaches the cast stream (TV and
// browser) without a second render. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml).
//
// What is pinned: the triple-buffer hand-off (the writer never gets the slot being read or the newest, the
// reader gets the newest only while it is fresh, abandoning and releasing free a slot, a reader holding a slot
// does not take another, nothing blocks with the reader holding one and the writer mid-copy), the stage's
// hand-over to the fallback renderer after kStaleNs and back on the next copy, the publish cadence (one copy per
// cast frame from a 60, 90, 120 or 144 Hz stage, none faster than the cast's fps, a 30 Hz stage every frame), and
// the fitted rectangle (aspect kept, centred, whole, portrait pillarboxed in 16:9, degenerate sizes empty).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "viz/frame/CastTeeRule.hpp"

namespace tee = geode::viz::frame::tee;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;

void testMailbox() {
    tee::Mailbox box;
    check(box.acquire(0) == -1 && !box.stageLeads(0), "nothing published: the fallback draws");
    const int a = box.beginWrite();
    check(a >= 0 && box.beginWrite() == -1, "one write at a time");
    check(box.acquire(0) == -1, "a slot being written is not readable");
    box.publish(a, 100 * kMs);
    check(box.latest == a && box.stageLeads(100 * kMs), "published: the stage leads");
    const int r = box.acquire(110 * kMs);
    check(r == a && box.acquire(110 * kMs) == -1, "the reader takes the newest, and only one at a time");
    const int b = box.beginWrite();
    check(b >= 0 && b != r, "the writer never gets the slot being read");
    box.publish(b, 120 * kMs);
    const int c = box.beginWrite();
    check(c >= 0 && c != r && c != b, "nor the newest: a third slot is always free");
    box.abandon(c);
    check(box.writing == -1, "an abandoned write frees its slot");
    box.release(r);
    check(box.reading == -1 && box.acquire(125 * kMs) == b, "released: the reader takes the newest again");
    box.release(b);
    box.release(b);
    check(box.reading == -1, "a second release is harmless");
    box.publish(c, 130 * kMs);
    check(box.latest == b, "publishing a slot not being written changes nothing");
    box.abandon(-1);
    box.release(-1);
    check(box.writing == -1 && box.reading == -1, "out-of-range slots are ignored");

    // A run of frames: every slot the writer gets is free, every read is the newest at that moment.
    tee::Mailbox run;
    int held = -1;
    bool ok = true;
    for (int i = 0; i < 1000; ++i) {
        const int64_t now = i * 16 * kMs;
        const int w = run.beginWrite();
        if (w < 0 || w == run.latest || w == run.reading) ok = false;
        run.publish(w, now);
        if (i % 3 == 0) {
            run.release(held);
            held = run.acquire(now);
            if (held != run.latest) ok = false;
        }
    }
    check(ok, "a thousand frames: no slot is written while read or newest");
}

void testStale() {
    tee::Mailbox box;
    box.publish(box.beginWrite(), 0);
    check(box.acquire(tee::kStaleNs) >= 0, "exactly kStaleNs old is still the stage's");
    box.release(box.reading);
    check(box.acquire(tee::kStaleNs + 1) == -1 && !box.stageLeads(tee::kStaleNs + 1),
          "older: the app went to the background, the fallback draws");
    box.publish(box.beginWrite(), tee::kStaleNs + 5 * kMs);
    check(box.acquire(tee::kStaleNs + 6 * kMs) >= 0, "the next copy hands the stream back to the stage at once");
    check(tee::kStaleNs >= 100 * kMs && tee::kStaleNs <= 500 * kMs, "stale after a few cast frames, not a moment");
}

// Copies a stage running at stageHz would publish for a cast at fps over one second.
int copiesPerSecond(double stageHz, int fps) {
    bool published = false;
    int64_t last = 0;
    int copies = 0;
    for (int i = 0; i < static_cast<int>(stageHz); ++i) {
        const int64_t now = static_cast<int64_t>(std::llround(i * 1e9 / stageHz));
        if (tee::publishDue(published, last, now, fps)) {
            published = true;
            last = now;
            ++copies;
        }
    }
    return copies;
}

void testCadence() {
    check(copiesPerSecond(60, 30) == 30, "60 Hz stage: every other frame, one per cast frame");
    check(copiesPerSecond(120, 30) == 30, "120 Hz: every fourth");
    check(copiesPerSecond(90, 30) == 30, "90 Hz: every third");
    const int at144 = copiesPerSecond(144, 30);
    check(at144 >= 28 && at144 <= 30, "144 Hz: about one per cast frame, never more");
    check(copiesPerSecond(30, 30) == 30, "a 30 Hz stage: every frame");
    check(copiesPerSecond(24, 30) == 24, "a slower stage: every frame it has");
    check(tee::publishDue(false, 0, 0, 30), "the first frame always copies");
    check(!tee::publishDue(true, 0, 5 * kMs, 30), "never twice inside a cast frame");
    check(tee::publishDue(true, 0, 30 * kMs, 0), "fps 0 reads as 30");
}

void testFit() {
    const tee::Rect same = tee::fit(1280, 720, 1280, 720);
    check(same.x0 == 0 && same.y0 == 0 && same.x1 == 1280 && same.y1 == 720, "same size: the whole frame");
    const tee::Rect portrait = tee::fit(1080, 2400, 1280, 720);
    check(portrait.y0 == 0 && portrait.y1 == 720, "a portrait stage fills the height");
    const int w = portrait.x1 - portrait.x0;
    check(std::abs(w - 324) <= 1 && std::abs(portrait.x0 - (1280 - w) / 2) <= 1, "and is pillarboxed, centred, aspect kept");
    const tee::Rect wide = tee::fit(2400, 1080, 1280, 720);
    check(wide.x0 == 0 && wide.x1 == 1280 && wide.y1 - wide.y0 == 576 && wide.y0 == 72, "a wider stage: bars above and below");
    const tee::Rect tablet = tee::fit(2560, 1600, 1280, 720);
    check(tablet.y0 == 0 && tablet.y1 == 720 && tablet.x1 - tablet.x0 == 1152 && tablet.x0 == 64, "16:10 in 16:9");
    const tee::Rect empty = tee::fit(0, 100, 1280, 720);
    check(empty.x1 == 0 && empty.y1 == 0 && tee::fit(100, 100, 0, 720).x1 == 0, "degenerate sizes: nothing");
    const tee::Rect thin = tee::fit(1, 100000, 1280, 720);
    check(thin.x1 - thin.x0 >= 1, "never zero wide");
}

}  // namespace

int main() {
    testMailbox();
    testStale();
    testCadence();
    testFit();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cast_tee_test: all passed\n");
    return 0;
}
