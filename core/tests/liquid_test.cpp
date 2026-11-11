// Host-side checks for Liquid mode's pure maths (core/viz/liquid/LiquidMath). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// What is pinned: the tier plan never silently turns the liquid off, the pour-back is frame-rate
// independent, the dive only ever dives in (a feedback zoom below 1 would back out of the liquid),
// stays inside its wander disc and obeys two fingers, the whirlpool only grows under a held finger,
// and the born clock swaps its look at the bottom of its dip, never while the disc is showing.
#include <cmath>
#include <cstdio>

#include "viz/liquid/LiquidMath.hpp"

using geode::viz::ThermalTier;
using namespace geode::viz::liquid;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void hotPhoneNeverSilentlyOff() {
    for (int p = 0; p <= 2; p++) {
        const HotPolicy policy = hotPolicyFrom(p);
        check(modeFor(ThermalTier::Full, policy) == Mode::Full, "a cool phone always runs the whole liquid");
        check(modeFor(ThermalTier::Eased, policy) == Mode::Full, "eased still runs the whole liquid");
        const Mode hot = modeFor(ThermalTier::Minimal, policy);
        check(hot != Mode::Off, "no policy turns the liquid off without saying so");
    }
    check(modeFor(ThermalTier::Reduced, HotPolicy::Simplify) == Mode::Lite, "reduced simplifies by default");
    check(modeFor(ThermalTier::Minimal, HotPolicy::Simplify) == Mode::Warp, "minimal falls back to the warp");
    check(modeFor(ThermalTier::Minimal, HotPolicy::Full) == Mode::Full, "Always full keeps the liquid");
    check(modeFor(ThermalTier::Minimal, HotPolicy::Pause) == Mode::Paused, "Pause says it paused");
    check(planFor(Mode::Full).feedback && planFor(Mode::Lite).feedback, "full and lite carry the liquid");
    check(!planFor(Mode::Warp).feedback && planFor(Mode::Warp).velocityGrid > 0, "the warp bends but keeps nothing");
    check(planFor(Mode::Lite).velocityGrid < planFor(Mode::Full).velocityGrid, "lite solves a coarser grid");
    check(planFor(Mode::Lite).liquidScale < planFor(Mode::Full).liquidScale, "lite carries a smaller liquid");
    check(planFor(Mode::Paused).velocityGrid == 0, "paused runs no solver");
    check(hotPolicyFrom(9) == HotPolicy::Simplify && bornFrom(-1) == Born::Rerolled && ageFrom(7) == Age::Pure &&
              surfaceFrom(5) == Surface::Ink,
          "out-of-range picks fall back to the defaults");
}

void injectWeightIsFrameRateIndependent() {
    for (float l : {0.0f, 0.35f, 0.7f, 1.0f}) {
        const float at60 = injectWeight(l, 1.0f / 60.0f);
        const float at120 = injectWeight(l, 1.0f / 120.0f);
        // Two 120 Hz frames keep exactly what one 60 Hz frame keeps.
        const float kept60 = 1.0f - at60;
        const float kept120 = (1.0f - at120) * (1.0f - at120);
        check(std::fabs(kept60 - kept120) < 1e-4f, "two half frames pour what one whole frame pours");
        check(at60 > 0.0f && at60 < 1.0f, "a frame always pours some style and keeps some liquid");
    }
    check(injectWeight(0.0f, 1.0f / 60.0f) > injectWeight(1.0f, 1.0f / 60.0f) * 20.0f, "Liquidity spans a short smear to deep liquid");
    check(std::fabs(injectWeight(0.0f, 1.0f / 60.0f) - 0.5f) < 1e-3f, "Liquidity 0 half re-draws each 60 Hz frame");
    check(std::isfinite(injectWeight(NAN, NAN)), "non-finite input stays finite");
    check(bornRadius(1.0f) > bornRadius(0.0f) && bornRadius(0.0f) > 0.05f, "deeper liquid opens a wider born disc");
}

