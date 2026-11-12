// Host checks that a cast carries the phone's own visual and fills the receiver's frame (owner, 2026-11-12: "Cast
// still broken. Shows other visuals then phone"; "stream visual does not need landscape lock but must fit screen").
//
// The stream is the stage's frames (core/viz/frame/CastTee); a second renderer draws only while the stage does not.
// Pinned here, through the C ABI the shell calls:
// - the stage's host (viz/StageRun host): while casting the view draws on every tab, also before Visuals was ever
//   laid out (it was 0 x 0 then, so the second renderer showed another visual until Visuals opened), and in the
//   stream's shape, so a portrait phone no longer reaches the TV as a narrow strip;
// - the stream's start (viz/frame/CastTeeRule awaitStage): a stage that is meant to draw gets time for its first
//   copy instead of the second renderer drawing first.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>

#include "api/geode_api.h"
#include "viz/frame/CastTeeRule.hpp"

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

int streamW() {
    int f[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(f);
    return f[0];
}

int streamH() {
    int f[GEODE_CAST_FORMAT_INTS];
    geode_cast_format(f);
    return f[1];
}

// The rect has the stream's shape to within the one pixel rounding costs.
bool streamShaped(const Placed& p) {
    if (p.w() <= 0 || p.h() <= 0) return false;
    const long long cross = static_cast<long long>(p.w()) * streamH() - static_cast<long long>(p.h()) * streamW();
    return std::llabs(cross) <= std::max(streamW(), streamH());
}

constexpr unsigned kShown = GEODE_STAGE_HOST_SHOWN;
constexpr unsigned kCasting = GEODE_STAGE_HOST_CASTING;

void withoutACastNothingChanges() {
    const Placed shown = host(kShown, 0, 0, 1080, 2400, 1080, 2400);
    check(shown.alpha == GEODE_STAGE_HOST_OPAQUE, "Visuals open: opaque");
    check(shown.r[0] == 0 && shown.r[1] == 0 && shown.r[2] == 1080 && shown.r[3] == 2400, "Visuals open: its whole rect");
    const Placed away = host(0, 0, 0, 1080, 2400, 1080, 2400);
    check(away.alpha == GEODE_STAGE_HOST_HIDDEN, "another tab: hidden");
    check(away.w() == 1080 && away.h() == 2400, "another tab: the last Visuals rect, so the surface survives");
    const Placed never = host(kShown, 0, 0, 0, 0, 1080, 2400);
    check(never.alpha == GEODE_STAGE_HOST_HIDDEN && never.w() == 0 && never.h() == 0, "not laid out yet: 0 x 0, hidden");
}

void aCastDrawsBeforeVisualsWasEverOpened() {
    // The cause: Share to PC from the Player on a cold start. The view had no rect, never drew, and the stream
    // showed the second renderer's visual until Visuals was opened.
    const Placed p = host(kCasting, 0, 0, 0, 0, 1080, 2400);
    check(p.alpha == GEODE_STAGE_HOST_FAINT, "casting, Visuals never laid out: drawing, faint");
    check(p.w() > 0 && p.h() > 0, "casting, Visuals never laid out: a real surface");
    check(p.r[0] >= 0 && p.r[1] >= 0 && p.r[2] <= 1080 && p.r[3] <= 2400, "inside the window");
    check(streamShaped(p), "in the stream's shape");
    check(p.w() == 1080, "as wide as a portrait window");
    check(p.r[1] == (2400 - p.h()) / 2, "centred");
    const Placed noWindow = host(kCasting, 0, 0, 0, 0, 0, 0);
    check(noWindow.alpha == GEODE_STAGE_HOST_HIDDEN && noWindow.w() == 0, "no window yet: nothing to lay out");
}

void aCastDrawsOnEveryTab() {
    const Placed away = host(kCasting, 0, 0, 1080, 2400, 1080, 2400);
    check(away.alpha == GEODE_STAGE_HOST_FAINT, "casting on another tab: drawing, faint");
    const Placed open = host(kShown | kCasting, 0, 120, 1080, 2280, 1080, 2400);
    check(open.alpha == GEODE_STAGE_HOST_OPAQUE, "casting on Visuals: opaque");
    check(streamShaped(open), "casting on Visuals: the stream's shape");
    check(open.r[1] >= 120 && open.r[3] <= 2280, "inside the Visuals rect");
}

void theReceiverGetsAFullFrameHoweverThePhoneIsHeld() {
    // The stage's composite is the view's size; the tee fits it into the stream. Portrait used to give a strip.
    for (const auto& [w, h] : {std::pair{1080, 2400}, std::pair{2400, 1080}, std::pair{1440, 3120}, std::pair{1600, 2560},
                               std::pair{2560, 1600}, std::pair{720, 720}}) {
        const Placed p = host(kShown | kCasting, 0, 0, w, h, w, h);
        const geode::viz::frame::tee::Rect in = geode::viz::frame::tee::fit(p.w(), p.h(), streamW(), streamH());
        const long long covered = static_cast<long long>(in.x1 - in.x0) * (in.y1 - in.y0);
        const long long frame = static_cast<long long>(streamW()) * streamH();
        check(covered * 100 >= frame * 99, "the stage's picture fills the stream's frame");
        const Placed before = host(kShown, 0, 0, w, h, w, h);
        const geode::viz::frame::tee::Rect old = geode::viz::frame::tee::fit(before.w(), before.h(), streamW(), streamH());
        if (h > w) check(static_cast<long long>(old.x1 - old.x0) * 2 < streamW(), "without a cast a portrait stage is a strip");
    }
}

void everyPlacementStaysInside() {
    std::mt19937 rng(9001);
    std::uniform_int_distribution<int> size(1, 4000);
    std::uniform_int_distribution<int> bit(0, 3);
    for (int i = 0; i < 20000; ++i) {
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
        if (laid) check(p.r[0] >= l && p.r[1] >= t && p.r[2] <= r && p.r[3] <= b, "inside the Visuals rect");
        if (!laid && casting) check(p.r[0] >= 0 && p.r[1] >= 0 && p.r[2] <= ww && p.r[3] <= wh, "inside the window");
        if (laid && !casting) check(p.r[0] == l && p.r[1] == t && p.r[2] == r && p.r[3] == b, "no cast: the rect as laid out");
        if (p.alpha == GEODE_STAGE_HOST_OPAQUE) check((flags & kShown) != 0 && laid, "opaque only on Visuals, laid out");
        if (failures > 20) return;
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
    check(geode::viz::frame::tee::awaitStage(true, false, geode::viz::frame::tee::kStageFirstCopyNs - 1), "the rule's edge");
}

}  // namespace

int main() {
    withoutACastNothingChanges();
    aCastDrawsBeforeVisualsWasEverOpened();
    aCastDrawsOnEveryTab();
    theReceiverGetsAFullFrameHoweverThePhoneIsHeld();
    everyPlacementStaysInside();
    theStreamWaitsForTheStage();
    if (failures == 0) std::printf("cast_same_visual_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
