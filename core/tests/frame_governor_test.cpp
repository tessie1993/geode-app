// Host-side checks for core/viz/frame/FrameGovernor, Settings › Performance's "Adapt to frame time" (owner,
// 2026-11-18). Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml) with the system
// compiler; no GL.
//
// What is pinned: the ladder's order (render scale, march scale, post FX, and the frame rate last) and its climb in
// reverse; nothing ever above the user's own values or fps maximum; a step down only after a sustained miss against
// the rate the pacer really paces to, and a step back up only after the dwell; settling frames not counted; a new
// style back on the user's settings with the backoff cleared; off, an export or a recording passing the user's
// settings through; Follow device stepping under the panel's rate; the "Now: …" read-back.
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "viz/frame/FrameGovernor.hpp"
#include "viz/frame/LiveFps.hpp"
#include "viz/frame/Quality.hpp"

namespace frame = geode::viz::frame;
namespace governor = geode::viz::frame::governor;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

constexpr float kFull = 1.0f;
const float kMid = frame::kScaleChoices[1];
const float kLow = frame::kScaleChoices[0];
// The live rungs by place, so no rate is spelled out here.
const float kRung0 = static_cast<float>(frame::kLiveFpsRungs[0]);
const float kRung1 = static_cast<float>(frame::kLiveFpsRungs[1]);
const float kRung2 = static_cast<float>(frame::kLiveFpsRungs[2]);
const float kTop = static_cast<float>(frame::kLiveFpsRungs.back());
// The dwell before a step back up: the fluid scenes' own (fluid::quality::AutoDowngrade).
constexpr float kRelax = geode::viz::fluid::quality::AutoDowngrade::kRelaxSeconds;

frame::GovernorUser user(float render, float march, bool postFx, float ceiling = 0.0f) {
    frame::GovernorUser u;
    u.renderScale = render;
    u.marchScale = march;
    u.postFx = postFx;
    u.ceilingFps = ceiling;
    return u;
}

// A surface paced at `capFps` (or the governor's cap once it set one) whose frames take `frameSeconds`, for
// `seconds`. The pacer's paced rate follows the governor's cap as the stage applies it.
struct Surface {
    frame::FrameGovernor g;
    frame::GovernorUser u = user(kFull, kFull, true);
    float capFps = kRung1;
    float panelFps = kRung1;  // Follow device's rate
    bool settling = false;
    bool held = false;

    float paced() const {
        const float own = frame::isLiveFpsFollow(capFps) ? panelFps : capFps;
        const float governed = g.capFps();
        return governed > 0.0f ? std::fmin(own, governed) : own;
    }
    // Frames at `fps` measured, the frame time slower than the paced budget when fps is under it.
    void run(float seconds, float fps) {
        for (float t = 0.0f; t < seconds; t += 1.0f / fps) {
            g.onFrame(1.0f / fps, paced(), capFps, u, settling, held);
        }
    }
    // On time: drawn at whatever the pacer paces to now.
    void onTime(float seconds) {
        for (float t = 0.0f; t < seconds;) {
            const float fps = paced();
            g.onFrame(1.0f / fps, fps, capFps, u, settling, held);
            t += 1.0f / fps;
        }
    }
    // Until the step changes or `limit` seconds pass, on time; the seconds it took.
    float onTimeUntilChange(float limit) {
        const int before = g.steps();
        float t = 0.0f;
        while (t < limit && g.steps() == before) {
            const float fps = paced();
            g.onFrame(1.0f / fps, fps, capFps, u, settling, held);
            t += 1.0f / fps;
        }
        return t;
    }
};

void ladderOrder() {
    const frame::GovernorUser u = user(kFull, kFull, true, kRung1);
    check(governor::ladderLength(u) == 6, "full quality at the second rung: two render, two march, post FX, one fps step");
    const frame::Governed s0 = governor::governed(u, 0);
    check(s0.renderScale == kFull && s0.marchScale == kFull && s0.postFx && s0.capFps == 0.0f, "no step is the user's settings");
    check(governor::governed(u, 1).renderScale == kMid && governor::governed(u, 1).marchScale == kFull,
          "render scale goes first");
    check(governor::governed(u, 2).renderScale == kLow && governor::governed(u, 2).marchScale == kFull,
          "down through the tab's scales");
    check(governor::governed(u, 3).marchScale == kMid && governor::governed(u, 4).marchScale == kLow &&
              governor::governed(u, 4).postFx,
          "then march scale");
    check(!governor::governed(u, 5).postFx && governor::governed(u, 5).capFps == 0.0f, "then post FX, the rate still the user's");
    check(governor::governed(u, 6).capFps == kRung0, "the frame rate only last");
    check(governor::governed(u, 99).capFps == kRung0 && governor::governed(u, 99).renderScale == kLow,
          "past the end it stays at the bottom");

    const frame::GovernorUser top = user(kFull, kFull, true, kTop);
    check(governor::fpsStepsUnder(kTop) == static_cast<int>(frame::kLiveFpsRungs.size()) - 1, "every lower rung is a step");
    check(governor::governed(top, 6).capFps == kRung2 && governor::governed(top, 7).capFps == kRung1 &&
              governor::governed(top, 8).capFps == kRung0,
          "the top rung steps one rung at a time");
}