void diveOnlyDivesInAndWanders() {
    Dive dive(42);
    float maxR = 0.0f;
    bool twisted = false;
    for (int i = 0; i < 60 * 120; i++) {
        const DiveFrame f = dive.step(1.0f / 60.0f, 0.3f, 0.0f, 0.0f, 0.0f, false);
        check(f.scale >= 1.0f, "the dive never backs out");
        check(f.scale < 1.05f, "one frame's magnification stays small");
        maxR = std::fmax(maxR, std::hypot(f.vpX, f.vpY));
        if (std::fabs(f.twist) > 1e-4f) twisted = true;
    }
    check(maxR <= Dive::kWander + 1e-4f, "the vanishing point stays inside its wander disc");
    check(maxR > 0.02f, "the vanishing point does wander");
    check(twisted, "a twist comes now and then");

    Dive a(1);
    Dive b(2);
    bool differs = false;
    for (int i = 0; i < 600; i++) {
        const DiveFrame fa = a.step(1.0f / 60.0f, 0.3f, 0, 0, 0, false);
        const DiveFrame fb = b.step(1.0f / 60.0f, 0.3f, 0, 0, 0, false);
        if (std::fabs(fa.vpX - fb.vpX) > 1e-3f) differs = true;
    }
    check(differs, "a different seed takes a different course");

    Dive steer(3);
    DiveFrame f;
    for (int i = 0; i < 120; i++) f = steer.step(1.0f / 60.0f, 0.3f, 0.4f, -0.3f, 1.0f, false);
    check(std::fabs(f.vpX - 0.4f) < 0.02f && std::fabs(f.vpY + 0.3f) < 0.02f, "two fingers hold the vanishing point");
    for (int i = 0; i < 60; i++) f = steer.step(1.0f / 60.0f, 0.3f, 5.0f, 5.0f, 1.0f, false);
    check(f.vpX <= Dive::kSteerReach + 1e-4f, "fingers off the edge pull no farther than the reach");

    Dive calm(4);
    for (int i = 0; i < 600; i++) f = calm.step(1.0f / 60.0f, 0.3f, 0, 0, 0, true);
    check(std::fabs(f.twist) < 1e-4f && std::hypot(f.vpX, f.vpY) < 0.01f, "reduced motion dives straight down the middle");
    check(calm.step(1.0f / 60.0f, NAN, NAN, NAN, NAN, false).scale >= 1.0f, "non-finite input holds the dive");
}

void holdSwirlsDragPushes() {
    Gestures g;
    float xy[4] = {0.1f, 0.2f, 0.0f, 0.0f};
    GestureOut out;
    for (int i = 0; i < 90; i++) out = g.step(1, xy, 1.0f / 60.0f);
    check(!out.push, "a still finger does not push");
    check(std::fabs(out.swirl) > 0.9f, "holding still grows a whirlpool");
    for (int i = 0; i < 30; i++) {
        xy[0] += 0.02f;
        out = g.step(1, xy, 1.0f / 60.0f);
    }
    check(out.push && out.vx > 1.0f, "a drag pushes along the stroke");
    check(std::fabs(out.swirl) < 0.1f, "moving calms the whirlpool");
    float two[4] = {-0.2f, 0.1f, 0.4f, 0.3f};
    out = g.step(2, two, 1.0f / 60.0f);
    check(out.steerWeight > 0.99f && std::fabs(out.steerX - 0.1f) < 1e-5f && std::fabs(out.steerY - 0.2f) < 1e-5f,
          "two fingers steer from their centre");
    for (int i = 0; i < 240; i++) out = g.step(0, nullptr, 1.0f / 60.0f);
    check(out.steerWeight < 0.01f && std::fabs(out.swirl) < 1e-5f, "lifting lets everything go");
}

void bornSwapsInsideItsDip() {
    BornClock clock(5);
    bool swapped = false;
    float gainAtSwap = 1.0f;
    for (int i = 0; i < 20 && !swapped; i++) {
        const bool trig = i == 0;
        if (clock.step(1.0f / 60.0f, trig, true)) {
            swapped = true;
            gainAtSwap = clock.gain();
        }
    }
    check(swapped, "a phrase swaps the born look");
    check(gainAtSwap < 0.1f, "the swap lands while the disc is dark");
    for (int i = 0; i < 60; i++) clock.step(1.0f / 60.0f, false, true);
    check(clock.gain() > 0.99f, "the disc comes back after the dip");
    int swaps = 0;
    for (int i = 0; i < 60 * 40; i++) swaps += clock.step(1.0f / 60.0f, false, true) ? 1 : 0;
    check(swaps == 0, "with the beat locked only the music swaps it");
    for (int i = 0; i < 60 * 40; i++) swaps += clock.step(1.0f / 60.0f, false, false) ? 1 : 0;
    check(swaps >= 2 && swaps <= 5, "unsure of the beat it swaps every 8-16 s");
}

}  // namespace

int main() {
    hotPhoneNeverSilentlyOff();
    injectWeightIsFrameRateIndependent();
    diveOnlyDivesInAndWanders();
    holdSwirlsDragPushes();
    bornSwapsInsideItsDip();
    if (failures == 0) std::printf("liquid_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
