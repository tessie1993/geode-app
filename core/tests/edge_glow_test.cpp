// Host-side checks for the wallpaper's edge glow (core/viz/frame/EdgeGlowMath.hpp, #439). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: the ABI options parse (and a garbled call turns the glow off), the VU-meter envelope
// (fast attack, slow release, an ease-off while nothing new is heard, the same speed at 30 and 120 fps),
// the window's flip into GL's y-up frame, the band's width from rest to full level, and the black's
// weight across the frame: clear on the rim, black inside and outside the rounded corners.
#include <cmath>
#include <cstdio>
#include <limits>

#include "viz/frame/EdgeGlowMath.hpp"

namespace eg = geode::viz::frame::edge_glow;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

GeodeSoundFrame frameWithRms(float rms) {
    GeodeSoundFrame f{};
    f.rms = rms;
    return f;
}

void optionsParse() {
    const float on[eg::kFloats] = {1.0f, 10.0f, 20.0f, 1080.0f, 2400.0f};
    const eg::Options o = eg::parse(on, eg::kFloats);
    check(o.enabled, "enabled > 0.5 turns the glow on");
    check(o.left == 10.0f && o.top == 20.0f && o.width == 1080.0f && o.height == 2400.0f, "the window is read in order");
    check(!eg::parse(on, eg::kFloats - 1).enabled, "too few floats turn it off");
    check(!eg::parse(nullptr, eg::kFloats).enabled, "no floats turn it off");
    float bad[eg::kFloats] = {1.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 100.0f, 100.0f};
    check(!eg::parse(bad, eg::kFloats).enabled, "a non-finite option turns it off");
    const float off[eg::kFloats] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    check(!eg::parse(off, eg::kFloats).enabled, "enabled 0 is off");
}

void envelopeAttacksFastReleasesSlow() {
    eg::Envelope env;
    const float dt = 1.0f / eg::kRefFps;
    // A loud frame: the target is clamped to 1 and half the gap closes in one reference frame.
    GeodeSoundFrame loud = frameWithRms(0.5f);
    check(near(eg::targetLevel(&loud), 1.0f), "a loud RMS clamps to a level of 1");
    env.step(&loud, dt);
    check(near(env.level, eg::kAttack), "one reference frame of attack closes kAttack of the gap");
    for (int i = 0; i < 30; ++i) {
        loud.rms = 0.5f + 0.001f * static_cast<float>(i + 1);  // a playing signal never repeats its RMS exactly
        env.step(&loud, dt);
    }
    check(env.level > 0.99f, "half a second of loud music holds the band wide");
    // Silence that is still fresh (a quiet passage): release is slow.
    GeodeSoundFrame quiet = frameWithRms(0.0001f);
    const float before = env.level;
    env.step(&quiet, dt);
    check(near(env.level, before * (1.0f - eg::kRelease), 1e-3f), "release closes only kRelease of the gap a frame");
    check(env.level > 0.9f, "one quiet frame barely narrows the band");
}

void envelopeEasesOffWhenNothingNewIsHeard() {
    eg::Envelope env;
    const float dt = 1.0f / eg::kRefFps;
    GeodeSoundFrame f = frameWithRms(0.2f);
    for (int i = 0; i < 20; ++i) {
        f.rms = 0.2f + 0.0001f * static_cast<float>(i);
        env.step(&f, dt);
    }
    const float playing = env.level;
    // Paused: the engine's RMS stops changing; the band must settle rather than hold.
    for (int i = 0; i < 600; ++i) env.step(&f, dt);
    check(playing > 0.5f, "a mastered level opens the band");
    check(env.level < 0.05f, "ten seconds without new sound let the band settle");
    eg::Envelope none;
    none.level = 0.8f;
    for (int i = 0; i < 600; ++i) none.step(nullptr, dt);
    check(none.level < 0.01f, "no sound frame at all is silence");
}

