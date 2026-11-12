// Host-side checks for core/viz/frame/CastTeeRule: how the stage's picture reaches the cast stream (TV and
// browser) without a second render. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml).
//
// What is pinned: the triple-buffer hand-off (the writer never gets the slot being read or the newest, the
// reader gets the newest only while it is fresh, abandoning and releasing free a slot, a reader holding a slot
// does not take another, nothing blocks with the reader holding one and the writer mid-copy), the stage's
// hand-over to the fallback renderer after kStaleNs and back on the next copy, the publish cadence (one copy per
// cast frame from a 60, 90, 120 or 144 Hz stage, none faster than the cast's fps, a 30 Hz stage every frame), and
// the placement (a portrait stage turned a quarter turn, the frame's shape cropped centred, aspect kept, degenerate sizes empty).
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

void testCover() {
    const tee::Rect same = tee::cover(1280, 720, 1280, 720);
    check(same.x0 == 0 && same.y0 == 0 && same.x1 == 1280 && same.y1 == 720, "same shape: the whole picture");
    const tee::Rect portrait = tee::cover(1080, 2400, 1280, 720);
    check(portrait.x0 == 0 && portrait.x1 == 1080, "a portrait stage: its full width");
    const int h = portrait.y1 - portrait.y0;
    check(h == 608 && portrait.y0 == (2400 - 608) / 2, "its middle band, centred, the frame's shape");
    const tee::Rect wide = tee::cover(2400, 1080, 1280, 720);
    check(wide.y0 == 0 && wide.y1 == 1080 && wide.x1 - wide.x0 == 1920 && wide.x0 == 240, "a wider stage: its middle columns");
    const tee::Rect tablet = tee::cover(2560, 1600, 1280, 720);
    check(tablet.x0 == 0 && tablet.x1 == 2560 && tablet.y1 - tablet.y0 == 1440 && tablet.y0 == 80, "16:10 to 16:9");
    const tee::Rect empty = tee::cover(0, 100, 1280, 720);
    check(empty.x1 == 0 && empty.y1 == 0 && tee::cover(100, 100, 0, 720).x1 == 0, "degenerate sizes: nothing");
    const tee::Rect thin = tee::cover(1, 100000, 1280, 720);
    check(thin.x1 - thin.x0 == 1 && thin.y1 - thin.y0 >= 1, "never zero, never past the picture");
}

void testPlace() {
    // A 20:9 phone upright: turned, its long side along the frame's, only the long ends cut (1920 of 2400 rows).
    const tee::Placement portrait = tee::place(1080, 2400, 1280, 720);
    check(portrait.turned && portrait.slotW == 720 && portrait.slotH == 1280, "portrait: turned, slot the frame's transpose");
    check(portrait.crop.x0 == 0 && portrait.crop.x1 == 1080, "portrait: its full width");
    check(portrait.crop.y1 - portrait.crop.y0 == 1920 && portrait.crop.y0 == 240, "portrait: its middle 1920 rows");
    // The same phone on its side: not turned, the middle 1920 columns.
    const tee::Placement landscape = tee::place(2400, 1080, 1280, 720);
    check(!landscape.turned && landscape.slotW == 1280 && landscape.slotH == 720, "landscape: not turned");
    check(landscape.crop.x1 - landscape.crop.x0 == 1920 && landscape.crop.x0 == 240 && landscape.crop.y1 == 1080,
          "landscape: its middle 1920 columns");
    // Turned or not, the same share of the picture is kept: the phone's turn changes nothing on the TV but its sides.
    const auto area = [](const tee::Rect& r) { return static_cast<long long>(r.x1 - r.x0) * (r.y1 - r.y0); };
    check(area(portrait.crop) == area(landscape.crop), "the same pixels kept either way up");
    const tee::Placement tablet = tee::place(1600, 2560, 1280, 720);
    check(tablet.turned && tablet.crop.y1 - tablet.crop.y0 == 2560 && tablet.crop.x1 - tablet.crop.x0 == 1440,
          "a 10:16 tablet upright: turned, all its rows, its middle 1440 columns");
    const tee::Placement wideTablet = tee::place(2560, 1600, 1280, 720);
    check(!wideTablet.turned && wideTablet.crop.y1 - wideTablet.crop.y0 == 1440, "16:10 on its side: not turned");
    const tee::Placement square = tee::place(720, 720, 1280, 720);
    check(!square.turned && square.crop.y1 - square.crop.y0 == 405, "square: not turned");
    check(!tee::place(1080, 2400, 720, 1280).turned, "a portrait frame is never turned into");
    const tee::Placement empty = tee::place(0, 100, 1280, 720);
    check(!empty.turned && empty.slotW == 0 && empty.crop.x1 == 0, "degenerate sizes: nothing");
    const tee::Placement thin = tee::place(1, 100000, 1280, 720);
    check(thin.turned && thin.crop.x1 - thin.crop.x0 == 1 && thin.crop.y1 - thin.crop.y0 >= 1, "never zero");
    // The direction: the phone's top on the frame's left, its left on the frame's bottom.
    float s = 0.f;
    float t = 0.f;
    tee::turnedSource(0.f, 0.5f, s, t);
    check(t == 1.f && s == 0.5f, "the frame's left edge is the phone's top");
    tee::turnedSource(0.5f, 0.f, s, t);
    check(s == 0.f && t == 0.5f, "the frame's bottom edge is the phone's left");
    tee::turnedSource(1.f, 1.f, s, t);
    check(s == 1.f && t == 0.f, "the frame's top right is the phone's bottom right");
}

}  // namespace

int main() {
    testMailbox();
    testStale();
    testCadence();
    testCover();
    testPlace();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cast_tee_test: all passed\n");
    return 0;
}