void neverAboveTheUser() {
    const frame::GovernorUser low = user(kMid, kLow, false, kRung0);
    check(governor::ladderLength(low) == 1, "a cheap user has one step left: render scale");
    for (int steps = 0; steps < 10; ++steps) {
        const frame::Governed g = governor::governed(low, steps);
        check(g.renderScale <= kMid && g.marchScale == kLow && !g.postFx && g.capFps == 0.0f,
              "never above the user's settings, never a rate under the lowest rung");
    }
    for (const int rung : frame::kLiveFpsRungs) {
        const frame::GovernorUser u = user(kFull, kFull, true, static_cast<float>(rung));
        for (int steps = 0; steps <= governor::ladderLength(u); ++steps) {
            const frame::Governed g = governor::governed(u, steps);
            check(g.capFps < static_cast<float>(rung), "the governor's cap is always under the user's maximum");
            check(g.renderScale <= kFull && g.marchScale <= kFull, "the scales never rise");
        }
    }
    // A scale between the tab's choices (an old pref) steps to the next choice down.
    check(governor::scaleStepped(0.6f, 1) == kLow && governor::scaleStepsUnder(kLow) == 0, "an odd scale steps to the choice under it");
}

void offPassesThrough() {
    Surface s;
    s.g.setEnabled(false);
    s.run(20.0f, kRung0);
    check(s.g.steps() == 0 && s.g.capFps() == 0.0f && s.g.governed().renderScale == kFull && s.g.governed().postFx,
          "off: frames far over budget change nothing");

    Surface held;
    held.g.setEnabled(true);
    held.run(10.0f, kRung0);
    check(held.g.steps() > 0, "on, the same frames step down");
    held.held = true;
    held.run(0.1f, kRung0);
    check(held.g.steps() == 0 && held.g.capFps() == 0.0f && held.g.governed().renderScale == kFull,
          "an export or a recording takes the user's settings at once");
    held.g.setEnabled(false);
    held.held = false;
    held.run(0.1f, kRung0);
    check(held.g.steps() == 0, "and switching it off does too");
}

void dropsOnMisses() {
    Surface s;
    s.g.setEnabled(true);
    const float mild = kRung1 * 0.75f;  // a quarter under budget
    s.run(2.0f, mild);
    check(s.g.steps() == 0, "a miss shorter than the sustain is not a slow phone");
    s.run(1.5f, mild);
    check(s.g.steps() == 1 && s.g.governed().renderScale == kMid, "a sustained miss takes one step");

    Surface severe;
    severe.g.setEnabled(true);
    severe.run(3.5f, kRung1 * 0.5f);
    check(severe.g.steps() == 2 && severe.g.governed().renderScale == kLow, "frames far under budget take two");

    // A 60-class cap on a panel that divides it unevenly paces under it (#929): on time at that rate is on time.
    Surface uneven;
    uneven.g.setEnabled(true);
    uneven.capFps = kRung1;
    for (float t = 0.0f; t < 20.0f; t += 1.0f / (kRung1 * 0.75f)) {
        uneven.g.onFrame(1.0f / (kRung1 * 0.75f), kRung1 * 0.75f, uneven.capFps, uneven.u, false, false);
    }
    check(uneven.g.steps() == 0, "frames on time at the rate the pacer really paces to are not a miss");
}