void envelopeIsFrameRateIndependent() {
    // A tenth of a second of attack from rest, at 30, 60 and 120 fps, lands in the same place, short of
    // the target (0.35), so the rate and not the clamp is what is compared.
    float levels[3];
    const float rates[3] = {30.0f, 60.0f, 120.0f};
    for (int r = 0; r < 3; ++r) {
        eg::Envelope env;
        GeodeSoundFrame f = frameWithRms(0.1f);
        const int frames = static_cast<int>(std::lround(rates[r] / 10.0f));
        for (int i = 0; i < frames; ++i) {
            f.rms = 0.1f + 1e-5f * static_cast<float>(i);
            env.step(&f, 1.0f / rates[r]);
        }
        levels[r] = env.level;
    }
    check(near(levels[0], levels[1], 2e-3f) && near(levels[1], levels[2], 2e-3f), "the envelope moves at the same speed at any fps");
    eg::Envelope stall;
    GeodeSoundFrame f = frameWithRms(1.0f);
    stall.step(&f, 10.0f);
    check(std::isfinite(stall.level) && stall.level <= 1.0f, "a long stall steps as at most kMaxDt");
}

void windowFlipsToGlAndShapes() {
    eg::Options whole;
    whole.enabled = true;
    const eg::Rect full = eg::glWindow(whole, 1080, 2400);
    check(full.x == 0.0f && full.y == 0.0f && full.w == 1080.0f && full.h == 2400.0f, "a zero window is the whole surface");
    eg::Options panned;
    panned.enabled = true;
    panned.left = 540.0f;
    panned.top = 0.0f;
    panned.width = 1080.0f;
    panned.height = 2400.0f;
    const eg::Rect r = eg::glWindow(panned, 2160, 2400);
    check(r.x == 540.0f && r.y == 0.0f && r.w == 1080.0f, "a panned window keeps its x");
    panned.top = 100.0f;
    panned.height = 2000.0f;
    check(eg::glWindow(panned, 2160, 2400).y == 300.0f, "y down becomes y up from the surface's bottom");

    const eg::Shape rest = eg::shape(full, 0.0f);
    const eg::Shape peak = eg::shape(full, 1.0f);
    check(near(rest.bandPx, 1080.0f * eg::kBandMin), "the band at rest is kBandMin of the short side");
    check(near(peak.bandPx, 1080.0f * (eg::kBandMin + eg::kBandSwing)), "the band at full level adds kBandSwing");
    check(near(rest.radiusPx, 1080.0f * eg::kCornerFraction) && rest.radiusPx == peak.radiusPx, "the corners do not breathe");
}

void blackWeightAcrossTheFrame() {
    eg::Rect full{0.0f, 0.0f, 1080.0f, 2400.0f};
    const eg::Shape s = eg::shape(full, 0.5f);
    // A pixel in from the rim (exactly on it, float rounding may put d a hair outside, where it is black).
    check(near(eg::blackAt(s, 540.0f, 1.0f), 0.0f), "the rim is clear");
    check(near(eg::blackAt(s, 1.0f, 1200.0f), 0.0f), "the side rim is clear");
    check(near(eg::blackAt(s, 540.0f, 1200.0f), 1.0f), "the middle of the screen is black");
    check(near(eg::blackAt(s, 540.0f, s.bandPx + 1.0f), 1.0f), "a band's width in is fully black");
    const float halfway = eg::blackAt(s, 540.0f, s.bandPx * 0.5f);
    check(halfway > 0.2f && halfway < 0.8f, "the band fades between the rim and the black");
    check(eg::blackAt(s, 540.0f, s.bandPx * 0.25f) < halfway, "the fade rises inward");
    check(near(eg::blackAt(s, 0.5f, 0.5f), 1.0f), "outside the rounded corner is black");
    check(near(eg::blackAt(s, -5.0f, 1200.0f), 1.0f), "off the window is black");
    // The colour: black is the only colour; the weight is a 0..1 alpha everywhere.
    bool inRange = true;
    for (float y = -10.0f; y <= 2410.0f; y += 37.0f) {
        for (float x = -10.0f; x <= 1090.0f; x += 41.0f) {
            const float a = eg::blackAt(s, x, y);
            inRange = inRange && std::isfinite(a) && a >= 0.0f && a <= 1.0f;
        }
    }
    check(inRange, "the black's weight is a finite 0..1 alpha across the frame");
}

}  // namespace

int main() {
    optionsParse();
    envelopeAttacksFastReleasesSlow();
    envelopeEasesOffWhenNothingNewIsHeard();
    envelopeIsFrameRateIndependent();
    windowFlipsToGlAndShapes();
    blackWeightAcrossTheFrame();
    if (failures != 0) {
        std::printf("%d edge glow check(s) failed\n", failures);
        return 1;
    }
    std::printf("edge glow: all checks passed\n");
    return 0;
}
