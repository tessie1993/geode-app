// Host checks that a cast carries the phone's own visual and fills the receiver's frame (owner, 2026-11-12: "Cast
// still broken. Shows other visuals then phone"; on the shape: "no stripes on either but cut to fit", "Phone picture turned 90°").
//
// The stream is the stage's frames (core/viz/frame/CastTee); a second renderer draws only while the stage does not.
// Pinned here:
// - the stage's host (viz/StageRun host, through the C ABI the shell calls): while casting the view draws on every
//   tab, also before Visuals was ever laid out (it was 0 x 0 then, so the second renderer showed another visual
//   until Visuals opened), in its whole rect: no bars on the phone;
// - the tee's placement (viz/frame/CastTeeRule place): a portrait stage turned a quarter turn, then every stage
//   covers the whole stream frame, aspect kept, crop centred, no bars on the receiver;
// - the stream's start (awaitStage, through the C ABI): a stage that is meant to draw gets time for its first
//   copy instead of the second renderer drawing first.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>

#include "api/geode_api.h"
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

struct Placed {
    int alpha;
    int r[4];
    int w() const { return r[2] - r[0]; }
    int h() const { return r[3] - r[1]; }
};

Placed host(unsigned flags, int left, int top, int right, int bottom, int windowW, int windowH) {
    const int bounds[4] = {left, top, right, bottom};
    Placed p{};
    p.alpha = geode_stage_host(flags, bounds, windowW, windowH, p.r);
    return p;
}

bool is(const Placed& p, int l, int t, int r, int b) { return p.r[0] == l && p.r[1] == t && p.r[2] == r && p.r[3] == b; }

