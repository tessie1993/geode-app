// Host checks that a cast carries the phone's own visual and fills the receiver's frame (owner, 2026-11-12: "Cast
// still broken. Shows other visuals then phone"; on the shape: "no stripes on either but cut to fit").
//
// The stream is the stage's frames (core/viz/frame/CastTee); a second renderer draws only while the stage does not.
// Pinned here:
// - the stage's host (viz/StageRun host, through the C ABI the shell calls): while casting the view draws on every
//   tab, also before Visuals was ever laid out (it was 0 x 0 then, so the second renderer showed another visual
//   until Visuals opened), in its whole rect: no bars on the phone;
// - the tee's crop (viz/frame/CastTeeRule cover): a portrait or a landscape stage fills the whole stream frame,
//   aspect kept, crop centred, no bars on the receiver;
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

// The crop of a srcW x srcH stage the stream shows: inside the picture, the frame's shape to a pixel, centred,
// one side whole. The blit stretches it over the whole frame, so the frame has no bars.
void checkCover(int srcW, int srcH, const char* what) {
    const int fw = stream(0);
    const int fh = stream(1);
    const tee::Rect c = tee::cover(srcW, srcH, fw, fh);
    const int w = c.x1 - c.x0;
    const int h = c.y1 - c.y0;
    check(w > 0 && h > 0 && c.x0 >= 0 && c.y0 >= 0 && c.x1 <= srcW && c.y1 <= srcH, what);
    check(w == srcW || h == srcH, what);
    // Aspect kept: w / h equals fw / fh to within the half pixel rounding costs on the cut side.
    const long long cross = static_cast<long long>(w) * fh - static_cast<long long>(h) * fw;
    check(std::llabs(cross) * 2 <= (w == srcW ? fw : fh) + 1, what);
    check(std::abs((c.x0 + c.x1) - srcW) <= 1 && std::abs((c.y0 + c.y1) - srcH) <= 1, what);
}

void theReceiverGetsAFullFrameHoweverThePhoneIsHeld() {
    for (const auto& [w, h] : {std::pair{1080, 2400}, std::pair{2400, 1080}, std::pair{1440, 3120}, std::pair{1600, 2560},
                               std::pair{2560, 1600}, std::pair{720, 720}, std::pair{1280, 720}}) {
        // The stage while casting is its whole Visuals rect; the tee crops that to the stream.
        const Placed p = host(kShown | kCasting, 0, 0, w, h, w, h);
        checkCover(p.w(), p.h(), "the stage fills the stream: frame shape, centred, aspect kept");
    }
    const tee::Rect portrait = tee::cover(1080, 2400, stream(0), stream(1));
    check(portrait.x1 - portrait.x0 == 1080 && portrait.y1 - portrait.y0 == 608, "a 1080 x 2400 phone: its middle 608 rows");
    const tee::Rect landscape = tee::cover(2400, 1080, stream(0), stream(1));
    check(landscape.y1 - landscape.y0 == 1080 && landscape.x1 - landscape.x0 == 1920, "on its side: its middle 1920 columns");
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
        checkCover(ww, wh, "every stage size fills the stream");
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