void frameRateLastAndClimbInReverse() {
    Surface s;
    s.g.setEnabled(true);
    // Mild misses all the way down, one step at a time: the cap comes only after every quality step.
    bool capBeforeQuality = false;
    const int length = governor::ladderLength(user(kFull, kFull, true, kRung1));
    for (int i = 0; i < 100 && s.g.steps() < length; ++i) {
        s.run(0.5f, s.paced() * 0.75f);
        const frame::Governed g = s.g.governed();
        if (g.capFps > 0.0f && (g.postFx || g.renderScale > kLow || g.marchScale > kLow)) capBeforeQuality = true;
    }
    check(s.g.steps() == 6 && s.g.capFps() == kRung0, "sustained misses reach the bottom: every quality step, then the lowest rung");
    check(!capBeforeQuality, "the frame rate never drops while quality is left to give");

    // On time from here: one step back per dwell, the frame rate first.
    const float first = s.onTimeUntilChange(200.0f);
    check(first >= kRelax - 0.5f && first <= kRelax + 2.0f,
          "the first step up waits the dwell");
    check(s.g.capFps() == 0.0f && !s.g.governed().postFx && s.g.governed().renderScale == kLow, "the frame rate comes back first");
    s.onTimeUntilChange(200.0f);
    check(s.g.governed().postFx && s.g.governed().marchScale == kLow, "then post FX");
    s.onTimeUntilChange(200.0f);
    check(s.g.governed().marchScale == kMid && s.g.governed().renderScale == kLow, "then march scale");
    s.onTimeUntilChange(200.0f);
    check(s.g.governed().marchScale == kFull && s.g.governed().renderScale == kLow, "render scale last");
    s.onTimeUntilChange(200.0f);
    s.onTimeUntilChange(200.0f);
    check(s.g.steps() == 0 && s.g.governed().renderScale == kFull, "back on the user's settings");
    s.onTime(200.0f);
    check(s.g.steps() == 0 && s.g.governed().renderScale == kFull && s.g.capFps() == 0.0f, "and never above them");
}

void climbsOnlyAfterDwell() {
    Surface s;
    s.g.setEnabled(true);
    s.run(3.5f, kRung1 * 0.75f);
    check(s.g.steps() == 1, "one step down");
    s.onTime(kRelax - 5.0f);
    check(s.g.steps() == 1, "on time for less than the dwell keeps the step");
    s.onTime(6.0f);
    check(s.g.steps() == 0, "the dwell climbs one step");
    // The step up fails within the probe: the next one waits twice as long (hysteresis, no flapping).
    s.run(3.5f, kRung1 * 0.75f);
    check(s.g.steps() == 1, "a miss right after the climb takes the step again");
    s.onTime(kRelax + 5.0f);
    check(s.g.steps() == 1, "and the next climb waits longer than the dwell");
    s.onTime(kRelax);
    check(s.g.steps() == 0, "twice the dwell");
}

void settlingNotCounted() {
    Surface s;
    s.g.setEnabled(true);
    s.settling = true;
    s.run(10.0f, kRung0);
    check(s.g.steps() == 0, "frames that build or blend a style are not counted");
    s.settling = false;
    s.run(2.0f, kRung1 * 0.75f);
    check(s.g.steps() == 0, "and a miss after them starts its own count");
}

void newStyleStartsOver() {
    Surface s;
    s.g.setEnabled(true);
    s.run(3.5f, kRung1 * 0.75f);
    s.onTime(kRelax + 1.0f);
    s.run(3.5f, kRung1 * 0.75f);  // a failed climb: the wait has doubled
    s.run(10.0f, kRung1 * 0.5f);
    check(s.g.steps() >= 3, "a heavy style stepped down");
    s.g.reset();
    check(s.g.steps() == 0, "a new style drops every step");
    s.onTime(0.1f);
    check(s.g.governed().renderScale == kFull && s.g.governed().postFx && s.g.capFps() == 0.0f,
          "and draws with the user's own settings");
    s.run(3.5f, kRung1 * 0.75f);
    const float climb = s.onTimeUntilChange(200.0f);
    check(climb <= kRelax + 2.0f, "with the backoff cleared: the usual dwell, not the doubled one");
}

void followDevice() {
    Surface s;
    s.g.setEnabled(true);
    s.capFps = static_cast<float>(frame::kLiveFpsFollow);
    s.panelFps = kRung2;
    s.u = user(kFull, kFull, false);
    s.run(30.0f, kRung0 * 0.5f);
    check(s.g.capFps() > 0.0f && s.g.capFps() < kRung2, "Follow device steps under the panel's rate");
    check(s.g.capFps() >= kRung0, "never under the lowest rung");
}

void readBack() {
    Surface s;
    s.g.setEnabled(true);
    s.run(3.5f, kRung1 * 0.75f);
    const frame::AdaptNow& now = frame::adaptNow();
    check(now.steps.load() == 1 && now.renderScale.load() == kMid && now.fps.load() == kRung1 && now.postFx.load(),
          "the last live frame says its rate and step");
    const int before = now.steps.load();
    s.held = true;
    s.run(0.1f, kRung1);
    check(now.steps.load() == before, "an export or a recording says nothing");
}

}  // namespace

int main() {
    ladderOrder();
    neverAboveTheUser();
    offPassesThrough();
    dropsOnMisses();
    frameRateLastAndClimbInReverse();
    climbsOnlyAfterDwell();
    settlingNotCounted();
    newStyleStartsOver();
    followDevice();
    readBack();
    if (failures) {
        std::fprintf(stderr, "%d frame governor check(s) failed\n", failures);
        return 1;
    }
    std::printf("frame governor: all checks passed\n");
    return 0;
}