int stream(int i) {
    int f[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(f);
    return f[i];
}

constexpr unsigned kShown = GEODE_STAGE_HOST_SHOWN;
constexpr unsigned kCasting = GEODE_STAGE_HOST_CASTING;

void withoutACastNothingChanges() {
    const Placed shown = host(kShown, 0, 0, 1080, 2400, 1080, 2400);
    check(shown.alpha == GEODE_STAGE_HOST_OPAQUE && is(shown, 0, 0, 1080, 2400), "Visuals open: opaque, its whole rect");
    const Placed away = host(0, 0, 0, 1080, 2400, 1080, 2400);
    check(away.alpha == GEODE_STAGE_HOST_HIDDEN, "another tab: hidden");
    check(is(away, 0, 0, 1080, 2400), "another tab: the last Visuals rect, so the surface survives");
    const Placed never = host(kShown, 0, 0, 0, 0, 1080, 2400);
    check(never.alpha == GEODE_STAGE_HOST_HIDDEN && never.w() == 0 && never.h() == 0, "not laid out yet: 0 x 0, hidden");
}

void aCastDrawsBeforeVisualsWasEverOpened() {
    // The cause: Share to PC from the Player on a cold start. The view had no rect, never drew, and the stream
    // showed the second renderer's visual until Visuals was opened.
    const Placed p = host(kCasting, 0, 0, 0, 0, 1080, 2400);
    check(p.alpha == GEODE_STAGE_HOST_FAINT, "casting, Visuals never laid out: drawing, faint");
    check(is(p, 0, 0, 1080, 2400), "casting, Visuals never laid out: the whole window");
    const Placed noWindow = host(kCasting, 0, 0, 0, 0, 0, 0);
    check(noWindow.alpha == GEODE_STAGE_HOST_HIDDEN && noWindow.w() == 0, "no window yet: nothing to lay out");
}

void aCastDrawsOnEveryTabWithNoBarsOnThePhone() {
    const Placed away = host(kCasting, 0, 0, 1080, 2400, 1080, 2400);
    check(away.alpha == GEODE_STAGE_HOST_FAINT && is(away, 0, 0, 1080, 2400), "casting on another tab: drawing, faint");
    const Placed open = host(kShown | kCasting, 0, 120, 1080, 2280, 1080, 2400);
    check(open.alpha == GEODE_STAGE_HOST_OPAQUE && is(open, 0, 120, 1080, 2280), "casting on Visuals: its whole rect");
}

// What the stream shows of a srcW x srcH stage: a portrait stage turned, then a crop inside the picture in the
// slot's shape (the frame's, or its transpose when turned) to a pixel, centred, one side whole. The copy scales it
// over the whole frame, so the frame has no bars and nothing is stretched.
tee::Placement checkPlace(int srcW, int srcH, const char* what) {
    const int fw = stream(0);
    const int fh = stream(1);
    const tee::Placement p = tee::place(srcW, srcH, fw, fh);
    check(p.turned == (srcH > srcW), what);
    check(p.slotW == (p.turned ? fh : fw) && p.slotH == (p.turned ? fw : fh), what);
    const tee::Rect& c = p.crop;
    const int w = c.x1 - c.x0;
    const int h = c.y1 - c.y0;
    check(w > 0 && h > 0 && c.x0 >= 0 && c.y0 >= 0 && c.x1 <= srcW && c.y1 <= srcH, what);
    check(w == srcW || h == srcH, what);
    // Aspect kept: w / h equals slotW / slotH to within the half pixel rounding costs on the side cover computed.
    // That side can round to the whole picture (a stage within half a pixel of the slot's shape), so the side left
    // whole does not say which one it was: half a pixel on h is slotW / 2 of cross, on w slotH / 2.
    const long long cross = static_cast<long long>(w) * p.slotH - static_cast<long long>(h) * p.slotW;
    const int computed = w != srcW ? p.slotH : h != srcH ? p.slotW : (p.slotW > p.slotH ? p.slotW : p.slotH);
    check(std::llabs(cross) * 2 <= computed + 1, what);
    check(std::abs((c.x0 + c.x1) - srcW) <= 1 && std::abs((c.y0 + c.y1) - srcH) <= 1, what);
    return p;
}

double kept(const tee::Placement& p, int srcW, int srcH) {
    return static_cast<double>(p.crop.x1 - p.crop.x0) * (p.crop.y1 - p.crop.y0) / (static_cast<double>(srcW) * srcH);
}

void theReceiverGetsAFullFrameHoweverThePhoneIsHeld() {
    for (const auto& [w, h] : {std::pair{1080, 2400}, std::pair{2400, 1080}, std::pair{1440, 3120}, std::pair{1600, 2560},
                               std::pair{2560, 1600}, std::pair{720, 720}, std::pair{1280, 720}}) {
        // The stage while casting is its whole Visuals rect; the tee turns and crops that to the stream.
        const Placed p = host(kShown | kCasting, 0, 0, w, h, w, h);
        checkPlace(p.w(), p.h(), "the stage fills the stream: turned when portrait, centred, aspect kept");
    }
    // Upright or on its side, a 20:9 phone keeps 80 % of its picture: only the long ends go, 10 % each.
    const tee::Placement upright = checkPlace(1080, 2400, "a 1080 x 2400 phone upright");
    check(upright.turned && upright.crop.y1 - upright.crop.y0 == 1920, "upright: turned, its middle 1920 rows");
    check(kept(upright, 1080, 2400) >= 0.79, "upright: nearly the whole picture");
    const tee::Placement side = checkPlace(2400, 1080, "the same phone on its side");
    check(!side.turned && side.crop.x1 - side.crop.x0 == 1920, "on its side: its middle 1920 columns");
    check(kept(side, 2400, 1080) == kept(upright, 1080, 2400), "the same share either way up");
    const tee::Placement tall = checkPlace(1440, 3120, "a 19.5:9 phone upright");
    check(kept(tall, 1440, 3120) >= 0.82, "a 19.5:9 phone keeps more");
    // Without the turn a portrait phone kept a quarter of its height (the crop of #1107's first round).
    const tee::Rect unturned = tee::cover(1080, 2400, stream(0), stream(1));
    check(unturned.y1 - unturned.y0 == 608, "unturned, the same phone would show 608 of 2400 rows");
}

void everyPlacementAndCropStaysInside() {
    std::mt19937 rng(9001);
    std::uniform_int_distribution<int> size(1, 4000);
    std::uniform_int_distribution<int> bit(0, 3);
    for (int i = 0; i < 20000 && failures < 20; ++i) {
        const int ww = size(rng);
        const int wh = size(rng);
        const unsigned flags = static_cast<unsigned>(bit(rng));
        const bool laid = rng() % 2 == 0;
        const int l = laid ? static_cast<int>(rng() % static_cast<unsigned>(ww)) : 0;
        const int t = laid ? static_cast<int>(rng() % static_cast<unsigned>(wh)) : 0;
        const int r = laid ? l + 1 + static_cast<int>(rng() % static_cast<unsigned>(ww - l)) : 0;
        const int b = laid ? t + 1 + static_cast<int>(rng() % static_cast<unsigned>(wh - t)) : 0;
        const Placed p = host(flags, l, t, r, b, ww, wh);
        const bool casting = (flags & kCasting) != 0;
        if (casting) check(p.alpha != GEODE_STAGE_HOST_HIDDEN && p.w() > 0 && p.h() > 0, "a cast always has a drawing stage");
        if (!laid && !casting) check(p.alpha == GEODE_STAGE_HOST_HIDDEN && p.w() == 0, "no rect, no cast: 0 x 0");
        if (laid) check(is(p, l, t, r, b), "laid out: the rect as laid out, cast or not");
        if (!laid && casting) check(is(p, 0, 0, ww, wh), "not laid out, casting: the window");
        if (p.alpha == GEODE_STAGE_HOST_OPAQUE) check((flags & kShown) != 0 && laid, "opaque only on Visuals, laid out");
        checkPlace(ww, wh, "every stage size fills the stream");
    }
}

void theStreamWaitsForTheStage() {
    constexpr int64_t kMs = 1'000'000;
    check(geode_cast_tee_await_stage(1, 0, 0) == 1, "the stage draws: its first copy is awaited");
    // A stage with no surface yet makes one and stages its first scene: the old half second let the second
    // renderer draw another visual first.
    check(geode_cast_tee_await_stage(1, 0, 1500 * kMs) == 1, "still awaited at 1.5 s");
    check(geode_cast_tee_await_stage(1, 0, 3000 * kMs) == 0, "not past 3 s: the TV must not freeze");
    check(geode_cast_tee_await_stage(0, 0, 0) == 0, "the app in the background: the cast draws at once");
    check(geode_cast_tee_await_stage(1, 1, 0) == 0, "once its own renderer exists it draws, never a gap");
    check(geode_cast_tee_await_stage(1, 0, -1) == 0, "a clock that went back does not hold the stream");
    check(tee::awaitStage(true, false, tee::kStageFirstCopyNs - 1), "the rule's edge");
}

}  // namespace

int main() {
    withoutACastNothingChanges();
    aCastDrawsBeforeVisualsWasEverOpened();
    aCastDrawsOnEveryTabWithNoBarsOnThePhone();
    theReceiverGetsAFullFrameHoweverThePhoneIsHeld();
    everyPlacementAndCropStaysInside();
    theStreamWaitsForTheStage();
    if (failures == 0) std::printf("cast_same_visual_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
